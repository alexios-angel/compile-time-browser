// dom_bindings - the parts of the shadow DOM that are not one element's own:
// slots and their assignments, `setHTMLUnsafe`/`getHTML`, and the declarative
// shadow roots the first of those attaches.
//
// `element.attachShadow` and the ShadowRoot's own members are in
// element/shadow.cpp, because they are installed per element and per root.
// What is here goes on the INTERFACE PROTOTYPES, once: `assignedNodes` is the
// same function for every <slot> there will ever be.
//
// WHAT AN ASSIGNMENT IS. DOM 4.2.2: a slottable - an element or a text node
// that is a CHILD OF A SHADOW HOST - has a name (an element's `slot`
// attribute, and always the empty string for text), and it is assigned to the
// FIRST slot in the host's shadow tree whose `name` matches. Nothing is
// stored: the assignment is a function of the two trees, so it is computed
// when it is asked for and cannot go stale the way a cached list would.
//
// ponytail: `slot.assign()` and `slotAssignment: "manual"` accept their
// arguments and assign nothing - a manual tree's slots are empty until the
// imperative API is real - and there is no `slotchange` event, which needs the
// microtask queue signals do. Neither changes what a named assignment answers.

#include <ctbrowser/shell/bindings.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser::shell {
namespace {

// The slot name an element asks for, and the one a slot offers. Text is always
// the default slot's.
[[nodiscard]] std::string slot_name_of(const read_txn & txn, atom_table & atoms, node_id id,
                                       std::string_view attribute) {
    if (!txn.tag(id)) { return {}; }
    return std::string{txn.attribute_value(id, atoms.intern(attribute))};
}

[[nodiscard]] bool is_slot(const read_txn & txn, node_id id) {
    return txn.tag(id) && txn.element_ns(id) == node_ns::html && txn.local_name(id) == "slot";
}

// The first <slot> of that name in a tree, in tree order - the only one an
// assignment can go to.
[[nodiscard]] node_id first_slot_named(const read_txn & txn, atom_table & atoms, node_id root,
                                       std::string_view name) {
    node_id found;
    const auto walk = [&](auto && self, node_id at) -> void {
        for (const node_id child : txn.children(at)) {
            if (found) { return; }
            if (is_slot(txn, child) && slot_name_of(txn, atoms, child, "name") == name) {
                found = child;
                return;
            }
            self(self, child);
        }
    };
    walk(walk, root);
    return found;
}

} // namespace

// The nodes assigned to one slot, in tree order. Empty for a slot that is not
// in a shadow tree, for a slot a NAME has already been claimed for by an
// earlier one, and for every slot of a manual-assignment tree.
std::vector<node_id> dom_bindings::assigned_nodes_of(node_id slot) const {
    const auto txn = doc_->read();
    if (!is_slot(txn, slot)) { return {}; }
    const node_id root = root_of_tree(txn, slot, false);
    const document::shadow_tree * tree = shadow_tree_of(root);
    if (tree == nullptr || tree->manual_slots) { return {}; }
    const std::string name = slot_name_of(txn, *atoms_, slot, "name");
    // FIRST SLOT OF THAT NAME WINS - a second <slot name=x> is assigned
    // nothing at all, which is what `slots-fallback.html` reads.
    if (first_slot_named(txn, *atoms_, root, name) != slot) { return {}; }
    std::vector<node_id> out;
    for (const node_id child : txn.children(tree->host)) {
        const node_kind kind = txn.kind(child).value_or(node_kind::comment);
        if (kind != node_kind::element && kind != node_kind::text) { continue; }
        if (slot_name_of(txn, *atoms_, child, "slot") == name) { out.push_back(child); }
    }
    return out;
}

// `<template shadowrootmode=open>` as the PARSER's shadow root - HTML 13.2.6,
// "attach a shadow root to the template's parent". Only `setHTMLUnsafe` and
// the parser run this: `innerHTML` leaves the template alone, which is the
// whole point of the unsafe spelling being a different method.
void dom_bindings::attach_declarative_shadow_roots(node_id within) {
    std::vector<node_id> templates;
    {
        const auto txn = doc_->read();
        const auto walk = [&](auto && self, node_id at) -> void {
            for (const node_id child : txn.children(at)) {
                if (txn.tag(child) && txn.element_ns(child) == node_ns::html &&
                    txn.local_name(child) == "template") {
                    templates.push_back(child);
                    self(self, doc_->template_content(child));
                    continue;
                }
                self(self, child);
            }
        };
        walk(walk, within);
    }
    for (const node_id one : templates) {
        node_id host;
        std::string mode;
        document::shadow_tree how;
        {
            const auto txn = doc_->read();
            mode = std::string{txn.attribute_value(one, atoms_->intern("shadowrootmode"))};
            if (mode != "open" && mode != "closed") { continue; }
            host = txn.parent(one);
            if (!host || !txn.tag(host)) { continue; }
            how.open = mode == "open";
            how.delegates_focus =
                txn.has_attribute(one, atoms_->intern("shadowrootdelegatesfocus"));
            how.manual_slots =
                txn.attribute_value(one, atoms_->intern("shadowrootslotassignment")) == "manual";
            how.clonable = txn.has_attribute(one, atoms_->intern("shadowrootclonable"));
            how.serializable = txn.has_attribute(one, atoms_->intern("shadowrootserializable"));
            how.declarative = true;
        }
        const auto root = doc_->attach_shadow(host, how);
        // An element that cannot host a shadow root, or already hosts one,
        // keeps an ORDINARY template: the markup is not an error.
        if (!root) { continue; }
        std::vector<node_id> moving;
        {
            const auto txn = doc_->read();
            const std::span<const node_id> kids = txn.children(doc_->template_content(one));
            moving.assign(kids.begin(), kids.end());
        }
        for (const node_id child : moving) { (void)doc_->append_child(*root, child); }
        (void)doc_->remove_child(one);
    }
    if (!templates.empty()) { mutated(); }
}

