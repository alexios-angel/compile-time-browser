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
// ponytail: no `slotchange` event - it needs the microtask queue signals do -
// and nothing here reaches the FLAT TREE that layout would render: a slot's
// assigned nodes are an answer to a question, not a rearrangement of the
// boxes. See the report.

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
    if (tree == nullptr) { return {}; }
    // MANUAL: the page said which nodes, and nothing else qualifies. A node
    // that has since left the host is dropped here rather than when it moved.
    if (tree->manual_slots) {
        const auto held = manual_slots_.find(slot.key());
        if (held == manual_slots_.end()) { return {}; }
        std::vector<node_id> out;
        for (const node_id one : held->second) {
            const node_kind kind = txn.kind(one).value_or(node_kind::comment);
            if (kind != node_kind::element && kind != node_kind::text) { continue; }
            if (txn.parent(one) == tree->host) { out.push_back(one); }
        }
        return out;
    }
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

// The <slot> a slottable is assigned to, mode-agnostic. Its host is the node's
// parent; the answer is the first slot in that host's shadow tree whose
// assigned-nodes list contains the node - the same question `assignedSlot`
// asks, without the open/closed gate the getter adds on top.
node_id dom_bindings::assigned_slot_of(node_id slottable) const {
    const auto txn = doc_->read();
    const node_id host = txn.parent(slottable);
    if (!host) { return node_id{}; }
    const node_id root = shadow_root_of(host);
    if (!root) { return node_id{}; }
    if (shadow_tree_of(root) == nullptr) { return node_id{}; }
    node_id answer;
    const auto walk = [&](auto && self, node_id at) -> void {
        for (const node_id child : txn.children(at)) {
            if (answer) { return; }
            if (is_slot(txn, child)) {
                for (const node_id one : assigned_nodes_of(child)) {
                    if (one == slottable) {
                        answer = child;
                        return;
                    }
                }
            }
            self(self, child);
        }
    };
    walk(walk, root);
    return answer;
}

// `<template shadowrootmode=open>` AS A SHADOW ROOT - HTML 13.2.6, "attach a
// shadow root to the template's parent". `setHTMLUnsafe` runs it over the
// scratch document its markup was parsed into; `innerHTML` deliberately does
// not, which is the whole difference between the two spellings.
//
// THE DOCUMENT IS AN ARGUMENT because the fragment parse happens in a scratch
// one - the tree builder replaces the root it is handed - and a template's
// contents live in the document that parsed them, not under the element, so
// the conversion has to happen THERE and the result be copied across.
void dom_bindings::attach_declarative_shadow_roots(document & doc, node_id within) {
    std::vector<node_id> templates;
    {
        const auto txn = doc.read();
        const auto walk = [&](auto && self, node_id at) -> void {
            for (const node_id child : txn.children(at)) {
                if (txn.tag(child) && txn.element_ns(child) == node_ns::html &&
                    txn.local_name(child) == "template") {
                    templates.push_back(child);
                    // A declarative root INSIDE a template's contents is one
                    // too, and the outer one is converted first - so by the
                    // time this one is, its parent is already in a shadow tree.
                    self(self, doc.template_content(child));
                    continue;
                }
                self(self, child);
            }
        };
        walk(walk, within);
    }
    for (const node_id one : templates) {
        node_id host;
        document::shadow_tree how;
        {
            const auto txn = doc.read();
            const std::string_view mode =
                txn.attribute_value(one, atoms_->intern("shadowrootmode"));
            if (mode != "open" && mode != "closed") { continue; }
            host = txn.parent(one);
            if (!host || !txn.tag(host)) { continue; }
            // NOT AT THE TOP OF THE FRAGMENT. A `<template shadowrootmode>`
            // whose parent is the fragment root has no element to be the
            // shadow of - the context element is not in the fragment tree - so
            // it stays an ordinary template, which is what every engine does
            // with one.
            if (host == within) { continue; }
            how.open = mode == "open";
            how.delegates_focus =
                txn.has_attribute(one, atoms_->intern("shadowrootdelegatesfocus"));
            how.manual_slots =
                txn.attribute_value(one, atoms_->intern("shadowrootslotassignment")) == "manual";
            how.clonable = txn.has_attribute(one, atoms_->intern("shadowrootclonable"));
            how.serializable = txn.has_attribute(one, atoms_->intern("shadowrootserializable"));
            how.declarative = true;
        }
        const auto root = doc.attach_shadow(host, how);
        // An element that cannot host a shadow root, or already hosts one,
        // keeps an ORDINARY template: the markup is not an error.
        if (!root) { continue; }
        std::vector<node_id> moving;
        {
            const auto txn = doc.read();
            const std::span<const node_id> kids = txn.children(doc.template_content(one));
            moving.assign(kids.begin(), kids.end());
        }
        for (const node_id child : moving) { (void)doc.append_child(*root, child); }
        (void)doc.remove_child(one);
    }
}

