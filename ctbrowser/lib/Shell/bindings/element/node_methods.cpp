//===- node_methods.cpp - Node, ParentNode, ChildNode methods -------------===//
//
// dom_bindings::install_node_methods: the Node, ParentNode and ChildNode
// methods: insertAdjacent*, tree mutation, selector matching, node comparison,
// the Node constants and the [Unscopable] lists. Geometry is in
// geometry_views.cpp; insertion validity is in pre_insert.cpp.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_node_methods(context & cx) {
    const std::initializer_list<const char *> node = {"Node"};
    const std::initializer_list<const char *> element = {"Element"};
    const std::initializer_list<const char *> parent_node = {"Element", "Document",
                                                             "DocumentFragment"};
    const std::initializer_list<const char *> child_node = {"Element", "CharacterData",
                                                            "DocumentType"};
    const auto method = [&](std::initializer_list<const char *> on, const char * name,
                            unsigned length, script::native_fn fn) {
        define_operation(cx, on, name, length, std::move(fn));
    };
    // Resolves the node passed to an insertion: one of ours by handle, or a node
    // of another document, adopted first (insertion adopts, DOM 4.2.3). Anything
    // else is an empty handle, and pre_insert_valid reports the TypeError.
    const auto insertable = [this](context & c, value v) {
        if (const node_id held = handle_of(v)) { return held; }
        if (is_a_document(v)) { return node_id{}; }
        dom_bindings * owner = owner_of(v);
        if (owner == nullptr || owner == this) { return node_id{}; }
        return node_from(c, v);
    };

    // The "insert adjacent" algorithm (DOM 4.9), shared by insertAdjacentElement,
    // insertAdjacentText and insertAdjacentHTML so that all three place nodes the
    // same way.
    //
    // Answers (parent, before), or nothing. There are two kinds of nothing and
    // the caller does not have to tell them apart: an unrecognised position is a
    // SyntaxError and `beforebegin`/`afterend` on the document element is a
    // HierarchyRequestError, both already thrown by the time this returns; an
    // element with no parent at all is the "return null" the specification
    // gives, and throws nothing.
    //
    // In the DOM the document element's parent is the Document; here it is
    // empty, because a parsed tree has no Document node (see
    // install_document_as_node), so that case is checked explicitly. A second
    // element or a text node beside `<html>` would be a second child of the
    // Document, which is what pre-insertion refuses.
    const auto adjacent_place =
        [this](context & c, node_id self,
               const std::string & given) -> std::optional<std::pair<node_id, node_id>> {
        std::string where = given;
        ascii_lower_in_place(where);
        const auto txn = doc_->read();
        if (where == "afterbegin") {
            const std::span<const node_id> kids = txn.children(self);
            return std::pair{self, kids.empty() ? node_id{} : kids.front()};
        }
        if (where == "beforeend") {
            return std::pair{self, node_id{}};
        }
        const bool before = where == "beforebegin";
        if (!before && where != "afterend") {
            throw_dom_exception(c, "SyntaxError",
                                "insertAdjacent: '" + given +
                                    "' is not one of beforebegin, afterbegin, beforeend "
                                    "or afterend");
            return std::nullopt;
        }
        const node_id parent = txn.parent(self);
        if (!parent) {
            if (self == txn.root()) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "insertAdjacent: the document element cannot have a sibling");
            }
            return std::nullopt;
        }
        if (before) { return std::pair{parent, self}; }
        const std::span<const node_id> siblings = txn.children(parent);
        node_id next;
        for (std::size_t i = 0; i + 1 < siblings.size(); ++i) {
            if (siblings[i] == self) { next = siblings[i + 1]; }
        }
        return std::pair{parent, next};
    };
    // `insertAdjacentHTML(position, markup)`: a fragment parse at one of four
    // places relative to this element. The parser and the copy are the same
    // ones innerHTML uses; only where the nodes land differs.
    method(element, "insertAdjacentHTML", 2,
           [this, adjacent_place](context & c, std::span<value> args) {
               const node_id self = receiver(c);
               if (!self || atoms_ == nullptr) { return value::undefined(); }
               const std::string where = arg_string(c, args, 0);
               const std::string markup = arg_string(c, args, 1);
               // The fragment's context is the parent for `beforebegin` and
               // `afterend` (HTML, insertAdjacentHTML), and a parent that is
               // none or the Document is a NoModificationAllowedError, not the
               // null or HierarchyRequestError insertAdjacentElement answers.
               if (ascii_iequals(where, "beforebegin") || ascii_iequals(where, "afterend")) {
                   const node_id parent = dom_parent(doc_->read(), self);
                   if (!parent || parent == doc_->document_node()) {
                       throw_dom_exception(c, "NoModificationAllowedError",
                                           "insertAdjacentHTML: the element has no parent "
                                           "element to parse the markup in");
                       return value::undefined();
                   }
               }
               const std::optional<std::pair<node_id, node_id>> place =
                   adjacent_place(c, self, where);
               if (!place) { return value::undefined(); }

               // Parsed into a scratch document, as innerHTML does and for the same
               // reason: tree_builder::parse replaces the root it is handed.
               document scratch{*atoms_};
               const node_id body = parse_html_body_fragment(scratch, markup);
               const auto from = scratch.read();

               // In order, because each node goes before the same reference rather
               // than before the one just added. copy_subtree appends to the parent it
               // is given, so the move is a no-op when the reference is empty.
               std::vector<node_id> added;
               for (const node_id child : from.children(body)) {
                   const node_id made = copy_subtree(from, child, place->first);
                   if (place->second) {
                       (void)doc_->insert_before(place->first, made, place->second);
                   }
                   added.push_back(made);
               }
               // [CEReactions]: what the fragment parser made is upgraded
               // where it landed, connected or not - see upgrade_created_subtree.
               for (const node_id made : added) { upgrade_created_subtree(made); }
               mutated();
               return value::undefined();
           });
    // The two forms that take a node rather than markup. `insertAdjacentElement`
    // returns the element it inserted, or null when the position has no place
    // (an element with no parent).
    method(
        element, "insertAdjacentElement", 2,
        [this, adjacent_place, insertable](context & c, std::span<value> args) {
            const node_id self = receiver(c);
            if (!self) { return value::null(); }
            const std::optional<std::pair<node_id, node_id>> place =
                adjacent_place(c, self, arg_string(c, args, 0));
            if (!place) { return value::null(); }
            // The IDL type is Element: a doctype or a text node is a TypeError
            // before any hierarchy question is asked. The kind is asked of the
            // document that owns the argument, which may be another one: an
            // element of another document is adopted (insertable), as
            // appendChild adopts it.
            dom_bindings * owner = owner_of(arg(args, 1));
            if (const node_id held = owner == nullptr ? node_id{} : owner->handle_of(arg(args, 1));
                held &&
                owner->doc_->read().kind(held).value_or(node_kind::element) != node_kind::element) {
                c.throw_error("TypeError", "insertAdjacentElement: the argument is not an Element");
                return value::null();
            }
            const node_id child = insertable(c, arg(args, 1));
            if (!pre_insert_valid(c, place->first, child, arg(args, 1), value::null())) {
                return value::null();
            }
            (void)insert_node(place->first, child, place->second);
            return arg(args, 1);
        });
    method(element, "insertAdjacentText", 2,
           [this, adjacent_place](context & c, std::span<value> args) {
               const node_id self = receiver(c);
               if (!self) { return value::undefined(); }
               const std::string text = arg_string(c, args, 1);
               const std::optional<std::pair<node_id, node_id>> place =
                   adjacent_place(c, self, arg_string(c, args, 0));
               if (!place) { return value::undefined(); }
               (void)insert_node(place->first, doc_->create_text(text), place->second);
               return value::undefined();
           });

    // `ChildNode.remove()`: detaches this node from its parent, if it has one.
    method(child_node, "remove", 0, [this](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (self) {
            // The document element of a created (secondary) document may be
            // removed (see the top of document/as_node.cpp), but
            // `remove_child` refuses the root, so it goes through
            // remove_document_element.
            if (self == doc_->root() && secondary_) {
                doc_->remove_document_element();
            } else {
                (void)doc_->remove_child(self);
            }
            mutated();
        }
        return value::undefined();
    });
    method(node, "insertBefore", 2, [this, insertable](context & c, std::span<value> args) {
        // Two required arguments: `insertBefore(node)` is a TypeError. A null
        // second argument is different: it means "at the end", so
        // `insertBefore(node, null)` appends; insert_node reads an empty handle
        // the same way.
        if (args.size() < 2) {
            c.throw_error("TypeError", "insertBefore needs a node and a reference child");
            return value::undefined();
        }
        const node_id parent = receiver(c);
        const node_id child = insertable(c, arg(args, 0));
        const node_id before = handle_of(arg(args, 1));
        if (!pre_insert_valid(c, parent, child, arg(args, 0), arg(args, 1))) {
            return value::undefined();
        }
        (void)insert_node(parent, child, before);
        return arg(args, 0);
    });
    // `moveBefore` is `insertBefore` without removing first. The DOM says a move
    // preserves state an insertion would destroy (an <iframe>'s document, a
    // playing <video>, a focused control, a running animation); this engine
    // keeps none of that state, so what is left is the insertion, with
    // `moving_` set while it runs. The observable difference here is the
    // validity checks, which are stricter than insertBefore's: the node must
    // already have a parent, and both nodes must be in the same tree.
    //
    // A separate function rather than an alias, because `moveBefore ===
    // insertBefore` would be observably wrong, and state preservation would be
    // added here.
    method(parent_node, "moveBefore", 2, [this](context & c, std::span<value> args) {
        if (args.size() < 2) {
            c.throw_error("TypeError", "moveBefore needs a node and a reference child");
            return value::undefined();
        }
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        // Another document's node is a node in another tree, so it gets the
        // root check's error; a move never adopts.
        if (!child && owner_of(arg(args, 0)) != nullptr) {
            throw_dom_exception(c, "HierarchyRequestError",
                                "moveBefore: the node belongs to another document");
            return value::undefined();
        }
        if (!pre_insert_valid(c, parent, child, arg(args, 0), arg(args, 1))) {
            return value::undefined();
        }
        {
            const auto txn = doc_->read();
            // "If parent's shadow-including root is not node's shadow-including
            // root, throw a HierarchyRequestError": a move stays within one
            // tree, which is what lets it keep state a remove-and-insert would
            // drop. A connected parent and a detached node have different
            // roots, and so do two detached subtrees.
            if (root_of_tree(txn, parent, true) != root_of_tree(txn, child, true)) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "moveBefore: the node and the parent are in different trees");
                return value::undefined();
            }
            // "If node's parent is null, then throw a HierarchyRequestError":
            // a move has to move something from somewhere.
            if (!txn.parent(child)) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "moveBefore: the node being moved has no parent");
                return value::undefined();
            }
        }
        moving_ = true;
        (void)insert_node(parent, child, handle_of(arg(args, 1)));
        moving_ = false;
        // `undefined`, unlike insertBefore: the IDL return type is void.
        return value::undefined();
    });
    // `querySelector`/`querySelectorAll` on an element or fragment search its own
    // subtree. Text that is not a selector is a SyntaxError, as on the document;
    // a valid selector this engine does not support matches nothing (null).
    // Both require one argument; needs_selector throws the arity TypeError.
    const auto needs_selector = [](context & c, std::span<value> args, const char * who) {
        if (!args.empty()) { return true; }
        c.throw_error("TypeError", std::string{who} + ": 1 argument required, but only 0 present");
        return false;
    };
    method(parent_node, "querySelector", 1,
           [this, needs_selector](context & c, std::span<value> args) {
               if (!needs_selector(c, args, "querySelector")) { return value::undefined(); }
               bool invalid = false;
               const std::string selector = arg_string(c, args, 0);
               const std::vector<node_id> found = query(selector, receiver(c), &invalid, true);
               if (invalid) {
                   throw_dom_exception(c, "SyntaxError",
                                       "'" + selector + "' is not a valid selector");
                   return value::undefined();
               }
               return found.empty() ? value::null() : wrap(c, found.front());
           });
    method(parent_node, "querySelectorAll", 1,
           [this, needs_selector](context & c, std::span<value> args) {
               if (!needs_selector(c, args, "querySelectorAll")) { return value::undefined(); }
               bool invalid = false;
               const std::string selector = arg_string(c, args, 0);
               const std::vector<node_id> found = query(selector, receiver(c), &invalid);
               if (invalid) {
                   throw_dom_exception(c, "SyntaxError",
                                       "'" + selector + "' is not a valid selector");
                   return value::undefined();
               }
               // A static NodeList: the members are fixed at the call, and
               // `instanceof NodeList` holds.
               return make_live_collection(c, [found] { return found; }, "NodeList");
           });
    // `element.getElementsByTagName(tag)`: a live collection of descendants that
    // uses the document's two-branch rule (DOM 4.5 "list of elements with
    // qualified name"): in an HTML document an HTML-namespace element matches
    // the name ASCII-lowercased and any other element matches it exactly, so
    // `<linearGradient>` inside an <svg> is found by its own spelling and not by
    // the folded one. In an XML document every element matches exactly.
    method(element, "getElementsByTagName", 1, [this](context & c, std::span<value> args) {
        const node_id from = receiver(c);
        const std::string want = arg_string(c, args, 0);
        return make_live_collection(c, [this, from, want] {
            std::vector<node_id> found;
            if (!from) { return found; }
            const auto txn = doc_->read();
            const bool every = want == "*";
            const atom folded = every ? atom{} : atoms_->intern_lower(want);
            const auto walk = [&](auto && self, node_id at, bool include) -> void {
                if (const auto tagged = txn.tag(at); include && tagged.has_value()) {
                    const bool folds = txn.element_ns(at) == node_ns::html && !doc_->xml();
                    if (every || (folds ? *tagged == folded : atoms_->text(*tagged) == want)) {
                        found.push_back(at);
                    }
                }
                for (const node_id child : txn.children(at)) { self(self, child, true); }
            };
            // Descendants only: the element itself is not one of its own results.
            walk(walk, from, false);
            return found;
        });
    });
    // `element.getElementsByTagNameNS(namespace, localName)`, matching as the
    // document's does: "*" means any on either half, and, as with
    // getElementsByTagName, the element is not one of its own results.
    method(element, "getElementsByTagNameNS", 2, [this](context & c, std::span<value> args) {
        const node_id from = receiver(c);
        const std::string ns = namespace_argument(c, args, 0);
        const std::string local = arg_string(c, args, 1);
        return make_live_collection(c, [this, from, ns, local] {
            std::vector<node_id> found;
            if (!from) { return found; }
            const auto txn = doc_->read();
            const auto walk = [&](auto && self, node_id at, bool include) -> void {
                if (include && txn.tag(at).has_value()) {
                    // The local name, not the qualified one: a prefix takes no
                    // part in this match any more than it does in getAttributeNS.
                    const std::string_view name = atoms_->text(txn.tag(at).value_or(atom{}));
                    const std::size_t colon = name.find(':');
                    const std::string_view own =
                        colon == std::string_view::npos ? name : name.substr(colon + 1);
                    if ((local == "*" || own == local) && (ns == "*" || namespace_of(at) == ns)) {
                        found.push_back(at);
                    }
                }
                for (const node_id child : txn.children(at)) { self(self, child, true); }
            };
            walk(walk, from, false);
            return found;
        });
    });
    // `element.getElementsByClassName(names)`, scoped to this subtree and live
    // for the same reason the document's is - see make_live_collection. The
    // element is not one of its own results.
    method(element, "getElementsByClassName", 1, [this](context & c, std::span<value> args) {
        const node_id from = receiver(c);
        const std::vector<std::string> tokens = parse_ordered_tokens(arg_string(c, args, 0));
        return make_live_collection(c, [this, from, tokens] {
            return from ? all_by_class(from, tokens) : std::vector<node_id>{};
        });
    });
    // The ParentNode and ChildNode insertion methods (append, prepend,
    // replaceChildren, before, after, replaceWith). Each takes any number of
    // arguments, turns strings into Text nodes, and follows the specification's
    // shape: "convert nodes into a node" first, then one insertion. Converting
    // first moves the arguments out of the tree before the position is chosen,
    // and the single insertion runs the pre-insertion checks once, so
    // `body.append(body)` is a HierarchyRequestError rather than a cycle.
    const auto parent_of = [this](node_id id) { return doc_->read().parent(id); };
    method(parent_node, "append", 0, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id node = convert_nodes(c, args);
        if (!pre_insert_valid(c, self, node, value::null(), value::null())) {
            return value::undefined();
        }
        (void)insert_node(self, node, node_id{});
        return value::undefined();
    });
    method(parent_node, "prepend", 0, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id node = convert_nodes(c, args);
        if (!pre_insert_valid(c, self, node, value::null(), value::null())) {
            return value::undefined();
        }
        // Before the first child, read after the conversion: an argument that was
        // that first child has been moved into the fragment by now.
        node_id first;
        {
            const auto txn = doc_->read();
            const auto children = txn.children(self);
            if (!children.empty()) { first = children.front(); }
        }
        (void)insert_node(self, node, first);
        return value::undefined();
    });
    // `replaceChildren(...nodes)`: removes every child, then inserts the
    // converted arguments. The Document's own copy is in
    // bindings/document/as_node.cpp.
    method(parent_node, "replaceChildren", 0, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id node = convert_nodes(c, args);
        if (!pre_insert_valid(c, self, node, value::null(), value::null())) {
            return value::undefined();
        }
        // Copied before removing: children() is a view onto the live child list
        // and each removal republishes it.
        std::vector<node_id> existing;
        std::vector<node_id> arriving;
        {
            const auto txn = doc_->read();
            for (const node_id child : txn.children(self)) { existing.push_back(child); }
            if (txn.kind(node).value_or(node_kind::element) == node_kind::document_fragment) {
                for (const node_id child : txn.children(node)) { arriving.push_back(child); }
            } else {
                arriving.push_back(node);
            }
        }
        // One mutation record naming every old child and every new one ("replace
        // all"), which the diff after the fact could not tell from a move.
        replace_all_ = replace_all_note{self, existing, arriving};
        for (const node_id child : existing) { (void)doc_->remove_child(child); }
        (void)insert_node(self, node, node_id{});
        return value::undefined();
    });
    // Shadow DOM. `attachShadow` is on Element.prototype; attach_shadow refuses
    // a receiver that is not an element, which keeps
    // `Element.prototype.attachShadow.call(fragment)` from building a second
    // tree under a fragment.
    method(element, "attachShadow", 1, [this](context & c, std::span<value> args) {
        return attach_shadow(c, receiver(c), args);
    });
    // `getRootNode(options)`, DOM 4.4, on every node.
    method(node, "getRootNode", 0, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        // `{composed: true}` keeps going through each shadow host; the default
        // stops at the ShadowRoot.
        bool composed = false;
        if (const value options = arg(args, 0); options.is_object()) {
            composed = context::truthy(c.lookup_property(options, "composed"));
        }
        const auto txn = doc_->read();
        const node_id top = root_of_tree(txn, self, composed);
        // The document is not a wrapper: `document` is one object built by
        // install_document, and `node.getRootNode() === document` must hold,
        // so a connected node answers that object rather than a wrapper.
        if (is_document_root(txn, top)) { return document_; }
        return wrap(c, top);
    });
    // ChildNode's before, after and replaceWith (DOM 4.2.7). Each finds the
    // viable sibling (one that is not among the arguments) first, then converts,
    // then inserts once, so `el.after(el.nextSibling, x)` still places the
    // arguments after `el` in argument order.
    method(child_node, "before", 0, [this, parent_of](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id parent = parent_of(self);
        if (!parent) { return value::undefined(); }
        const node_id previous = viable_sibling(self, args, false);
        const node_id node = convert_nodes(c, args);
        // "If viablePreviousSibling is null, set it to parent's first child;
        // otherwise to viablePreviousSibling's next sibling", read after the
        // conversion, which may have moved the old first child away.
        node_id reference;
        {
            const auto txn = doc_->read();
            const std::span<const node_id> kids = txn.children(parent);
            if (!previous) {
                reference = kids.empty() ? node_id{} : kids.front();
            } else {
                for (std::size_t i = 0; i + 1 < kids.size(); ++i) {
                    if (kids[i] == previous) { reference = kids[i + 1]; }
                }
            }
        }
        (void)insert_node(parent, node, reference);
        return value::undefined();
    });
    method(child_node, "after", 0, [this, parent_of](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id parent = parent_of(self);
        if (!parent) { return value::undefined(); }
        const node_id next = viable_sibling(self, args, true);
        const node_id node = convert_nodes(c, args);
        (void)insert_node(parent, node, next);
        return value::undefined();
    });
    method(child_node, "replaceWith", 0, [this, parent_of](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id parent = parent_of(self);
        if (!parent) { return value::undefined(); }
        const node_id next = viable_sibling(self, args, true);
        const node_id node = convert_nodes(c, args);
        // "If this's parent is parent, replace this with node". It is not
        // when this was among the arguments, because the conversion moved it
        // into the fragment; then node goes before the viable sibling instead.
        if (parent_of(self) == parent) {
            // `this.replaceWith(this)` is a replacement with itself, which
            // "replace" defines as leaving the node where it is.
            if (node != self) {
                (void)insert_node(parent, node, self);
                (void)doc_->remove_child(self);
            }
        } else {
            (void)insert_node(parent, node, next);
        }
        mutated();
        return value::undefined();
    });
    // `replaceChild(node, child)`, DOM 4.2.3 "replace": the pre-insertion
    // checks with `child` as the reference (so a `child` that is not this
    // node's is a NotFoundError), and then the swap. Replacing a node with
    // itself leaves it where it is.
    method(node, "replaceChild", 2, [this, insertable](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const value node_arg = arg(args, 0);
        const value child_arg = arg(args, 1);
        const node_id fresh = insertable(c, node_arg);
        const node_id stale = handle_of(child_arg);
        // Both arguments are `Node`, not `Node?`: null is a TypeError for either.
        // Another document's node is a Node; pre_insert_valid says what is
        // wrong with it, in the specification's order.
        if ((!fresh && !is_a_document(node_arg)) ||
            (!stale && !is_a_document(child_arg) && owner_of(child_arg) == nullptr)) {
            c.throw_error("TypeError", "replaceChild: the argument is not a Node");
            return value::undefined();
        }
        if (!pre_insert_valid(c, parent, fresh, node_arg, child_arg)) { return value::undefined(); }
        // DOM 4.2.3 "replace", in the order its records come out: a node that
        // is already a child of the parent is removed first, with a record of
        // its own (step 8), `replaceChild(x, x)` included, which goes out and
        // comes back in the same place; then the child is removed and the node
        // inserted where it was, one record naming both (step 13).
        const auto next_sibling = [this](node_id of) {
            const auto txn = doc_->read();
            const std::span<const node_id> kids = txn.children(txn.parent(of));
            const auto here = std::ranges::find(kids, of);
            return here == kids.end() || here + 1 == kids.end() ? node_id{} : *(here + 1);
        };
        if (fresh == stale) {
            const node_id next = next_sibling(stale);
            (void)doc_->remove_child(stale);
            mutated();
            (void)insert_node(parent, stale, next);
            return child_arg;
        }
        if (doc_->read().parent(fresh) == parent) {
            (void)doc_->remove_child(fresh);
            mutated();
        }
        const node_id next = next_sibling(stale);
        (void)doc_->remove_child(stale);
        (void)insert_node(parent, fresh, next);
        return child_arg;
    });
    // `getElementById` on a fragment (DOM 4.2.6 NonElementParentNode), on
    // DocumentFragment.prototype and so on a ShadowRoot and a <template>'s
    // contents. An id inside a shadow tree is scoped to that tree and
    // `document.getElementById` must not find it; on a template's contents
    // this is the only way to reach a node by id at all.
    method({"DocumentFragment"}, "getElementById", 1, [this](context & c, std::span<value> args) {
        const node_id root = receiver(c);
        const std::string want = arg_string(c, args, 0);
        if (!root || want.empty()) { return value::null(); }
        const auto txn = doc_->read();
        const atom id_name = atoms_->intern("id");
        node_id found{};
        const auto walk = [&](auto && self, node_id at) -> void {
            for (const node_id child : txn.children(at)) {
                if (found) { return; }
                if (txn.attribute_value(child, id_name) == want) {
                    found = child;
                    return;
                }
                self(self, child);
            }
        };
        walk(walk, root);
        return found ? wrap(c, found) : value::null();
    });
    // `cloneNode(deep)`: a detached copy of this node, and of its subtree when
    // `deep` is true.
    method(node, "cloneNode", 0, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) {
            // An Attr is a Node with no handle - see attribute_object.
            if (attribute_of_object(c, c.current_this()).name) {
                return clone_attr_object(c, c.current_this());
            }
            return value::null();
        }
        if (shadow_tree_of(self) != nullptr) {
            throw_dom_exception(c, "NotSupportedError",
                                "cloneNode: a shadow root cannot be cloned");
            return value::undefined();
        }
        const bool deep = !args.empty() && context::truthy(args[0]);
        node_id made;
        {
            const auto txn = doc_->read();
            made = clone_node(txn, self, deep);
        }
        // [CEReactions]: DOM 4.4's clone runs "create an element" for every
        // element of the copy, which enqueues an upgrade for each candidate a
        // definition covers, even though the copy is detached.
        upgrade_created_subtree(made);
        return wrap(c, made);
    });
    // `lookupNamespaceURI`, `lookupPrefix` and `isDefaultNamespace` (DOM 4.4) on
    // every node, plus normalize and contains (namespace_methods.cpp). Each
    // lookup runs the element algorithm on the element this node names (an
    // element itself, a Text or Comment's parent element, nothing for a
    // DocumentFragment). The algorithms are shared with the document's copies in
    // bindings/document/as_node.cpp.
    install_node_namespaces(cx);
    method(node, "appendChild", 1, [this, insertable](context & c, std::span<value> args) {
        // Through insert_node, which is where a DocumentFragment is flattened:
        // appending one must move its children and leave the fragment behind.
        const node_id parent = receiver(c);
        const node_id child = insertable(c, arg(args, 0));
        if (!pre_insert_valid(c, parent, child, arg(args, 0), value::null())) {
            return value::undefined();
        }
        (void)insert_node(parent, child, node_id{});
        return arg(args, 0);
    });
    // "If child's parent is not this, then throw a NotFoundError" (DOM 4.2.3).
    // Without the check the node would be removed from wherever it actually is.
    method(node, "removeChild", 1, [this](context & c, std::span<value> args) {
        const node_id child = handle_of(arg(args, 0));
        const node_id parent = receiver(c);
        if (!child) {
            // The document object is a Node with no handle (see
            // pre_insert_valid) and it is nobody's child, so `s.removeChild
            // (document)` is the NotFoundError below rather than a TypeError.
            // So is a node of another document, such as a frame's.
            if (is_a_document(arg(args, 0)) || owner_of(arg(args, 0)) != nullptr) {
                throw_dom_exception(c, "NotFoundError",
                                    "removeChild: the node is not a child of this one");
                return value::undefined();
            }
            c.throw_error("TypeError", "removeChild: the argument is not a Node");
            return value::undefined();
        }
        {
            const auto txn = doc_->read();
            if (txn.parent(child) != parent) {
                throw_dom_exception(c, "NotFoundError",
                                    "removeChild: the node is not a child of this one");
                return value::undefined();
            }
        }
        (void)doc_->remove_child(child);
        mutated();
        return arg(args, 0);
    });
    // `matches` and `closest` test one element with
    // style::engine::element_matches, which builds that element's ancestor chain
    // and runs the same matcher `select` uses. They do not search the document:
    // a detached element is in no document, yet
    // `document.createElement('div').matches('div')` must be true.
    //
    // The selector is parsed per call, as in `query`: a compiled_selector owns
    // everything it holds, and a selector string is a handful of tokens.
    const auto compiled = [this](context & c, std::span<value> args, bool & bad) {
        return style::css::parse_selector_text(arg_string(c, args, 0), *atoms_, bad);
    };
    // `webkitMatchesSelector` is `matches` under its legacy name; DOM 4.9
    // defines it as an alias.
    const script::native_fn matches = [this, compiled, needs_selector](context & c,
                                                                       std::span<value> args) {
        if (!needs_selector(c, args, "matches")) { return value::boolean(false); }
        const node_id self = receiver(c);
        bool bad = false;
        const style::css::stylesheet parsed = compiled(c, args, bad);
        // "If s is not a valid selector, throw a SyntaxError" (DOM 4.9), the
        // same refusal querySelector makes. A selector that is valid CSS this
        // engine cannot answer, such as `:has(.x)`, is not invalid and matches
        // nothing.
        if (bad) {
            throw_dom_exception(c, "SyntaxError",
                                "matches: '" + arg_string(c, args, 0) +
                                    "' is not a valid selector");
            return value::boolean(false);
        }
        if (!self || parsed.selectors.empty()) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(selector_engine().element_matches(txn, self, parsed.selectors));
    };
    method(element, "matches", 1, matches);
    method(element, "webkitMatchesSelector", 1, matches);
    method(element, "closest", 1,
           [this, compiled, needs_selector](context & c, std::span<value> args) {
               if (!needs_selector(c, args, "closest")) { return value::null(); }
               const node_id self = receiver(c);
               bool bad = false;
               const style::css::stylesheet parsed = compiled(c, args, bad);
               if (bad) {
                   throw_dom_exception(c, "SyntaxError",
                                       "closest: '" + arg_string(c, args, 0) +
                                           "' is not a valid selector");
                   return value::null();
               }
               if (!self || parsed.selectors.empty()) { return value::null(); }
               const auto txn = doc_->read();
               return wrap(c, selector_engine().closest(txn, self, parsed.selectors));
           });

    // `isEqualNode` and `isSameNode` on Node.prototype. The Document has its own
    // pair as own properties (bindings/document/as_node.cpp), which shadow
    // these.
    method(node, "isEqualNode", 1, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        // A null argument is not an error and is not equal. The IDL is `Node?`,
        // so `isEqualNode(null)` is a question with the answer false rather
        // than a TypeError.
        node_id other = handle_of(arg(args, 0));
        if (!self) { return value::boolean(false); }
        // Another document's node (e.g. `doc1.doctype.isEqualNode(
        // doc2.doctype)`) is compared the way the document's own isEqualNode
        // compares two documents: cloned deep into this document's slab and
        // left detached for collect().
        if (!other) {
            dom_bindings * theirs = owner_of(arg(args, 0));
            if (theirs == nullptr || theirs == this) { return value::boolean(false); }
            const auto from = theirs->doc_->read();
            other = clone_node(from, theirs->handle_of(arg(args, 0)), true, theirs);
        }
        if (!other) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(nodes_are_equal(txn, self, other));
    });
    method(node, "isSameNode", 1, [this](context & c, std::span<value> args) {
        // Identity and nothing else: this is `===` as a method.
        const node_id self = receiver(c);
        const node_id other = handle_of(arg(args, 0));
        // An Attr has no handle and is itself and nothing else.
        if (!self) {
            const value given = arg(args, 0);
            return value::boolean(given.is_object() && given.bits() == c.current_this().bits());
        }
        return value::boolean(self && other && self == other);
    });
    method(node, "hasChildNodes", 0, [this](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return value::boolean(self && !doc_->read().children(self).empty());
    });
    // `compareDocumentPosition(other)`, DOM 4.4: the position bitmask. The
    // document has its own (as_node.cpp) because it is not a wrapper. Two nodes
    // in different trees are DISCONNECTED and IMPLEMENTATION_SPECIFIC, with a
    // direction that only has to be consistent: the packed handle orders them.
    method(node, "compareDocumentPosition", 1, [this](context & c, std::span<value> args) {
        constexpr unsigned disconnected = 0x01, preceding = 0x02, following = 0x04, contains = 0x08,
                           contained_by = 0x10, implementation_specific = 0x20;
        const node_id self = receiver(c);
        const value given = arg(args, 0);
        const node_id other = handle_of(given);
        const auto txn = doc_->read();
        if (!other) {
            // The document object: it contains everything connected and is
            // disconnected from the rest, and either way it comes first.
            if (is_the_document(given) && self) {
                const bool connected = is_document_root(txn, root_of_tree(txn, self, false));
                return value::number(connected
                                         ? (contains | preceding)
                                         : (disconnected | implementation_specific | preceding));
            }
            if (const unsigned foreign = foreign_document_position(given); foreign != 0) {
                return value::number(foreign);
            }
            c.throw_error("TypeError", "compareDocumentPosition: the argument is not a Node");
            return value::undefined();
        }
        if (!self || self == other) { return value::number(0); }
        // The tree as the DOM sees it: the walk goes through `dom_parent`, so
        // the document element and a doctype beside it share the Document
        // node as their root rather than being two trees.
        const auto chain = [&txn](node_id from) {
            std::vector<node_id> up;
            for (node_id at = from; at; at = dom_parent(txn, at)) { up.push_back(at); }
            return up;
        };
        const std::vector<node_id> mine = chain(self);
        const std::vector<node_id> theirs = chain(other);
        if (mine.back() != theirs.back()) {
            return value::number(disconnected | implementation_specific |
                                 (other.key() < self.key() ? preceding : following));
        }
        if (std::ranges::find(mine, other) != mine.end()) {
            return value::number(contains | preceding);
        }
        if (std::ranges::find(theirs, self) != theirs.end()) {
            return value::number(contained_by | following);
        }
        // Neither contains the other: compare the two children of the deepest
        // shared ancestor by their order in it.
        std::size_t i = mine.size();
        std::size_t j = theirs.size();
        while (i > 1 && j > 1 && mine[i - 2] == theirs[j - 2]) {
            --i;
            --j;
        }
        const std::span<const node_id> kids = txn.children(mine[i - 1]);
        for (const node_id kid : kids) {
            if (kid == mine[i - 2]) { return value::number(following); }
            if (kid == theirs[j - 2]) { return value::number(preceding); }
        }
        return value::number(disconnected | implementation_specific | following);
    });

    // The Node constants, on Node.prototype and on the Node interface object,
    // which is where WebIDL puts a constant: `Node.ELEMENT_NODE` and
    // `element.ELEMENT_NODE` are both 1. Enumerable, non-writable,
    // non-configurable, as a constant is. Defined once per realm.
    if (secondary_) { return; }
    for (const auto & [name, bits] : node_constants) {
        if (auto * proto = prototype_object(interface_prototype("Node"))) {
            proto->define(name, value::number(bits), script::attr_enumerable);
        }
        if (const value ctor = cx.global("Node"); ctor.is_kind(script::heap_kind::native)) {
            static_cast<script::native_object *>(ctor.as_heap())
                ->define(name, value::number(bits), script::attr_enumerable);
        }
    }
    // The [Unscopable] members, DOM 4.2.6 and 4.2.8: ParentNode's prepend,
    // append and replaceChildren, ChildNode's before, after, replaceWith and
    // remove. Each interface's prototype carries a null-prototype
    // @@unscopables object naming the ones it has (WebIDL 3.7.9). It matters for
    // event handler attributes: `onclick="remove()"` runs with the element in
    // its scope chain (compile_handler_attribute), and `remove` there must be
    // the page's global, not the element's method.
    const auto unscopable = [&](std::initializer_list<const char *> on,
                                std::initializer_list<const char *> names) {
        for (const char * which : on) {
            auto * proto = prototype_object(interface_prototype(which));
            if (proto == nullptr) { continue; }
            const value made = cx.make_object();
            auto * blocked = static_cast<script::object_object *>(made.as_heap());
            blocked->prototype = value::undefined(); // An explicit null (object_object::prototype).
            for (const char * name : names) { blocked->set(name, value::boolean(true)); }
            proto->define(CTBROWSER_SYMBOL_KEY("unscopables"), made, script::attr_configurable);
        }
    };
    unscopable(element, {"before", "after", "replaceWith", "remove", "prepend", "append",
                         "replaceChildren"});
    unscopable({"Document", "DocumentFragment"}, {"prepend", "append", "replaceChildren"});
    unscopable({"CharacterData", "DocumentType"}, {"before", "after", "replaceWith", "remove"});
}

} // namespace ctbrowser::shell
