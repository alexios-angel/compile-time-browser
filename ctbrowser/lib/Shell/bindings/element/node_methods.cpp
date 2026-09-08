// dom_bindings - the Node, ParentNode and ChildNode method surface: insertion
// validity, insertAdjacent*, tree mutation, selectors and geometry.
//
// One of twelve files carved out of a 5,442-line bindings/element.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the helpers more than one of them
// needs are declared in internal.hpp beside this, with external linkage in
// ctbrowser::shell::detail. Nothing about the public header changed.

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
    if (!child) {
        // Not a Node at all. WebIDL reports a failed conversion as a TypeError
        // rather than a DOMException, which is the one case in these steps that
        // is not a DOMException.
        (void)node_arg;
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
    if (txn.kind(child).value_or(node_kind::element) == node_kind::document) {
        throw_dom_exception(cx, "HierarchyRequestError", "a Document cannot be inserted");
        return false;
    }
    return true;
}

void dom_bindings::install_node_methods(context & cx, script::object_object & obj) {
    const auto method = [&](std::string name, script::native_fn fn) {
        obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
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
    // `insertAdjacentHTML(position, markup)` - a fragment parse at one of four
    // places relative to this element. The parser and the copy are the same
    // ones innerHTML uses; only where the nodes land differs.
    method("insertAdjacentHTML", [this, adjacent_place](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self || atoms_ == nullptr) { return value::undefined(); }
        const std::string where = arg_string(c, args, 0);
        const std::string markup = arg_string(c, args, 1);
        const std::optional<std::pair<node_id, node_id>> place = adjacent_place(c, self, where);
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
            if (place->second) { (void)doc_->insert_before(place->first, made, place->second); }
        }
        mutated();
        return value::undefined();
    });
    // ...and the two spellings that take a NODE rather than markup, both of
    // which were missing. `insertAdjacentElement` ANSWERS with the element it
    // inserted - or null, which is how a page learns the position was one the
    // element has no room for.
    method("insertAdjacentElement", [this, adjacent_place](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        const std::optional<std::pair<node_id, node_id>> place =
            adjacent_place(c, self, arg_string(c, args, 0));
        if (!place) { return value::null(); }
        const node_id child = handle_of(arg(args, 1));
        if (!pre_insert_valid(c, place->first, child, arg(args, 1), value::null())) {
            return value::null();
        }
        (void)insert_node(place->first, child, place->second);
        return arg(args, 1);
    });
    method("insertAdjacentText", [this, adjacent_place](context & c, std::span<value> args) {
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
    method("remove", [this](context & c, std::span<value>) {
        const node_id self = receiver(c);
        if (self) {
            (void)doc_->remove_child(self);
            mutated();
        }
        return value::undefined();
    });
    method("insertBefore", [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        const node_id before = handle_of(arg(args, 1));
        // A null reference node means "at the end", which is what makes
        // `insertBefore(node, null)` a documented spelling of appendChild -
        // insert_node reads an empty handle the same way.
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
    method("moveBefore", [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const node_id child = handle_of(arg(args, 0));
        if (!pre_insert_valid(c, parent, child, arg(args, 0), arg(args, 1))) {
            return value::undefined();
        }
        {
            const auto txn = doc_->read();
            // "If node's parent is null, then throw a HierarchyRequestError" -
            // a move has to move something from somewhere.
            if (!txn.parent(child)) {
                throw_dom_exception(c, "HierarchyRequestError",
                                    "moveBefore: the node being moved has no parent");
                return value::undefined();
            }
        }
        (void)insert_node(parent, child, handle_of(arg(args, 1)));
        return arg(args, 0);
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
    method("querySelector", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = query(arg_string(c, args, 0), receiver(c));
        return found.empty() ? value::null() : wrap(c, found.front());
    });
    method("querySelectorAll", [this](context & c, std::span<value> args) {
        const std::vector<node_id> found = query(arg_string(c, args, 0), receiver(c));
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        for (const node_id node : found) { items->items.push_back(wrap(c, node)); }
        return out;
    });
    // `element.getElementsByTagName(tag)` - the DOCUMENT had one and an element
    // did not, so a page that scoped its search to a subtree found the method
    // missing. p5's XML module walks a parsed document with exactly this.
    method("getElementsByTagName", [this](context & c, std::span<value> args) {
        const node_id from = receiver(c);
        const std::string want = arg_string(c, args, 0);
        value out = c.make_array();
        auto * items = static_cast<script::array_object *>(out.as_heap());
        if (!from) { return out; }
        const auto txn = doc_->read();
        // `*` is every descendant, which is what a page uses to count a subtree.
        const atom tag = want == "*" ? atom{} : atoms_->intern_lower(want);
        const auto walk = [&](auto && self, node_id at, bool include) -> void {
            if (include && (want == "*" || txn.tag(at).value_or(atom{}) == tag)) {
                items->items.push_back(wrap(c, at));
            }
            for (const node_id child : txn.children(at)) { self(self, child, true); }
        };
        // DESCENDANTS ONLY - the element itself is not one of its own results.
        walk(walk, from, false);
        return out;
    });
    // `element.getElementsByTagNameNS(namespace, localName)`, the same battery
    // the document has answered all along. `Document-Element-getElementsBy...
    // TagNameNS.js` runs its whole table against BOTH, so half of every case in
    // it was a TypeError on a missing method rather than a comparison. "*"
    // means any on either half, and - as with getElementsByTagName - the
    // element is not one of its own results, which the file asserts by name.
    method("getElementsByTagNameNS", [this](context & c, std::span<value> args) {
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
    method("getElementsByClassName", [this](context & c, std::span<value> args) {
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
    const auto parent_of = [this](node_id id) { return doc_->read().parent(id); };
    method("append", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        for (const value & one : args) { (void)insert_node(self, node_from(c, one), node_id{}); }
        return value::undefined();
    });
    method("prepend", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        // BEFORE THE FIRST CHILD, and the arguments keep their order because
        // each is inserted before the SAME reference node rather than before
        // the one just added.
        node_id first;
        {
            const auto txn = doc_->read();
            const auto children = txn.children(self);
            if (!children.empty()) { first = children.front(); }
        }
        for (const value & one : args) { (void)insert_node(self, node_from(c, one), first); }
        return value::undefined();
    });
    // `replaceChildren` - the third of the ParentNode mixin, and the one an
    // element did not have. `document` has had it all along
    // (bindings/document.cpp); an element and a ShadowRoot are where a page
    // actually calls it, because "empty this and put those in it" is what
    // rebuilding a list IS.
    method("replaceChildren", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::undefined(); }
        // COPIED BEFORE REMOVING: children() is a view onto the live child list
        // and each removal republishes it.
        std::vector<node_id> existing;
        {
            const auto txn = doc_->read();
            for (const node_id child : txn.children(self)) { existing.push_back(child); }
        }
        for (const node_id child : existing) { (void)doc_->remove_child(child); }
        for (const value & one : args) { (void)insert_node(self, node_from(c, one), node_id{}); }
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
    method("attachShadow", [this](context & c, std::span<value> args) {
        return attach_shadow(c, receiver(c), args);
    });
    // `getRootNode(options)`, DOM 4.4. On every node, which is what
    // `rootNode.html` asks of an element, a text node and a fragment in turn.
    method("getRootNode", [this](context & c, std::span<value> args) {
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
    method("before", [this, parent_of](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id parent = parent_of(self);
        for (const value & one : args) { (void)insert_node(parent, node_from(c, one), self); }
        return value::undefined();
    });
    method("after", [this, parent_of](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id parent = parent_of(self);
        if (!parent) { return value::undefined(); }
        // The reference is the NEXT sibling, and an empty one means "at the
        // end" - which is exactly what insert_node does with an empty handle.
        node_id next;
        {
            const auto txn = doc_->read();
            const auto children = txn.children(parent);
            for (std::size_t i = 0; i + 1 < children.size(); ++i) {
                if (children[i] == self) { next = children[i + 1]; }
            }
        }
        for (const value & one : args) { (void)insert_node(parent, node_from(c, one), next); }
        return value::undefined();
    });
    method("replaceWith", [this, parent_of](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id parent = parent_of(self);
        if (!parent) { return value::undefined(); }
        for (const value & one : args) { (void)insert_node(parent, node_from(c, one), self); }
        (void)doc_->remove_child(self);
        mutated();
        return value::undefined();
    });
    method("replaceChild", [this](context & c, std::span<value> args) {
        const node_id parent = receiver(c);
        const node_id fresh = handle_of(arg(args, 0));
        const node_id stale = handle_of(arg(args, 1));
        if (!parent || !fresh || !stale) { return arg(args, 1); }
        (void)insert_node(parent, fresh, stale);
        (void)doc_->remove_child(stale);
        mutated();
        return arg(args, 1);
    });
    // `cloneNode(deep)` - a DETACHED copy, and without it there is no way at all
    // to duplicate a template, which is how a page builds a list from one row.
    method("cloneNode", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        if (!self) { return value::null(); }
        const bool deep = !args.empty() && context::truthy(args[0]);
        const auto txn = doc_->read();
        return wrap(c, clone_node(txn, self, deep));
    });
    // `contains` INCLUDES THE NODE ITSELF, which is the part that is easy to get
    // wrong: `el.contains(el)` is true in every browser.
    method("contains", [this](context & c, std::span<value> args) {
        const node_id self = receiver(c);
        const node_id other = handle_of(arg(args, 0));
        if (!self || !other) { return value::boolean(false); }
        return value::boolean(doc_->read().is_ancestor_of(self, other));
    });
    method("getBoundingClientRect", [this](context & c, std::span<value>) {
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
    method("appendChild", [this](context & c, std::span<value> args) {
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
    method("removeChild", [this](context & c, std::span<value> args) {
        const node_id child = handle_of(arg(args, 0));
        const node_id parent = receiver(c);
        if (!child) {
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
    method("matches", [this, compiled](context & c, std::span<value> args) {
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
    });
    method("closest", [this, compiled](context & c, std::span<value> args) {
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
        // INCLUSIVE, and upward: the element itself is the first candidate.
        for (node_id at = self; at; at = txn.parent(at)) {
            if (selector_engine().element_matches(txn, at, parsed.selectors)) {
                return wrap(c, at);
            }
        }
        return value::null();
    });
}

} // namespace ctbrowser::shell