// THE SHADOW TREES A COPY DOES NOT CARRY. `copy_subtree` copies nodes and
// attributes, and a shadow root is neither - it is a second tree the document
// remembers beside the host. The two trees have the same SHAPE, so this walks
// them in step and re-attaches each root on the copy.
void dom_bindings::copy_shadow_trees(const document & src, const read_txn & from, node_id source,
                                     node_id made) {
    if (const node_id root = src.shadow_root_of(source)) {
        if (const document::shadow_tree * how = src.shadow_tree_of(root)) {
            if (const auto mine = doc_->attach_shadow(made, *how)) {
                const std::span<const node_id> kids = from.children(root);
                const std::vector<node_id> sources{kids.begin(), kids.end()};
                for (const node_id child : sources) {
                    copy_shadow_trees(src, from, child, copy_subtree(from, child, *mine));
                }
            }
        }
    }
    const std::span<const node_id> kids = from.children(source);
    const std::vector<node_id> sources{kids.begin(), kids.end()};
    const std::span<const node_id> copied = doc_->read().children(made);
    const std::vector<node_id> destinations{copied.begin(), copied.end()};
    for (std::size_t i = 0; i < sources.size() && i < destinations.size(); ++i) {
        copy_shadow_trees(src, from, sources[i], destinations[i]);
    }
}

// `setHTMLUnsafe(markup)`: `innerHTML` PLUS the declarative shadow roots.
//
// It cannot be innerHTML followed by a conversion: the fragment is parsed into
// a SCRATCH document, and a `<template>`'s contents are held by THAT document
// rather than under the element, so they are gone by the time the copy lands
// here. The conversion therefore happens in the scratch, and the copy carries
// the shadow trees over.
void dom_bindings::set_html_unsafe(node_id target, std::string_view markup) {
    if (!target || atoms_ == nullptr) { return; }
    {
        const auto txn = doc_->read();
        const std::span<const node_id> kids = txn.children(target);
        const std::vector<node_id> existing{kids.begin(), kids.end()};
        for (const node_id child : existing) { (void)doc_->remove_child(child); }
    }
    document scratch{*atoms_};
    const node_id body = parse_html_body_fragment(scratch, markup);
    attach_declarative_shadow_roots(scratch, body);
    const auto from = scratch.read();
    const std::span<const node_id> kids = from.children(body);
    const std::vector<node_id> sources{kids.begin(), kids.end()};
    for (const node_id child : sources) {
        copy_shadow_trees(scratch, from, child, copy_subtree(from, child, target));
    }
    mutated();
}

