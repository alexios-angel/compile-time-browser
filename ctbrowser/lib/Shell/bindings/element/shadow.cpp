// dom_bindings - the shadow DOM.

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

// `element.attachShadow(init)`, DOM 4.8.
//
// The ORDER of the three refusals is the specification's and is observable:
// `mode` is a required member of a required dictionary, so WebIDL's argument
// conversion runs - and throws a plain TypeError - before one thing about the
// element is looked at. Only then may the element be the wrong element
// (NotSupportedError), and only then can it already have a shadow root.
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
    if (!host) {
        cx.throw_error("TypeError", "attachShadow: the receiver is not an Element");
        return value::undefined();
    }
    const auto root = doc_->attach_shadow(host, mode == "open");
    if (!root) {
        if (root.error() == dom_error::shadow_root_exists) {
            throw_dom_exception(cx, "NotSupportedError",
                                "attachShadow: this element already hosts a shadow root");
        } else {
            const std::string tag{atoms_->text(doc_->read().tag(host).value_or(atom{}))};
            throw_dom_exception(cx, "NotSupportedError",
                                "attachShadow: <" + tag + "> cannot host a shadow root");
        }
        return value::undefined();
    }
    // Metadata precedes wrapping: prototype_for_node distinguishes the root
    // from an ordinary DocumentFragment by asking the document.
    return wrap(cx, *root);
}

// THE MEMBERS A ShadowRoot HAS THAT A PLAIN DocumentFragment DOES NOT.
//
// Everything else it needs it already has: `wrap` gives every node
// the interface prototypes and install_element_views, so `innerHTML`,
// `appendChild`, `append`, `replaceChildren`, `childNodes`, `children`,
// `firstChild` and `textContent` are the same code an element uses and work on a
// fragment unchanged, and `getElementById` comes with being a fragment - see
// DocumentFragment.prototype. Only `mode` and `host` are a ShadowRoot's own.
void dom_bindings::install_shadow_root_members(context & cx, script::object_object & obj,
                                               node_id root) {
    const shadow_tree * tree = shadow_tree_of(root);
    if (tree == nullptr) { return; }
    const node_id host = tree->host;
    const bool open = tree->open;
    // `mode` and `host` are READ-ONLY, and accessors rather than data properties
    // for the reason `parentNode` is: the host may be moved or removed and the
    // answer has to follow it.
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
}

} // namespace ctbrowser::shell
