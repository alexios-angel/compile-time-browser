//===- named_access.cpp - Named access on the Document ----------*- C++ -*-===//
//
// dom_bindings' implementation of HTML 3.1.5, "named access on the Document
// object": `document.someName` answering with the element that carries that
// name, through a Proxy wrapped around the document object.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

// ============================================================================
// Named access on the document
// ============================================================================

/// The HTML elements, in tree order, that `document.<name>` names (HTML 3.1.5).
///
/// Sixteen WPT files under html/dom test this. It is a Proxy rather than
/// properties refreshed once per event-loop tick because those tests remove
/// an attribute and read the property back in the same statement, expecting
/// `undefined`.
///
/// The element list is not "anything with a name". It is five tags by their
/// `name` and two by their `id`, and the two are not the same two:
///
///   name= : embed, form, iframe, img, object
///   id=   : object always; img only if it also has a non-empty name
///
/// So `<img id=a name=b>` answers to both `a` and `b`; removing `name` removes
/// both, because the id route needs a name to exist; removing `id` leaves `b`
/// alone (WPT nameditem-01.html's third and fourth cases).
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

/// The document's supported property names: the same five-tags-and-two-ids
/// rule read the other way round, as `Object.getOwnPropertyNames(document)`
/// lists them. Every name appears once, in tree order of the first element
/// that contributes it, and an element that contributes both puts its id ahead
/// of its name (HTML 3.1.5).
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

/// Wraps the document object `target` in a Proxy with four traps that adds
/// named access. The two traps that read consult the target first.
///
/// That order is the specification's: a named property is a fallback for a
/// name the object does not otherwise have, so `document.forms` is the
/// collection accessor on the target and not the `<form name=forms>` on the
/// page. It also keeps the rest of these bindings working: `document.title`,
/// `document.body`, `createElement` and the Node members all live on the
/// target and are found before the walk runs.
///
/// ponytail: the walk is O(nodes) and runs on every `document.x` that is not
/// found on the target's chain, which includes `document.hasOwnProperty`.
/// `window`'s proxy makes the same trade. If it ever shows in a profile, add an
/// id and name index on the document rather than a special case here.
value dom_bindings::make_document_proxy(context & cx, value target) {
    auto * handler = cx.allocate<script::object_object>();
    handler->set(
        "get", native(cx, "get", [this](context & c, std::span<value> args) {
            if (args.size() < 2 || !args[0].is_object()) { return value::undefined(); }
            const std::string name = c.to_string(args[1]);
            // The whole chain first, not only the own properties: a named
            // item does not shadow `constructor`, `__proto__` or a null
            // `onreadystatechange` from Document.prototype. That is what WPT
            // nameditem-no-shadowing.tentative.html asks, and what keeps
            // `document.forms` the collection rather than a `<form
            // name=forms>`.
            if (const value held = c.lookup_property(args[0], name); !held.is_undefined()) {
                return held;
            }
            if (const std::vector<node_id> named = named_document_items(name); !named.empty()) {
                // One element answers as itself (or, for an `<iframe>`, as
                // its content navigable's WindowProxy, HTML 3.1.5 step 3);
                // several answer as a live HTMLCollection, cached per name.
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
    handler->set("has", native(cx, "has", [this](context & c, std::span<value> args) {
                     if (args.size() < 2 || !args[0].is_object()) { return value::boolean(false); }
                     const std::string name = c.to_string(args[1]);
                     // `'x' in document` must agree with `document.x`, or a
                     // page's feature detection and its use of the feature
                     // disagree.
                     return value::boolean(!c.lookup_property(args[0], name).is_undefined() ||
                                           !named_document_items(name).empty());
                 }));
    // The keys, for `Object.getOwnPropertyNames(document)`: the target's own
    // string keys, every one and in its order, then the supported property
    // names not already among them; WPT nameditem-names.html reads that order.
    // ponytail: symbol-keyed own properties are not reported. Rebuilding a
    // symbol from its key is the VM's business (key_value in
    // builtins/objects/operations.cpp), and nothing on a document carries one.
    // getOwnPropertyDescriptor below describes each named key, since a key an
    // object reports must be one it can describe: a legacy platform object's
    // named property is a read-only, enumerable, configurable data property
    // (WebIDL 3.9.1); the target's own properties return their own
    // descriptors.
    handler->set("ownKeys", native(cx, "ownKeys", [this](context & c, std::span<value> args) {
                     const value out = c.make_array();
                     if (args.empty() || !args[0].is_object()) { return out; }
                     auto & items = static_cast<script::array_object *>(out.as_heap())->items;
                     std::vector<std::string> keys;
                     static_cast<script::object_object *>(args[0].as_heap())
                         ->each_own_key([&keys](const std::string & key) {
                             if (!key.starts_with(symbol_key_mark)) { keys.push_back(key); }
                         });
                     for (const std::string & name : document_property_names()) {
                         if (std::ranges::find(keys, name) == keys.end()) { keys.push_back(name); }
                     }
                     for (const std::string & key : keys) { items.push_back(c.string(key)); }
                     return out;
                 }));
    handler->set("getOwnPropertyDescriptor",
                 native(cx, "getOwnPropertyDescriptor", [this](context & c, std::span<value> args) {
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