void dom_bindings::install_shadow_dom(context & cx) {
    // `Document.parseHTMLUnsafe(html)`, HTML 8.6.1: a new document, parsed
    // with scripting off and the declarative shadow roots attached - the
    // static twin of setHTMLUnsafe, on the Document interface object.
    // (The interface object is a NATIVE, not a plain object - `is_object()`
    // is heap_kind::object exactly.)
    if (const value ctor = cx.global("Document"); ctor.is_kind(script::heap_kind::native)) {
        set_method(cx, *static_cast<script::native_object *>(ctor.as_heap()), "parseHTMLUnsafe",
                   [this](context & c, std::span<value> a) {
                       const std::string markup = arg_string(c, a, 0);
                       const value made = parse_from_string(c, markup, "text/html");
                       dom_bindings * top = primary_ == nullptr ? this : primary_;
                       for (const auto & owner : top->secondary_documents_) {
                           if (!owner->is_the_document(made)) { continue; }
                           owner->attach_declarative_shadow_roots(*owner->doc_,
                                                                  owner->doc_->root());
                           owner->observe_location("about:blank", "");
                       }
                       return made;
                   });
    }
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
                            // A SLOT AMONG THEM IS ITS OWN ASSIGNMENT, flattened
                            // in its place - that is what makes the flat tree
                            // flat.
                            if (flatten && is_slot(txn, one)) {
                                self(self, one);
                                continue;
                            }
                            const node_kind kind = txn.kind(one).value_or(node_kind::comment);
                            const bool wanted = elements_only ? kind == node_kind::element
                                                              : kind == node_kind::element ||
                                                                    kind == node_kind::text;
                            if (wanted) { items.push_back(wrap(c, one)); }
                        }
                    };
                    gather(gather, slot);
                    return made;
                },
                script::attr_builtin);
        }
        // `slot.assign(...nodes)`: VARIADIC, not a sequence, and it REPLACES
        // whatever was assigned before. It means nothing to a tree that
        // assigns by name - assigned_nodes_of only reads the list for a manual
        // one - and the nodes are remembered as given, because whether one
        // still qualifies is a question about where it is NOW.
        set_method(
            cx, *on, "assign",
            [this](context & c, std::span<value> args) {
                const node_id slot = handle_of(c.current_this());
                if (!slot) { return value::undefined(); }
                std::vector<node_id> assigned;
                for (const value & one : args) {
                    const node_id id = handle_of(one);
                    const node_kind kind = id ? doc_->read().kind(id).value_or(node_kind::comment)
                                              : node_kind::comment;
                    // ONLY A SLOTTABLE: the argument type is
                    // `(Element or Text)...`, so anything else is a WebIDL
                    // conversion failure before one node is assigned.
                    if (kind != node_kind::element && kind != node_kind::text) {
                        c.throw_error("TypeError",
                                      "Failed to execute 'assign' on 'HTMLSlotElement': the "
                                      "arguments must be Element or Text nodes.");
                        return value::undefined();
                    }
                    assigned.push_back(id);
                }
                // A node may be assigned to ONE slot: taking it here takes it
                // from wherever it was.
                for (auto & [key, held] : manual_slots_) {
                    if (key == slot.key()) { continue; }
                    std::erase_if(held, [&](node_id one) {
                        return std::ranges::find(assigned, one) != assigned.end();
                    });
                }
                manual_slots_.insert_or_assign(slot.key(), std::move(assigned));
                mutated();
                return value::undefined();
            },
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
                          // NULL FOR A CLOSED TREE, though the assignment is the
                          // same: assigned_slot_of answers mode-agnostically
                          // (the event path needs that), so the open gate is
                          // here.
                          const node_id host = doc_->read().parent(id);
                          const node_id root = host ? doc_->shadow_root_of(host) : node_id{};
                          const document::shadow_tree * tree = shadow_tree_of(root);
                          if (tree == nullptr || !tree->open) { return value::null(); }
                          const node_id answer = assigned_slot_of(id);
                          return answer ? wrap(c, answer) : value::null();
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
                    set_html_unsafe(id, arg_string(c, args, 0));
                }
                return value::undefined();
            },
            script::attr_builtin);
        // `getHTML({serializableShadowRoots, shadowRoots})` - HTML fragment
        // serialisation with a "serializable shadow roots" set: a shadow root
        // is written out as its `<template shadowrootmode>` when it is in the
        // `shadowRoots` list, or when `serializableShadowRoots` is true and the
        // root's own `serializable` is set. With neither it is exactly
        // `innerHTML`.
        set_method(
            cx, *on, "getHTML",
            [this](context & c, std::span<value> args) {
                const node_id id = handle_of(c.current_this());
                if (!id) { return c.string(std::string{}); }
                bool serializable = false;
                std::vector<node_id> roots;
                const value opts = args.empty() ? value::undefined() : args[0];
                if (opts.is_object()) {
                    serializable =
                        context::truthy(c.lookup_property(opts, "serializableShadowRoots"));
                    const value list = c.lookup_property(opts, "shadowRoots");
                    if (list.is_array()) {
                        for (const value & item :
                             static_cast<script::array_object *>(list.as_heap())->items) {
                            if (const node_id r = handle_of(item)) { roots.push_back(r); }
                        }
                    }
                }
                return c.string(serialize_html(id, false, serializable, roots));
            },
            script::attr_builtin);
    }
}

} // namespace ctbrowser::shell
