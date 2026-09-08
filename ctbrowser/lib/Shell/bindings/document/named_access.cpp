// dom_bindings - named access on the Document object, HTML 3.1.5.
//
// One of eight files carved out of a 3,071-line bindings/document.cpp on
// 2026-09-08 - which was itself one of six carved out of bindings.cpp on
// 2026-08-09. All are member functions of one class declared in
// include/ctbrowser/shell/bindings.hpp; the name productions every one of
// them needs are in internal.hpp beside this. Nothing about the public header
// changed.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// ============================================================================
// NAMED ACCESS ON THE DOCUMENT
// ============================================================================
//
// `document.someName` for an element that carries that name - HTML 3.1.5,
// "named access on the Document object". Sixteen files in `html/dom` are about
// nothing else, and the reason it needs a Proxy rather than a set of properties
// pushed on the tick is in every one of them: they remove an attribute and read
// the property back IN THE SAME STATEMENT, expecting `undefined`.
//
// THE ELEMENT LIST IS NOT "anything with a name". It is five tags by their
// `name` and two by their `id`, and the two are not the same two:
//
//   name= : embed, form, iframe, img, object
//   id=   : object always; img ONLY IF it also has a non-empty name
//
// That last clause is the whole of `nameditem-01.html`'s third and fourth
// cases. `<img id=a name=b>` answers to both `a` and `b`; removing `name`
// removes BOTH, because the id route needs a name to exist; removing `id`
// leaves `b` alone. An implementation that indexed ids unconditionally would
// pass the first two subtests of that file and fail the next two.
std::vector<node_id> dom_bindings::named_document_items(std::string_view name) {
    std::vector<node_id> found;
    if (name.empty()) { return found; }
    const auto txn = doc_->read();
    const atom id_attribute = atoms_->intern("id");
    const atom name_attribute = atoms_->intern("name");
    const auto walk = [&](auto && self, node_id at) -> void {
        const auto tagged = txn.tag(at);
        if (tagged.has_value() && txn.element_ns(at) == node_ns::html) {
            const std::string_view local = atoms_->text(*tagged);
            const std::string_view has_name = txn.attribute_value(at, name_attribute);
            const bool by_name =
                has_name == name && (local == "embed" || local == "form" || local == "iframe" ||
                                     local == "img" || local == "object");
            const bool by_id = txn.attribute_value(at, id_attribute) == name &&
                               (local == "object" || (local == "img" && !has_name.empty()));
            if (by_name || by_id) { found.push_back(at); }
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

// THE PROXY. Two traps, and both of them consult the target FIRST.
//
// That order is the specification's: a named property is a fallback for a name
// the object does not otherwise have, so `document.forms` is the collection
// accessor installed above it and not the `<form name=forms>` on the page. It
// is also the order that keeps everything else in these bindings working -
// `document.title`, `document.body`, `createElement` and the twenty-two Node
// members all live on the target and are found before the walk is ever run.
//
// The walk is O(nodes) and runs on every `document.x` that is not an own
// property, an inherited one, or a name the tree answers - which includes
// `document.hasOwnProperty`. That is the same trade `window`'s proxy already
// makes, and the same answer if it ever shows in a measurement: an id and name
// index on the document rather than a special case here.
value dom_bindings::make_document_proxy(context & cx, value target) {
    auto * handler = static_cast<script::object_object *>(cx.make_object().as_heap());
    const auto native = [&](std::string name, script::native_fn fn) {
        return value::object(cx.allocate<script::native_object>(std::move(name), std::move(fn)));
    };
    handler->set(
        "get", native("get", [this](context & c, std::span<value> args) {
            if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
            auto * object = static_cast<script::object_object *>(args[0].as_heap());
            const std::string name = c.to_string(args[1]);
            if (object->find(name) != nullptr || object->find_accessor(name) != nullptr) {
                return c.lookup_property(args[0], name);
            }
            if (const std::vector<node_id> named = named_document_items(name); !named.empty()) {
                // ONE ELEMENT IS THE ELEMENT, several are a live
                // HTMLCollection - and an `<iframe>` alone should be
                // its content document, which this engine has no
                // second browsing context to give. It hands back the
                // iframe, which is wrong in a way that is visible and
                // cheap rather than wrong in a way that is silent.
                if (named.size() == 1) { return wrap(c, named.front()); }
                return make_live_collection(c, [this, name] { return named_document_items(name); });
            }
            // ...and failing all that the prototype chain, which now
            // ends at `Document.prototype` and `EventTarget.prototype`.
            return c.lookup_property(args[0], name);
        }));
    handler->set("has", native("has", [this](context & c, std::span<value> args) {
                     if (args.size() < 2 || !args[0].is_object()) { return value::boolean(false); }
                     const std::string name = c.to_string(args[1]);
                     // `'x' in document` must agree with `document.x`, or a
                     // page's feature detection and its use of the feature
                     // disagree.
                     return value::boolean(!c.lookup_property(args[0], name).is_undefined() ||
                                           !named_document_items(name).empty());
                 }));
    return value::object(cx.allocate<script::proxy_object>(target, value::object(handler)));
}

} // namespace ctbrowser::shell
