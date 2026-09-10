// dom_bindings - the shadow DOM.
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

// ===================== the shadow DOM =====================================
//
// DOM 4.8, far enough that a test which merely USES a shadow tree can run.
//
// A SHADOW ROOT IS A DocumentFragment PLUS TWO FACTS, which is the whole reason
// this lives in the bindings and not in lib/DOM: `node_kind` already has a
// document_fragment - a parentless bag of nodes - and that is exactly the shape
// the specification gives a shadow root. What a fragment does not carry is its
// HOST and its MODE, and neither belongs on `node`: it is the most replicated
// object in the engine and every field on it is paid for by every document that
// has never heard of shadow DOM. They live in two maps on dom_bindings instead,
// keyed on pack(node_id) exactly as `wrappers_`, `namespaces_` and `mirrors_`
// already are.
//
// WHAT THIS DELIBERATELY DOES NOT DO IS RENDER. The fragment is detached, so the
// cascade, layout and paint never reach it: an element inside a shadow root has
// no box, no computed style and no pixels, and `getComputedStyle` on one answers
// as it does for any detached element. That is a real gap and it is named here
// rather than left to be discovered - flattening a shadow tree into the box tree
// is slot assignment and the flat tree, which is a rung of its own. Every test
// this was built for asserts about the TREE, about events, or about
// getComputedStyle on a LIGHT-DOM element.
//
// EVENT RETARGETING IS ALSO NOT HERE. An event dispatched inside a shadow tree
// is not re-targeted at the host as it crosses the boundary, so
// `shadow-relatedTarget.html` and the composed-path half of `event-global.html`
// still report what the engine dispatched rather than what the boundary should
// hide. That lives in bindings/events.cpp.

namespace {

// "VALID SHADOW HOST NAME", DOM 4.8. Sixteen HTML elements, and the list is
// exhaustive on purpose: `attachShadow` on anything else is a NotSupportedError
// rather than a shadow tree nobody can see.
constexpr std::string_view shadow_host_names = "article aside blockquote body div footer h1 h2 h3 "
                                               "h4 h5 h6 header main nav p section span";

// ...plus ANY VALID CUSTOM ELEMENT NAME, which is the half no table can carry:
// `<my-widget>` is a legal host and there is no list of the ones a page will
// invent. HTML's production is a lowercase ASCII letter, then anything that is
// not an ASCII uppercase letter, with at least one hyphen - and eight reserved
// spellings that satisfy it and name SVG or MathML elements that already exist.
[[nodiscard]] bool valid_custom_element_name(std::string_view name) {
    if (name.size() < 2 || name.front() < 'a' || name.front() > 'z') { return false; }
    if (name.find('-') == std::string_view::npos) { return false; }
    for (const char c : name) {
        if (c >= 'A' && c <= 'Z') { return false; }
    }
    for (const std::string_view taken :
         {"annotation-xml", "color-profile", "font-face", "font-face-src", "font-face-uri",
          "font-face-format", "font-face-name", "missing-glyph"}) {
        if (name == taken) { return false; }
    }
    return true;
}

} // namespace

node_id dom_bindings::shadow_root_of(node_id host) const {
    if (!host) { return node_id{}; }
    const auto it = shadow_roots_.find(pack(host));
    return it == shadow_roots_.end() ? node_id{} : it->second;
}

const dom_bindings::shadow_tree * dom_bindings::shadow_tree_of(node_id root) const {
    if (!root) { return nullptr; }
    const auto it = shadow_hosts_.find(pack(root));
    return it == shadow_hosts_.end() ? nullptr : &it->second;
}

node_id dom_bindings::root_of_tree(const read_txn & txn, node_id from, bool composed) const {
    node_id at = from;
    // A DEPTH CAP, for the reason every other walk in this file has one: a cycle
    // is refused by pre_insert_valid, and a walk that trusts that and is wrong
    // hangs the page rather than answering badly.
    for (std::size_t step = 0; at && step < 4096; ++step) {
        if (const node_id up = txn.parent(at)) {
            at = up;
            continue;
        }
        if (!composed) { break; }
        const shadow_tree * tree = shadow_tree_of(at);
        if (tree == nullptr || !tree->host) { break; }
        at = tree->host;
    }
    return at;
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
    {
        const auto txn = doc_->read();
        const std::string tag{atoms_->text(txn.tag(host).value_or(atom{}))};
        // AN HTML ELEMENT WITH ONE OF SIXTEEN NAMES, or a custom element name.
        // An <svg> is not a host, a <span> in some page-invented namespace is
        // not one either, and neither is a <table>.
        const bool can_host =
            txn.kind(host).value_or(node_kind::text) == node_kind::element &&
            txn.element_ns(host) == node_ns::html &&
            (lists_token(shadow_host_names, tag) || valid_custom_element_name(tag));
        if (!can_host) {
            throw_dom_exception(cx, "NotSupportedError",
                                "attachShadow: <" + tag + "> cannot host a shadow root");
            return value::undefined();
        }
    }
    if (shadow_root_of(host)) {
        throw_dom_exception(cx, "NotSupportedError",
                            "attachShadow: this element already hosts a shadow root");
        return value::undefined();
    }
    const node_id root = doc_->create_fragment();
    if (!root) {
        cx.throw_error("TypeError", "attachShadow: the document refused a fragment");
        return value::undefined();
    }
    shadow_roots_.emplace(pack(host), root);
    shadow_hosts_.emplace(pack(root), shadow_tree{host, mode == "open"});
    // The wrapper is made AFTER the maps are written, because prototype_for_node
    // asks them which of DocumentFragment and ShadowRoot this fragment is.
    return wrap(cx, root);
}

// THE MEMBERS A ShadowRoot HAS THAT A PLAIN DocumentFragment DOES NOT.
//
// Everything else it needs it already has: `wrap` gives every node
// install_element_methods and install_element_views, so `innerHTML`,
// `appendChild`, `append`, `replaceChildren`, `childNodes`, `children`,
// `firstChild` and `textContent` are the same code an element uses and work on a
// fragment unchanged. Only these five are different, and two of them are
// different because they have to search a tree the selector engine cannot reach.
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
    const auto method = [&](std::string name, script::native_fn fn) {
        obj.set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // `getElementById` ON THE SHADOW ROOT, which is a DocumentFragment method
    // rather than an Element one - an id inside a shadow tree is scoped to that
    // tree, and `document.getElementById` must NOT find it.
    method("getElementById", [this, root](context & c, std::span<value> args) {
        const std::string want = arg_string(c, args, 0);
        if (want.empty()) { return value::null(); }
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
}

} // namespace ctbrowser::shell