void dom_bindings::install_shadow_dom(context & cx) {
    // --- HTMLSlotElement ------------------------------------------------------
    if (const value slot_proto = interface_prototype("HTMLSlotElement"); slot_proto.is_object()) {
        auto * on = static_cast<script::object_object *>(slot_proto.as_heap());
        // `assignedNodes({flatten})`: without the flag the assignment itself,
        // with it what would actually be RENDERED there - the slot's own
        // children when nothing is assigned, and a nested slot expanded.
        for (const bool elements_only : {false, true}) {
            set_method(
                cx, *on, elements_only ? "assignedElements" : "assignedNodes",
                [this, elements_only](context & c, std::span<value> args) {
                    const value made = c.make_array();
                    const node_id slot = handle_of(c.current_this());
                    if (!slot) { return made; }
                    auto & items = static_cast<script::array_object *>(made.as_heap())->items;
                    const bool flatten = dict_flag(c, arg(args, 0), "flatten");
                    const auto gather = [&](auto && self, node_id at) -> void {
                        std::vector<node_id> assigned = assigned_nodes_of(at);
                        if (flatten && assigned.empty()) {
                            const auto txn = doc_->read();
                            const std::span<const node_id> kids = txn.children(at);
                            assigned.assign(kids.begin(), kids.end());
                        }
                        for (const node_id one : assigned) {
                            const auto txn = doc_->read();
                            const node_kind kind = txn.kind(one).value_or(node_kind::comment);
                            if (flatten && is_slot(txn, one)) {
                                self(self, one);
                                continue;
                            }
                            if (kind != node_kind::element &&
                                (elements_only || kind != node_kind::text)) {
                                continue;
                            }
                            if (elements_only && kind != node_kind::element) { continue; }
                            items.push_back(wrap(c, one));
                        }
                    };
                    gather(gather, slot);
                    return made;
                },
                script::attr_builtin);
        }
        // ponytail: `assign()` takes its nodes and drops them - see the note at
        // the top. It is here so a page that calls it is not a TypeError.
        set_method(
            cx, *on, "assign", [](context &, std::span<value>) { return value::undefined(); },
            script::attr_builtin);
    }

    // --- Slottable.assignedSlot, on an Element and on a Text -------------------
    //
    // NULL FOR A CLOSED TREE. The assignment is the same either way - a closed
    // slot still renders what it was given - but a page outside the tree may
    // not learn which slot that was.
    for (const char * interface : {"Element", "Text"}) {
        const value proto = interface_prototype(interface);
        if (!proto.is_object()) { continue; }
        define_getter(cx, *static_cast<script::object_object *>(proto.as_heap()), "assignedSlot",
                      [this](context & c, std::span<value>) {
                          const node_id id = handle_of(c.current_this());
                          if (!id) { return value::null(); }
                          node_id host;
                          std::string name;
                          {
                              const auto txn = doc_->read();
                              host = txn.parent(id);
                              if (!host) { return value::null(); }
                              name = slot_name_of(txn, *atoms_, id, "slot");
                          }
                          const node_id root = doc_->shadow_root_of(host);
                          const document::shadow_tree * tree = shadow_tree_of(root);
                          if (tree == nullptr || !tree->open) { return value::null(); }
                          const auto txn = doc_->read();
                          const node_id found = first_slot_named(txn, *atoms_, root, name);
                          if (!found) { return value::null(); }
                          for (const node_id one : assigned_nodes_of(found)) {
                              if (one == id) { return wrap(c, found); }
                          }
                          return value::null();
                      });
    }

    // --- getHTML / setHTMLUnsafe, on an Element and on a ShadowRoot ------------
    //
    // `setHTMLUnsafe` is `innerHTML` PLUS the declarative shadow roots: the
    // markup is parsed the same way and then every `<template shadowrootmode>`
    // in it becomes a real shadow root. That is the only difference, and it is
    // why the safe spelling leaves the templates where they are.
    for (const char * interface : {"Element", "ShadowRoot"}) {
        const value proto = interface_prototype(interface);
        if (!proto.is_object()) { continue; }
        auto * on = static_cast<script::object_object *>(proto.as_heap());
        set_method(
            cx, *on, "setHTMLUnsafe",
            [this](context & c, std::span<value> args) {
                if (const node_id id = handle_of(c.current_this())) {
                    set_inner_html(id, arg_string(c, args, 0));
                    attach_declarative_shadow_roots(id);
                }
                return value::undefined();
            },
            script::attr_builtin);
        // ponytail: `getHTML({serializableShadowRoots, shadowRoots})` answers
        // what `innerHTML` does - a serializable shadow root is NOT written
        // out as its `<template shadowrootmode>`. That needs the fragment
        // serialiser itself to know about shadow roots; see the report.
        set_method(
            cx, *on, "getHTML",
            [this](context & c, std::span<value>) {
                const node_id id = handle_of(c.current_this());
                return c.string(id ? inner_html(id) : std::string{});
            },
            script::attr_builtin);
    }
}

} // namespace ctbrowser::shell
