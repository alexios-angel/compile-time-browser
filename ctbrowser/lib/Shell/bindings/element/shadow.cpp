//===- shadow.cpp - Shadow DOM bindings -------------------------*- C++ -*-===//
//
// `element.attachShadow()` and the members a ShadowRoot wrapper has beyond a
// plain DocumentFragment. The shadow tree itself (host, mode, flags) is stored
// by the document; these are the script-facing adapters.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// The document owns each detached fragment's host/mode relationship. These
// adapters retain the VM's conversion, error and wrapper behavior; native
// callers use document and read_txn directly.

node_id dom_bindings::shadow_root_of(node_id host) const {
    return doc_->shadow_root_of(host);
}

const dom_bindings::shadow_tree * dom_bindings::shadow_tree_of(node_id root) const {
    return doc_->shadow_tree_of(root);
}

node_id dom_bindings::root_of_tree(const read_txn & txn, node_id from, bool composed) const {
    return txn.root_of_tree(from, composed);
}

/// `element.attachShadow(init)`, DOM 4.8.
///
/// The order of the refusals is the specification's and is observable:
/// `mode` is a required member of a required dictionary, so WebIDL's argument
/// conversion runs, and throws a plain TypeError, before the element is
/// looked at. Only then may the element be the wrong kind (NotSupportedError),
/// and only then can it already have a shadow root.
value dom_bindings::attach_shadow(context & cx, node_id host, std::span<value> args) {
    const value init = arg(args, 0);
    std::string mode;
    if (init.is_object()) {
        const value given = cx.lookup_property(init, "mode");
        if (!given.is_undefined()) { mode = cx.to_string(given); }
    }
    if (mode != "open" && mode != "closed") {
        cx.throw_error("TypeError",
                       "attachShadow: `mode` is required and must be \"open\" or \"closed\"");
        return value::undefined();
    }
    // The rest of the dictionary, still before the element is looked at:
    // `slotAssignment` is a WebIDL enumeration, so a value that is neither
    // "named" nor "manual" is a TypeError from the conversion, the same rank
    // of failure as a missing `mode`, and comes before any NotSupportedError.
    const value given_assignment = dict_member(cx, init, "slotAssignment");
    const std::string assignment =
        given_assignment.is_undefined() ? "named" : cx.to_string(given_assignment);
    if (assignment != "named" && assignment != "manual") {
        cx.throw_error("TypeError", "attachShadow: `slotAssignment` must be \"named\" or "
                                    "\"manual\"");
        return value::undefined();
    }
    if (!host) {
        cx.throw_error("TypeError", "attachShadow: the receiver is not an Element");
        return value::undefined();
    }
    if (!shadow_registry_option(cx, host, init)) { return value::undefined(); }
    // DOM 4.8 step 3: a custom element whose definition (looked up by name
    // and `is`, upgraded or not) lists "shadow" in disabledFeatures cannot
    // host one. Checked before the existing-root rules below.
    {
        const auto txn = doc_->read();
        if (const std::size_t def = custom_definition_for(txn, host);
            def < primary().custom_definitions_.size() &&
            primary().custom_definitions_[def].disable_shadow) {
            throw_dom_exception(cx, "NotSupportedError",
                                "attachShadow: this custom element disables shadow roots");
            return value::undefined();
        }
    }
    // A root the parser attached is claimed, not refused (DOM 4.8 step 4): a
    // shadow root that is still declarative and whose mode matches is emptied
    // and handed back, so a page can reach a `<template shadowrootmode>` tree
    // it did not build, including a closed one. It works once: clearing the
    // declarative flag makes a second call the ordinary NotSupportedError.
    if (const node_id existing = shadow_root_of(host)) {
        const shadow_tree * tree = shadow_tree_of(existing);
        if (tree != nullptr && tree->declarative && tree->open == (mode == "open")) {
            std::vector<node_id> children;
            {
                const auto txn = doc_->read();
                const std::span<const node_id> kids = txn.children(existing);
                children.assign(kids.begin(), kids.end());
            }
            for (const node_id child : children) { (void)doc_->remove_child(child); }
            doc_->set_shadow_declarative(existing, false);
            mutated();
            return wrap(cx, existing);
        }
    }
    const auto root = doc_->attach_shadow(
        host, document::shadow_tree{.host = host,
                                    .open = mode == "open",
                                    .delegates_focus = dict_flag(cx, init, "delegatesFocus"),
                                    .manual_slots = assignment == "manual",
                                    .clonable = dict_flag(cx, init, "clonable"),
                                    .serializable = dict_flag(cx, init, "serializable")});
    if (!root) {
        if (root.error() == dom_error::shadow_root_exists) {
            throw_dom_exception(cx, "NotSupportedError",
                                "attachShadow: this element already hosts a shadow root");
        } else if (root.error() == dom_error::too_deep) {
            throw_dom_exception(cx, "HierarchyRequestError",
                                "attachShadow: the tree would be deeper than the engine allows");
        } else {
            const std::string tag{atoms_->text(doc_->read().tag(host).value_or(atom{}))};
            throw_dom_exception(cx, "NotSupportedError",
                                "attachShadow: <" + tag + "> cannot host a shadow root");
        }
        return value::undefined();
    }
    // Metadata precedes wrapping: prototype_for_node distinguishes the root
    // from an ordinary DocumentFragment by asking the document.
    const value made = wrap(cx, *root);
    // "Available to element internals" (DOM 4.8 step 8) holds only when the
    // host's custom element state is precustomized or custom: a root attached
    // before the element became custom is not one ElementInternals.shadowRoot
    // hands out. The absence is recorded as a mark on the root's wrapper, so a
    // root the parser attached (declarative, which HTML makes available) needs
    // no mark.
    const auto state = custom_elements_.find(host.key());
    if ((state == custom_elements_.end() ||
         state->second.state == custom_element_state::status::failed) &&
        made.is_object()) {
        static_cast<script::object_object *>(made.as_heap())
            ->define(std::string{shadow_hidden_from_internals_key}, value::boolean(true),
                     script::attr_none);
    }
    return made;
}

