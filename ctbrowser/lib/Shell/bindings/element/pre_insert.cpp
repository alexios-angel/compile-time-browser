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
        // Another document's node is a Node that is no child of this parent.
        if (!before && owner_of(ref_arg) == nullptr && !is_a_document(ref_arg)) {
            cx.throw_error("TypeError", "the reference node is not a Node");
            return false;
        }
        if (!before || txn.parent(before) != parent) {
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
    // 5. "...or node is a doctype and parent is not a document, throw a
    //    HierarchyRequestError." The parent here is never the Document - a
    //    Document's own insertions run document/as_node.cpp's checks.
    if (txn.kind(child).value_or(node_kind::element) == node_kind::document_type) {
        throw_dom_exception(cx, "HierarchyRequestError",
                            "a DocumentType can only be inserted into a Document");
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

} // namespace ctbrowser::shell
