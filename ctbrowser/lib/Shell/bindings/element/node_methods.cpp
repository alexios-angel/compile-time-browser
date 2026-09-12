// dom_bindings - the Node, ParentNode and ChildNode method surface: insertion
// validity, insertAdjacent*, tree mutation, selectors and geometry.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// DOM 4.2.3, "ensure pre-insertion validity". Every one of these checks stands
// in front of a `assert_throws_dom` in `dom/nodes/Node-insertBefore.html`,
// `Node-appendChild.html` and `Node-removeChild.html`, and the FIRST one is the
// reason this is not merely conformance work: appending a node to its own
// descendant built a cycle, and every tree walk in this file has a depth cap
// precisely because nothing stopped one being made.
bool dom_bindings::pre_insert_valid(context & cx, node_id parent, node_id child, value node_arg,
                                    value ref_arg) {
    // THE DOCUMENT OBJECT IS A NODE WITHOUT A HANDLE - `document` is one object
    // built by install_document, not a wrapper - so `handle_of` answers nothing
    // for it. It IS a Node, and inserting one is step 4's HierarchyRequestError,
    // not a TypeError: `el.insertBefore(document, a)` is a subtest by name.
    // ANY document - a frame's or a created one is a Node just the same.
    const bool node_is_document = !child && is_a_document(node_arg);
    if (!child && !node_is_document) {
        // Not a Node at all. WebIDL reports a failed conversion as a TypeError
        // rather than a DOMException, which is the one case in these steps that
        // is not a DOMException.
        cx.throw_error("TypeError", "the argument is not a Node");
        return false;
    }
    if (!parent) {
        cx.throw_error("TypeError", "the receiver is not a Node");
        return false;
    }
    const auto txn = doc_->read();
    // 1. "If parent is not a Document, DocumentFragment, or Element node, throw
    //    a HierarchyRequestError." A text node has no children to insert into.
    const node_kind parent_kind = txn.kind(parent).value_or(node_kind::element);
    if (parent_kind != node_kind::document && parent_kind != node_kind::document_fragment &&
        parent_kind != node_kind::element) {
        throw_dom_exception(cx, "HierarchyRequestError", "the parent cannot have children");
        return false;
    }
    // 2. "If node is a host-including inclusive ancestor of parent, throw a
    //    HierarchyRequestError." The cycle case, and the one with teeth.
    for (node_id at = parent; at; at = txn.parent(at)) {
        if (at != child) { continue; }
        throw_dom_exception(cx, "HierarchyRequestError",
                            "the node is an ancestor of the parent it would go into");
        return false;
    }
    // 3. "If child is non-null and its parent is not parent, throw a
    //    NotFoundError."
    if (!ref_arg.is_nullish()) {
        const node_id before = handle_of(ref_arg);
        if (!before) {
            cx.throw_error("TypeError", "the reference node is not a Node");
            return false;
        }
        if (txn.parent(before) != parent) {
            throw_dom_exception(cx, "NotFoundError",
                                "the reference node is not a child of the parent");
            return false;
        }
    }
    // 4. "If node is not a DocumentFragment, DocumentType, Element, or
    //    CharacterData node, throw a HierarchyRequestError." A Document is the
    //    one this engine can produce and must refuse.
    if (node_is_document || txn.kind(child).value_or(node_kind::element) == node_kind::document) {
        throw_dom_exception(cx, "HierarchyRequestError", "a Document cannot be inserted");
        return false;
    }
    return true;
}

// "CONVERT NODES INTO A NODE", DOM 4.2.5: one argument is the node it names,
// several become a DocumentFragment holding them all - which MOVES each out of
// wherever it was - and a string becomes a Text node. Every ParentNode and
// ChildNode insertion method starts here, and the conversion running BEFORE
// the position is computed is what makes `child.after(x, child)` land where the
// specification says.
node_id dom_bindings::convert_nodes(context & cx, std::span<value> args) {
    if (args.size() == 1) { return node_from(cx, args[0]); }
    const node_id fragment = doc_->create_fragment();
    for (const value & one : args) { (void)insert_node(fragment, node_from(cx, one), node_id{}); }
    return fragment;
}