/// Installs the members a ShadowRoot has that a plain DocumentFragment does
/// not: `mode`, `host` and the init dictionary's flags read back.
///
/// Everything else comes from `wrap`, which gives every node the interface
/// prototypes and install_element_views, so `innerHTML`, `appendChild`,
/// `childNodes`, `textContent` and the rest are the code an element uses, and
/// `getElementById` comes from DocumentFragment.prototype.
void dom_bindings::install_shadow_root_members(context & cx, script::object_object & obj,
                                               node_id root) {
    const shadow_tree * tree = shadow_tree_of(root);
    if (tree == nullptr) { return; }
    const node_id host = tree->host;
    const bool open = tree->open;
    // `mode` and `host` are read-only accessors rather than data properties,
    // for the same reason `parentNode` is: the host may be moved or removed
    // and the answer has to follow it.
    obj.define_accessor(
        "mode",
        value::object(cx.allocate<script::native_object>(
            "mode",
            [open](context & c, std::span<value>) { return c.string(open ? "open" : "closed"); })),
        value::undefined());
    obj.define_accessor(
        "host",
        value::object(cx.allocate<script::native_object>(
            "host", [this, host](context & c, std::span<value>) { return wrap(c, host); })),
        value::undefined());
    // The rest of the init dictionary, read back. Pages feature-detect
    // declarative shadow DOM with these, `getHTML` consults them and
    // `cloneNode` carries them (WPT shadow-dom/declarative/gethtml.html reads
    // all four).
    const bool delegates = tree->delegates_focus;
    const bool manual = tree->manual_slots;
    const bool clonable = tree->clonable;
    const bool serializable = tree->serializable;
    for (const auto & [name, held] :
         std::initializer_list<std::pair<const char *, bool>>{{"delegatesFocus", delegates},
                                                              {"clonable", clonable},
                                                              {"serializable", serializable}}) {
        obj.define_accessor(
            name,
            value::object(cx.allocate<script::native_object>(
                name, [held](context &, std::span<value>) { return value::boolean(held); })),
            value::undefined());
    }
    obj.define_accessor("slotAssignment",
                        value::object(cx.allocate<script::native_object>(
                            "slotAssignment",
                            [manual](context & c, std::span<value>) {
                                return c.string(manual ? "manual" : "named");
                            })),
                        value::undefined());
}

} // namespace ctbrowser::shell
