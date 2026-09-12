// dom_bindings - named access on the Document object, HTML 3.1.5.

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

// THE SUPPORTED PROPERTY NAMES - the same five-tags-and-two-ids rule read the
// other way round, as `Object.getOwnPropertyNames(document)` lists them: every
// name once, in tree order of the FIRST element that contributes it, and an
// element that contributes both puts its id ahead of its name (HTML 3.1.5).
std::vector<std::string> dom_bindings::document_property_names() {
    std::vector<std::string> names;
    const auto txn = doc_->read();
    const atom id_attribute = atoms_->intern("id");
    const atom name_attribute = atoms_->intern("name");
    const auto add = [&names](std::string_view name) {
        if (name.empty() || std::ranges::find(names, name) != names.end()) { return; }
        names.emplace_back(name);
    };
    const auto walk = [&](auto && self, node_id at) -> void {
        const auto tagged = txn.tag(at);
        if (tagged.has_value() && txn.element_ns(at) == node_ns::html) {
            const std::string_view local = atoms_->text(*tagged);
            const std::string_view name = txn.attribute_value(at, name_attribute);
            if (local == "object" || (local == "img" && !name.empty())) {
                add(txn.attribute_value(at, id_attribute));
            }
            if (local == "embed" || local == "form" || local == "iframe" || local == "img" ||
                local == "object") {
                add(name);
            }
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return names;
}

// THE PROXY. Four traps; the two that read consult the target FIRST.
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
            const std::string name = c.to_string(args[1]);
            // THE WHOLE CHAIN FIRST, not only the own properties: a named
            // item does not shadow `constructor`, `__proto__` or a null
            // `onreadystatechange` from Document.prototype - which is what
            // nameditem-no-shadowing.tentative.html asks, and what keeps
            // `document.forms` the collection rather than a `<form
            // name=forms>`.
            if (const value held = c.lookup_property(args[0], name); !held.is_undefined()) {
                return held;
            }
            if (const std::vector<node_id> named = named_document_items(name); !named.empty()) {
                // ONE ELEMENT IS THE ELEMENT - or, for an `<iframe>`, its
                // content navigable's WindowProxy (HTML 3.1.5 step 3) -
                // and several are a live HTMLCollection.
                if (named.size() == 1) {
                    const value element = wrap(c, named.front());
                    if (doc_->read().local_name(named.front()) == "iframe") {
                        const value window = c.lookup_property(element, "contentWindow");
                        if (window.is_object_like()) { return window; }
                    }
                    return element;
                }
                if (const auto held = named_collections_.find(name);
                    held != named_collections_.end()) {
                    return held->second;
                }
                const value made =
                    make_live_collection(c, [this, name] { return named_document_items(name); });
                named_collections_.emplace(name, made);
                return made;
            }
            return value::undefined();
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
    // THE KEYS, for `Object.getOwnPropertyNames(document)`: the target's own
    // string keys, every one and in its order, then the supported property
    // names not already among them - nameditem-names.html reads that order.
    // Symbol-keyed own properties are not reported, which is a gap rather than
    // a rule: rebuilding a symbol from its key is the VM's own business
    // (key_value in builtins/objects/operations.cpp), and nothing on a
    // document carries one. And a DESCRIPTOR for each named one, since a key
    // an object reports must be one it can describe: a legacy platform
    // object's named property is a read-only, enumerable, configurable data
    // property (WebIDL 3.9.1), and the target's own answer for themselves.
    handler->set("ownKeys", native("ownKeys", [this](context & c, std::span<value> args) {
                     const value out = c.make_array();
                     if (args.empty() || !args[0].is_object()) { return out; }
                     auto & items = static_cast<script::array_object *>(out.as_heap())->items;
                     std::vector<std::string> keys;
                     static_cast<script::object_object *>(args[0].as_heap())
                         ->each_own_key([&keys](const std::string & key) {
                             if (!key.starts_with("@@")) { keys.push_back(key); }
                         });
                     for (const std::string & name : document_property_names()) {
                         if (std::ranges::find(keys, name) == keys.end()) { keys.push_back(name); }
                     }
                     for (const std::string & key : keys) { items.push_back(c.string(key)); }
                     return out;
                 }));
    handler->set("getOwnPropertyDescriptor",
                 native("getOwnPropertyDescriptor", [this](context & c, std::span<value> args) {
                     if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
                     const std::string name = c.to_string(args[1]);
                     context::property_descriptor own;
                     if (c.own_property(args[0], name, own)) {
                         return c.from_property_descriptor(own);
                     }
                     if (named_document_items(name).empty()) { return value::undefined(); }
                     return c.from_property_descriptor(context::property_descriptor::data(
                         c.lookup_property(c.global("document"), name),
                         script::attr_enumerable | script::attr_configurable));
                 }));
    return value::object(cx.allocate<script::proxy_object>(target, value::object(handler)));
}

} // namespace ctbrowser::shell