// "VIABLE NEXT/PREVIOUS SIBLING", DOM 4.2.7: the first sibling in that direction
// that is NOT one of the arguments. `child.after(x, y)` where x and y are the
// very siblings that follow it must put them after child, not after
// themselves - which is what the immediate sibling would have said.
node_id dom_bindings::viable_sibling(node_id self, std::span<value> args, bool forward) {
    std::vector<node_id> given;
    for (const value & one : args) {
        if (const node_id held = handle_of(one)) { given.push_back(held); }
    }
    const auto txn = doc_->read();
    const node_id parent = txn.parent(self);
    if (!parent) { return node_id{}; }
    const std::span<const node_id> kids = txn.children(parent);
    std::size_t at = 0;
    while (at < kids.size() && kids[at] != self) { ++at; }
    if (at >= kids.size()) { return node_id{}; }
    const auto excluded = [&](node_id one) { return std::ranges::find(given, one) != given.end(); };
    if (forward) {
        for (std::size_t i = at + 1; i < kids.size(); ++i) {
            if (!excluded(kids[i])) { return kids[i]; }
        }
        return node_id{};
    }
    for (std::size_t i = at; i > 0; --i) {
        if (!excluded(kids[i - 1])) { return kids[i - 1]; }
    }
    return node_id{};
}

// WHICH PROTOTYPE EACH OPERATION GOES ON is WebIDL's answer, not this file's:
// Node's on Node.prototype; the ParentNode mixin on Element, Document and
// DocumentFragment; the ChildNode mixin on Element and CharacterData; the rest
// on Element. A method that is not on the interface is then simply absent -
// `"moveBefore" in textNode` is false without anything being erased.
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

    // "INSERT ADJACENT", DOM 4.9 - ONE ALGORITHM FOR THREE METHODS, which is
    // the point of it. `insertAdjacentHTML` did this by hand and the other two
    // did not exist, and doing it by hand got `afterend` wrong: it APPENDED to
    // the parent rather than placing the node after this element, so a
    // `beforebegin` and an `afterend` on the same element landed in the wrong
    // order whenever the element had a later sibling.
    //
    // Answers (parent, before), or nothing. There are two kinds of nothing and
    // the caller does not have to tell them apart: an unrecognised position is a
    // SyntaxError and `beforebegin`/`afterend` on the DOCUMENT ELEMENT is a
    // HierarchyRequestError, both already thrown by the time this returns; an
    // element with no parent at all is the "return null" the specification
    // gives, and throws nothing.
    //
    // THE DOCUMENT ELEMENT'S PARENT IS THE DOCUMENT in the DOM and is EMPTY
    // here - this tree builder has no Document node, see install_document_as_node
    // - so the one place the two models differ has to be named rather than
    // inferred. A second element or a text node beside `<html>` would be a
    // second child of the Document, which is what pre-insertion refuses.
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
        if (where == "beforeend") { return std::pair{self, node_id{}}; }
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
    // `insertAdjacentHTML(position, markup)` - a fragment parse at one of four
    // places relative to this element. The parser and the copy are the same
    // ones innerHTML uses; only where the nodes land differs.
    method(element, "insertAdjacentHTML", 2,
           [this, adjacent_place](context & c, std::span<value> args) {
               const node_id self = receiver(c);
               if (!self || atoms_ == nullptr) { return value::undefined(); }
               const std::string where = arg_string(c, args, 0);
               const std::string markup = arg_string(c, args, 1);
               const std::optional<std::pair<node_id, node_id>> place =
                   adjacent_place(c, self, where);
               if (!place) { return value::undefined(); }

               // Parsed into a scratch document, as innerHTML does and for the same
               // reason: tree_builder::parse replaces the root it is handed.
               document scratch{*atoms_};
               (void)parse_html(scratch, markup);
               const auto from = scratch.read();
               node_id body{};
               const auto find_body = [&](auto && walk, node_id at) -> void {
                   if (!body && from.tag(at).value_or(atom{}) == atoms_->intern_lower("body")) {
                       body = at;
                   }
                   for (const node_id child : from.children(at)) { walk(walk, child); }
               };
               find_body(find_body, from.root());
               if (!body) { return value::undefined(); }

               // IN ORDER, because each node goes before the SAME reference rather
               // than before the one just added. copy_subtree appends to the parent it
               // is given, so the move is a no-op when the reference is empty.
               for (const node_id child : from.children(body)) {
                   const node_id made = copy_subtree(from, child, place->first);
                   if (place->second) {
                       (void)doc_->insert_before(place->first, made, place->second);
                   }
               }
               mutated();
               return value::undefined();
           });
    // ...and the two spellings that take a NODE rather than markup, both of
    // which were missing. `insertAdjacentElement` ANSWERS with the element it
    // inserted - or null, which is how a page learns the position was one the
    // element has no room for.
    method(element, "insertAdjacentElement", 2,
           [this, adjacent_place](context & c, std::span<value> args) {
               const node_id self = receiver(c);
               if (!self) { return value::null(); }
               const std::optional<std::pair<node_id, node_id>> place =
                   adjacent_place(c, self, arg_string(c, args, 0));
               if (!place) { return value::null(); }
               const node_id child = handle_of(arg(args, 1));
               // AN ELEMENT, by the IDL: a doctype or a text node is a TypeError
               // before any hierarchy question is asked - insert-adjacent.html
               // hands it a DocumentType by name.
               if (child &&
                   doc_->read().kind(child).value_or(node_kind::element) != node_kind::element) {
                   c.throw_error("TypeError",
                                 "insertAdjacentElement: the argument is not an Element");
                   return value::null();
               }
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

    // TREE NAVIGATION. appendChild and removeChild could already change the
    // tree; nothing could WALK it, so an element could not reach its own
    // parent. `this.elt.parentNode.removeChild(this.elt)` is the ordinary way
    // to take an element out of the page - it is how p5.js discards the default
    // canvas when a sketch calls createCanvas - and with parentNode undefined
    // the removal threw inside a callback and the discarded canvas stayed in
    // the document, laid out and painted, underneath the real one.
    method(child_node, "remove", 0, [this](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (self) {
            (void)doc_->remove_child(self);
            mutated();
        }
        return value::undefined();
    });
    method(node, "insertBefore", 2, [this](context & c, std::span<value> args) {
        // TWO REQUIRED ARGUMENTS: `insertBefore(node)` is a TypeError, and
        // `Node-insertBefore.html` asks for it by name. A null SECOND argument is
        // a different thing - it means "at the end", which is what makes
        // `insertBefore(node, null)` a documented spelling of appendChild;
        // insert_node reads an empty handle the same way.
        if (args.size() < 2) {
            c.throw_error("TypeError", "insertBefore needs a node and a reference child");
            return value::undefined();
        }
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        const node_id before = handle_of(arg(args, 1));
        if (!pre_insert_valid(c, parent, child, arg(args, 0), arg(args, 1))) {
            return value::undefined();
        }
        (void)insert_node(parent, child, before);
        return arg(args, 0);
    });
    // `moveBefore` IS `insertBefore` THAT DOES NOT REMOVE FIRST. The DOM says a
    // move preserves state an insertion would destroy - an <iframe>'s document,
    // a playing <video>, a focused control, a running animation - and this
    // engine has none of those, so what is left of the operation is exactly the
    // insertion. The DIFFERENCE that IS observable here is the validity checks,
    // which are stricter than insertBefore's: the node must already have a
    // parent, and both nodes must be in the same document.
    //
    // Named rather than aliased, because `moveBefore === insertBefore` would be
    // a lie a page can test for, and because when state preservation does
    // arrive it arrives here.
    method(parent_node, "moveBefore", 2, [this](context & c, std::span<value> args) {
        if (args.size() < 2) {
            c.throw_error("TypeError", "moveBefore needs a node and a reference child");
            return value::undefined();
        }
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        if (!pre_insert_valid(c, parent, child, arg(args, 0), arg(args, 1))) {
            return value::undefined();
        }
        {
            const auto txn = doc_->read();
            // "If parent's shadow-including root is not node's shadow-including
            // root, throw a HierarchyRequestError" - a move stays within one
            // tree, which is what lets it keep state a remove-and-insert would
            // drop. A connected parent and a detached node have different
            // roots, and so do two detached subtrees.
            if (root_of_tree(txn, parent, true) != root_of_tree(txn, child, true)) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "moveBefore: the node and the parent are in different trees");
                return value::undefined();
            }
            // "If node's parent is null, then throw a HierarchyRequestError" -
            // a move has to move something from somewhere.
            if (!txn.parent(child)) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "moveBefore: the node being moved has no parent");
                return value::undefined();
            }
        }
        (void)insert_node(parent, child, handle_of(arg(args, 1)));
        // `undefined`, unlike insertBefore: the IDL return type is void.
        return value::undefined();
    });
    // WHERE THE ELEMENT IS ON SCREEN. A page turns a pointer event's viewport
    // coordinates into coordinates within an element by subtracting this - p5's
    // getMouseInfo does exactly that to compute mouseX/mouseY - so without it
    // every mouse listener throws on its first event. The listeners were
    // installed and the events were dispatched; the conversion in between is
    // what was missing, and it made the whole input surface look absent.
    // `querySelector` ON AN ELEMENT, searching its own subtree. The document
    // had both and an element had neither, so the ordinary "find something
    // inside this" - which is what a library does with a container it owns -
    // threw. p5.js's describe() builds an offscreen tree and queries it.
    // Text that is not a selector at all is a SyntaxError, exactly as on the
    // document; an UNSUPPORTED selector still answers null, a missing answer
    // rather than a wrong one.
    // ONE REQUIRED ARGUMENT, and the arity TypeError is a subtest by name in
    // `ParentNode-querySelector-All.html` for an element and a fragment alike.
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
               value out = c.make_array();
               auto * items = static_cast<script::array_object *>(out.as_heap());
               for (const node_id node : found) { items->items.push_back(wrap(c, node)); }
               return out;
           });
    // `element.getElementsByTagName(tag)` - the DOCUMENT had one and an element
    // did not, so a page that scoped its search to a subtree found the method
    // missing. p5's XML module walks a parsed document with exactly this.
    //
    // LIVE, as the document's is, and with the document's TWO-BRANCH rule -
    // DOM 4.5 "list of elements with qualified name": in an HTML document an
    // HTML-namespace element matches the name ASCII-lowercased and any other
    // element matches it exactly, so `<linearGradient>` inside an <svg> is found
    // by its own spelling and not by the folded one. In an XML document every
    // element matches exactly. `Element-getElementsByTagName.html` runs the
    // document's whole battery against an element.
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
            // DESCENDANTS ONLY - the element itself is not one of its own results.
            walk(walk, from, false);
            return found;
        });
    });
    // `element.getElementsByTagNameNS(namespace, localName)`, the same battery
    // the document has answered all along. `Document-Element-getElementsBy...
    // TagNameNS.js` runs its whole table against BOTH, so half of every case in
    // it was a TypeError on a missing method rather than a comparison. "*"
    // means any on either half, and - as with getElementsByTagName - the
    // element is not one of its own results, which the file asserts by name.
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
                    // THE LOCAL NAME, not the qualified one: a prefix takes no
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
    // `element.getElementsByClassName(names)`, scoped to this subtree and LIVE
    // for the same reason the document's is - see make_live_collection. The
    // element is not one of its own results.
    method(element, "getElementsByClassName", 1, [this](context & c, std::span<value> args) {
        const node_id from = receiver(c);
        const std::vector<std::string> tokens = ordered_set(arg_string(c, args, 0));
        return make_live_collection(c, [this, from, tokens] {
            return from ? all_by_class(from, tokens) : std::vector<node_id>{};
        });
    });
    // THE ParentNode AND ChildNode INSERTION METHODS, none of which existed.
    //
    // They are what modern code writes instead of appendChild/insertBefore, and
    // they differ in two ways that matter: they take ANY NUMBER of arguments,
    // and a STRING argument becomes a Text node - so `el.append("x", node)` is
    // one call where the old spelling is three lines and a createTextNode. WPT
    // reaches for them constantly, and so does every library written since 2016.
    //
    // EVERY ONE OF THEM IS "convert nodes into a node" AND THEN ONE INSERTION,
    // which is the specification's shape and not a nicety: converting first is
    // what moves the arguments out of the tree BEFORE the position is chosen,
    // and one insertion is what runs the pre-insertion checks once - so
    // `body.append(body)` is the HierarchyRequestError it must be rather than a
    // cycle, which `ParentNode-append.html` asks for by name.
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
        // BEFORE THE FIRST CHILD, read AFTER the conversion: an argument that was
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
    // `replaceChildren` - the third of the ParentNode mixin, and the one an
    // element did not have. `document` has had it all along
    // (bindings/document.cpp); an element and a ShadowRoot are where a page
    // actually calls it, because "empty this and put those in it" is what
    // rebuilding a list IS.
    method(parent_node, "replaceChildren", 0, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id node = convert_nodes(c, args);
        if (!pre_insert_valid(c, self, node, value::null(), value::null())) {
            return value::undefined();
        }
        // COPIED BEFORE REMOVING: children() is a view onto the live child list
        // and each removal republishes it.
        std::vector<node_id> existing;
        {
            const auto txn = doc_->read();
            for (const node_id child : txn.children(self)) { existing.push_back(child); }
        }
        for (const node_id child : existing) { (void)doc_->remove_child(child); }
        (void)insert_node(self, node, node_id{});
        mutated();
        return value::undefined();
    });
    // --- shadow DOM: the two things an ELEMENT gains --------------------------
    //
    // `attachShadow` is on every wrapper rather than on Element.prototype for
    // the reason every other method in this function is: this engine's methods
    // are own properties of the wrapper. It refuses a receiver that is not an
    // element, which is what keeps `shadowRoot.attachShadow` from building a
    // second tree under a fragment.
    method(element, "attachShadow", 1, [this](context & c, std::span<value> args) {
        return attach_shadow(c, receiver(c), args);
    });
    // `getRootNode(options)`, DOM 4.4. On every node, which is what
    // `rootNode.html` asks of an element, a text node and a fragment in turn.
    method(node, "getRootNode", 0, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        // `{composed: true}` KEEPS GOING through each shadow host; the default
        // stops at the ShadowRoot, which is the whole point of the boundary.
        bool composed = false;
        if (const value options = arg(args, 0); options.is_object()) {
            composed = context::truthy(c.lookup_property(options, "composed"));
        }
        const auto txn = doc_->read();
        const node_id top = root_of_tree(txn, self, composed);
        // THE DOCUMENT IS NOT A WRAPPER. `document` is one object built by
        // install_document, and `node.getRootNode() === document` is the
        // assertion in four of `rootNode.html`'s five cases - so answering with
        // a wrapper for the document node would fail every one of them.
        if (is_document_root(txn, top)) { return document_; }
        return wrap(c, top);
    });
    // THE ChildNode THREE, DOM 4.2.7 - each is: the viable sibling, THEN the
    // conversion, THEN one insertion. `ChildNode-after.html` puts the very
    // siblings that follow a node into its `after()` call and asserts they end
    // up after it in argument order; the immediate sibling was one of them.
    method(child_node, "before", 0, [this, parent_of](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id parent = parent_of(self);
        if (!parent) { return value::undefined(); }
        const node_id previous = viable_sibling(self, args, false);
        const node_id node = convert_nodes(c, args);
        // "If viablePreviousSibling is null, set it to parent's first child;
        // otherwise to viablePreviousSibling's next sibling" - read AFTER the
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
        // "If this's parent is parent, replace this with node" - and it is NOT
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
    // checks with `child` as the reference - so a `child` that is not this
    // node's is a NotFoundError - and then the swap. Replacing a node WITH
    // ITSELF leaves it where it is, which `Node-replaceChild.html` asserts.
    method(node, "replaceChild", 2, [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const value node_arg = arg(args, 0);
        const value child_arg = arg(args, 1);
        const node_id fresh = handle_of(node_arg);
        const node_id stale = handle_of(child_arg);
        // BOTH ARGUMENTS ARE `Node`, not `Node?`: null is a TypeError for either.
        if ((!fresh && !is_a_document(node_arg)) || (!stale && !is_a_document(child_arg))) {
            c.throw_error("TypeError", "replaceChild: the argument is not a Node");
            return value::undefined();
        }
        if (!pre_insert_valid(c, parent, fresh, node_arg, child_arg)) { return value::undefined(); }
        if (fresh == stale) { return child_arg; }
        (void)insert_node(parent, fresh, stale);
        (void)doc_->remove_child(stale);
        mutated();
        return child_arg;
    });
    // `cloneNode(deep)` - a DETACHED copy, and without it there is no way at all
    // to duplicate a template, which is how a page builds a list from one row.
    method(node, "cloneNode", 0, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        const bool deep = !args.empty() && context::truthy(args[0]);
        const auto txn = doc_->read();
        return wrap(c, clone_node(txn, self, deep));
    });
    // `contains` INCLUDES THE NODE ITSELF, which is the part that is easy to get
    // wrong: `el.contains(el)` is true in every browser.
    // `lookupNamespaceURI`, `lookupPrefix` and `isDefaultNamespace`, DOM 4.4,
    // on every node the wrapper is made for. The document had all three
    // (bindings/document/as_node.cpp) and nothing else did, which left
    // `Node-lookupPrefix.xhtml` nine TypeErrors out of eleven. Each is "run the
    // element algorithm on THE element this node names": an element is its own,
    // a Text or Comment names its parent element, a DocumentFragment names
    // nothing - and the algorithms themselves are the document's, shared.
    const auto namespace_element = [this](node_id self) {
        const auto txn = doc_->read();
        switch (txn.kind(self).value_or(node_kind::element)) {
        case node_kind::element: return self;
        case node_kind::text:
        case node_kind::comment:
        case node_kind::cdata_section:
        case node_kind::processing_instruction: {
            const node_id parent = txn.parent(self);
            return parent && txn.kind(parent).value_or(node_kind::text) == node_kind::element
                       ? parent
                       : node_id{};
        }
        case node_kind::document:
            for (const node_id child : txn.children(self)) {
                if (txn.kind(child).value_or(node_kind::text) == node_kind::element) {
                    return child;
                }
            }
            return node_id{};
        case node_kind::document_fragment:
        case node_kind::document_type: return node_id{};
        }
        return node_id{};
    };
    method(node, "lookupNamespaceURI", 1,
           [this, namespace_element](context & c, std::span<value> args) {
               const value given = arg(args, 0);
               // "If prefix is the empty string, then set it to null."
               const std::string prefix = given.is_nullish() ? std::string{} : c.to_string(given);
               const std::string found = locate_namespace(namespace_element(receiver(c)),
                                                          prefix.empty() ? nullptr : &prefix);
               return found.empty() ? value::null() : c.string(found);
           });
    method(node, "isDefaultNamespace", 1,
           [this, namespace_element](context & c, std::span<value> args) {
               const value given = arg(args, 0);
               const std::string want = given.is_nullish() ? std::string{} : c.to_string(given);
               return value::boolean(locate_namespace(namespace_element(receiver(c)), nullptr) ==
                                     want);
           });
    method(node, "lookupPrefix", 1, [this, namespace_element](context & c, std::span<value> args) {
        const value given = arg(args, 0);
        if (given.is_nullish()) { return value::null(); }
        const std::string found =
            locate_namespace_prefix(namespace_element(receiver(c)), c.to_string(given));
        return found.empty() ? value::null() : c.string(found);
    });
    // `normalize()`, DOM 4.4: every EMPTY Text descendant goes, and every run of
    // contiguous Text siblings becomes its first member. The first member and
    // not a new node - `Node-normalize.html` holds the node and reads its data
    // afterwards - and an empty first member goes rather than absorbing the run,
    // which is the order the specification walks in and the bug 19837 case.
    method(node, "normalize", 0, [this](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        std::vector<std::pair<node_id, std::string>> merged;
        std::vector<node_id> removed;
        {
            const auto txn = doc_->read();
            const auto is_text = [&txn](node_id one) {
                return txn.kind(one).value_or(node_kind::element) == node_kind::text;
            };
            const auto walk = [&](auto && again, node_id at) -> void {
                const std::span<const node_id> kids = txn.children(at);
                for (std::size_t i = 0; i < kids.size();) {
                    if (!is_text(kids[i])) {
                        again(again, kids[i]);
                        ++i;
                        continue;
                    }
                    if (txn.text(kids[i]).empty()) {
                        removed.push_back(kids[i]);
                        ++i;
                        continue;
                    }
                    std::string data{txn.text(kids[i])};
                    std::size_t j = i + 1;
                    for (; j < kids.size() && is_text(kids[j]); ++j) {
                        data += txn.text(kids[j]);
                        removed.push_back(kids[j]);
                    }
                    if (j > i + 1) { merged.emplace_back(kids[i], std::move(data)); }
                    i = j;
                }
            };
            walk(walk, self);
        }
        if (merged.empty() && removed.empty()) { return value::undefined(); }
        for (const auto & [node, data] : merged) { (void)doc_->set_text(node, data); }
        for (const node_id node : removed) { (void)doc_->remove_child(node); }
        mutated();
        return value::undefined();
    });
    method(node, "contains", 1, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id other = handle_of(arg(args, 0));
        if (!self || !other) { return value::boolean(false); }
        return value::boolean(doc_->read().is_ancestor_of(self, other));
    });
    method(element, "getBoundingClientRect", 0, [this](context & c, std::span<value>) {
        const rect box = box_of(receiver(c));
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        const auto set = [&](const char * name, float v) {
            out->set(name, value::number(static_cast<double>(v)));
        };
        set("x", box.x);
        set("y", box.y);
        set("left", box.x);
        set("top", box.y);
        set("width", box.width);
        set("height", box.height);
        // right and bottom are DERIVED, and pages read them directly rather
        // than adding the width themselves.
        set("right", box.x + box.width);
        set("bottom", box.y + box.height);
        return value::object(out);
    });
    method(node, "appendChild", 1, [this](context & c, std::span<value> args) {
        // THROUGH insert_node, which is where a DocumentFragment is flattened:
        // appending one must move its children and leave the fragment behind.
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        if (!pre_insert_valid(c, parent, child, arg(args, 0), value::null())) {
            return value::undefined();
        }
        (void)insert_node(parent, child, node_id{});
        return arg(args, 0);
    });
    // "If child's parent is not this, then throw a NotFoundError" - DOM
    // §4.2.3. Removing a node from an element that is not its parent used to
    // succeed and remove it from wherever it actually was, which is a silent
    // corruption of the caller's tree rather than a refused operation.
    method(node, "removeChild", 1, [this](context & c, std::span<value> args) {
        const node_id child = handle_of(arg(args, 0));
        const node_id parent = receiver(c);
        if (!child) {
            // The document object is a Node with no handle - see
            // pre_insert_valid - and it is nobody's child, so `s.removeChild
            // (document)` is the NotFoundError below rather than a TypeError.
            // So is a node of ANOTHER document: `Node-removeChild.html` hands
            // this one a frame's and a synthetic document's nodes.
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
    // `matches` and `closest`, THROUGH `element_matches` AND NOT THROUGH A
    // DOCUMENT QUERY.
    //
    // Both used to ask `query()` for every match in the tree and then look for
    // this element in the answer, which is O(document) per call and - worse -
    // is a different QUESTION. A detached element is in no document, so it was
    // never in that list: `document.createElement('div').matches('div')` was
    // false, and so was every `matches` a page ran on an element it had just
    // built. `style::engine::element_matches` builds the ancestor chain of one
    // element and runs the matcher over that, which is the same matcher
    // `select` runs - so the three still cannot disagree about what a selector
    // MEANS - and it was fixed for exactly the detached case in e00268b while
    // nothing called it.
    //
    // The selector is PARSED PER CALL, as it is in `query`: a compiled_selector
    // owns everything it holds, and a selector string is a handful of tokens.
    const auto compiled = [this](context & c, std::span<value> args, bool & bad) {
        return style::css::parse_selector_text(arg_string(c, args, 0), *atoms_, bad);
    };
    // `webkitMatchesSelector` IS `matches` under its legacy name - DOM 4.9
    // defines it as an alias, and `Element-webkitMatchesSelector.html` runs
    // the whole of `matches`' battery against it.
    const script::native_fn matches = [this, compiled, needs_selector](context & c,
                                                                       std::span<value> args) {
        if (!needs_selector(c, args, "matches")) { return value::boolean(false); }
        const node_id self = receiver(c);
        bool bad = false;
        const style::css::stylesheet parsed = compiled(c, args, bad);
        // "If s is not a valid selector, throw a SyntaxError" - DOM 4.9, and
        // the same refusal shadowRoot.querySelector already makes. A selector
        // that is valid CSS this engine cannot answer - `:has(.x)` - is not
        // this, and matches nothing.
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
               // INCLUSIVE, and upward: the element itself is the first candidate,
               // and it stays `:scope` for every ancestor tried (Element-closest:
               // `div > :scope` is about the element, not the ancestor).
               for (node_id at = self; at; at = txn.parent(at)) {
                   if (selector_engine().element_matches(txn, at, parsed.selectors, self)) {
                       return wrap(c, at);
                   }
               }
               return value::null();
           });

    // `isEqualNode` and `isSameNode` - so an element, a text node, a comment and
    // a fragment all have them. The Document has its OWN pair as own properties
    // (bindings/document/as_node.cpp) and those shadow these; with one document
    // per page they can only agree.
    method(node, "isEqualNode", 1, [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        // A NULL ARGUMENT IS NOT AN ERROR AND IS NOT EQUAL. The IDL is `Node?`,
        // so `isEqualNode(null)` is a question with the answer false rather
        // than a TypeError.
        const node_id other = handle_of(arg(args, 0));
        if (!self || !other) { return value::boolean(false); }
        const auto txn = doc_->read();
        return value::boolean(nodes_are_equal(txn, self, other));
    });
    method(node, "isSameNode", 1, [this](context & c, std::span<value> args) {
        // IDENTITY, and nothing else: this is `===` with a name, and it is a
        // separate method because `isEqualNode` is not.
        const node_id self = receiver(c);
        const node_id other = handle_of(arg(args, 0));
        return value::boolean(self && other && self == other);
    });
    method(node, "hasChildNodes", 0, [this](context & c, std::span<value>) {
        const node_id self = receiver(c);
        return value::boolean(self && !doc_->read().children(self).empty());
    });
    // `compareDocumentPosition(other)`, DOM 4.4 - the bitmask, and the document
    // has its own (as_node.cpp) because it is not a wrapper. Two nodes in
    // different trees are DISCONNECTED and IMPLEMENTATION_SPECIFIC, with a
    // direction that only has to be CONSISTENT: the packed handle orders them.
    method(node, "compareDocumentPosition", 1, [this](context & c, std::span<value> args) {
        constexpr unsigned disconnected = 0x01, preceding = 0x02, following = 0x04, contains = 0x08,
                           contained_by = 0x10, implementation_specific = 0x20;
        const node_id self = receiver(c);
        const value given = arg(args, 0);
        const node_id other = handle_of(given);
        const auto txn = doc_->read();
        if (!other) {
            // The document object: it contains everything connected and is
            // disconnected from the rest - and either way it comes first.
            if (is_the_document(given) && self) {
                const bool connected = is_document_root(txn, root_of_tree(txn, self, false));
                return value::number(connected
                                         ? (contains | preceding)
                                         : (disconnected | implementation_specific | preceding));
            }
            c.throw_error("TypeError", "compareDocumentPosition: the argument is not a Node");
            return value::undefined();
        }
        if (!self || self == other) { return value::number(0); }
        if (root_of_tree(txn, self, false) != root_of_tree(txn, other, false)) {
            return value::number(disconnected | implementation_specific |
                                 (pack(other) < pack(self) ? preceding : following));
        }
        if (txn.is_ancestor_of(other, self)) { return value::number(contains | preceding); }
        if (txn.is_ancestor_of(self, other)) { return value::number(contained_by | following); }
        // Neither contains the other: walk both up to the root and compare the
        // two children of the deepest shared ancestor by their order in it.
        const auto chain = [&txn](node_id from) {
            std::vector<node_id> up;
            for (node_id at = from; at; at = txn.parent(at)) { up.push_back(at); }
            return up;
        };
        const std::vector<node_id> mine = chain(self);
        const std::vector<node_id> theirs = chain(other);
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

    // THE CONSTANTS, on Node.prototype AND on the Node interface object, which
    // is where WebIDL puts a constant: `Node.ELEMENT_NODE` and
    // `element.ELEMENT_NODE` are both 1, and `Node-constants.html` reads both.
    // Enumerable, non-writable, non-configurable, as a constant is.
    if (secondary_) { return; }
    for (const auto & [name, bits] : std::initializer_list<std::pair<const char *, double>>{
             {"ELEMENT_NODE", 1},
             {"ATTRIBUTE_NODE", 2},
             {"TEXT_NODE", 3},
             {"CDATA_SECTION_NODE", 4},
             {"ENTITY_REFERENCE_NODE", 5},
             {"ENTITY_NODE", 6},
             {"PROCESSING_INSTRUCTION_NODE", 7},
             {"COMMENT_NODE", 8},
             {"DOCUMENT_NODE", 9},
             {"DOCUMENT_TYPE_NODE", 10},
             {"DOCUMENT_FRAGMENT_NODE", 11},
             {"NOTATION_NODE", 12},
             {"DOCUMENT_POSITION_DISCONNECTED", 0x01},
             {"DOCUMENT_POSITION_PRECEDING", 0x02},
             {"DOCUMENT_POSITION_FOLLOWING", 0x04},
             {"DOCUMENT_POSITION_CONTAINS", 0x08},
             {"DOCUMENT_POSITION_CONTAINED_BY", 0x10},
             {"DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC", 0x20}}) {
        if (auto * proto = prototype_object(interface_prototype("Node"))) {
            proto->define(name, value::number(bits), script::attr_enumerable);
        }
        if (const value ctor = cx.global("Node"); ctor.is_kind(script::heap_kind::native)) {
            static_cast<script::native_object *>(ctor.as_heap())
                ->define(name, value::number(bits), script::attr_enumerable);
        }
    }
}

} // namespace ctbrowser::shell
