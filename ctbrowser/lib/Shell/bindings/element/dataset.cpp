//===- dataset.cpp - element.dataset (DOMStringMap) -----------------------===//
//
// dom_bindings::install_dataset: `element.dataset`, the DOMStringMap over an
// element's `data-*` attributes (HTML 3.2.6.3), built as a proxy whose traps
// read and write the document's attributes.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

#include <ctbrowser/dom/dataset.hpp>

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_dataset(context & cx, script::object_object & obj, node_id id) {
    // The store is the proxy's target and is what `for (k in el.dataset)` and
    // Object.keys walk, so it holds the element's data-* names, refilled on every
    // read of `dataset`. DOMStringMap.prototype is put in front of it; lookups
    // still fall through to Object.prototype (WPT dataset-prototype.html).
    auto * store = cx.allocate<script::object_object>();
    auto * handler = cx.allocate<script::object_object>();
    // Finds one attribute the way the specification does: by computing
    // every supported property name from the attribute list and comparing, not
    // by mangling the key and looking that up. The two disagree exactly where
    // `dataset-delete.html` and `dataset-get.html` say they must.
    const auto value_of = [this, id](std::string_view key) -> std::optional<std::string> {
        return dataset_value(*doc_, id, key);
    };
    // A miss falls through to the target, which is how `dataset.toString` finds
    // Object.prototype's, but not past a name the refill left on the target.
    // The store holds the element's data-* names so that enumeration can walk
    // them, and a page that kept `var ds = el.dataset` across a
    // `removeAttribute` must still read undefined out of it: the document is
    // the map, and the target is a list of its keys as of the last read.
    const auto refilled_key = [store](const std::string & key) {
        return store->find(key) != nullptr;
    };
    set_method(cx, *handler, "get", [value_of, refilled_key](context & c, std::span<value> args) {
        if (args.size() < 2) { return value::undefined(); }
        const std::string key = c.to_string(args[1]);
        if (const std::optional<std::string> found = value_of(key)) { return c.string(*found); }
        if (refilled_key(key)) { return value::undefined(); }
        return c.lookup_property(args[0], key);
    });
    set_method(cx, *handler, "has", [value_of, refilled_key](context & c, std::span<value> args) {
        if (args.size() < 2) { return value::boolean(false); }
        const std::string key = c.to_string(args[1]);
        if (value_of(key)) { return value::boolean(true); }
        if (refilled_key(key)) { return value::boolean(false); }
        return value::boolean(!c.lookup_property(args[0], key).is_undefined());
    });
    set_method(cx, *handler, "set", [this, id](context & c, std::span<value> args) {
        if (args.size() < 3) { return value::boolean(false); }
        const std::string key = c.to_string(args[1]);
        std::string name;
        switch (dataset_attribute_of(key, name)) {
        case dataset_fault::syntax:
            // "If name contains a U+002D followed by an ASCII lower alpha,
            // throw a SyntaxError" - because that key is not one this map could
            // ever hand back, `data--foo` reading as `Foo` and not as `-foo`.
            throw_dom_exception(c, "SyntaxError",
                                "dataset: '" + key + "' is not a name a data- attribute can have");
            return value::boolean(false);
        case dataset_fault::character:
            throw_dom_exception(c, "InvalidCharacterError",
                                "dataset: '" + name + "' is not a valid attribute name");
            return value::boolean(false);
        case dataset_fault::none: break;
        }
        // `name` is already lowercase by construction, so it is interned as
        // written rather than folded: the only characters the mangle can emit
        // above 'z' are ones it copied from the key, and folding them would
        // change the author's text.
        //
        // Set in the null namespace explicitly: plain set_attribute changes the
        // first attribute with that local name in any namespace, but a data-*
        // attribute is a null-namespace attribute by definition (WPT
        // custom-attrs.html checks this), the same rule `value_of` reads by.
        (void)doc_->set_attribute_ns(id, atoms_->intern(""), atoms_->intern(name),
                                     c.to_string(args[2]));
        mutated();
        return value::boolean(true);
    });
    // `delete el.dataset.fooBar` removes `data-foo-bar`: a supported property
    // name runs the named property deleter (HTML 3.2.6.3: uppercase becomes
    // `-` and lowercase, `data-` in front, remove the attribute); anything
    // else is an ordinary delete on the target, which is how `dataset['-foo']`
    // leaves `data--foo` alone: that attribute answers to `Foo`, not `-foo`.
    set_method(cx, *handler, "deleteProperty",
               [this, id, value_of](context & c, std::span<value> args) {
                   if (args.size() < 2) { return value::boolean(false); }
                   const std::string key = c.to_string(args[1]);
                   if (!value_of(key)) {
                       return value::boolean(c.delete_own_property(args[0], key));
                   }
                   std::string name;
                   (void)dataset_attribute_of(key, name);
                   (void)doc_->remove_attribute_ns(id, "", name);
                   mutated();
                   return value::boolean(true);
               });
    // A supported name is an own data property of the map, with writable,
    // enumerable and configurable all true (Web IDL 3.9.3 step 2.3). It is read
    // off the document, not the store, because a page may set the attribute
    // after taking `dataset` and ask at once (WPT dataset-binding.window.js).
    set_method(cx, *handler, "getOwnPropertyDescriptor",
               [value_of, refilled_key](context & c, std::span<value> args) {
                   if (args.size() < 2) { return value::undefined(); }
                   const std::string key = c.to_string(args[1]);
                   context::property_descriptor found;
                   if (const std::optional<std::string> held = value_of(key)) {
                       found = context::property_descriptor::data(
                           c.string(*held), script::attr_writable | script::attr_enumerable |
                                                script::attr_configurable);
                       return c.from_property_descriptor(found);
                   }
                   if (refilled_key(key)) { return value::undefined(); }
                   return c.own_property(args[0], key, found) ? c.from_property_descriptor(found)
                                                              : value::undefined();
               });
    const value proxy = value::object(
        cx.allocate<script::proxy_object>(value::object(store), value::object(handler)));
    // `dataset` is an accessor so the target can be refilled before the page
    // sees it: setAttribute('data-...') bypasses this object, so a store filled
    // once would list stale names. Values are refilled with the keys because
    // Object.keys and JSON.stringify read them off the target; single reads go
    // through the `get` trap, which asks the document.
    auto * reader = cx.allocate<script::native_object>(
        "dataset", [this, id, store, proxy](context & c, std::span<value>) {
            if (!store->prototype.is_object()) {
                const value map = interface_prototype("DOMStringMap");
                if (map.is_object()) { store->prototype = map; }
            }
            std::vector<std::string> stale;
            stale.reserve(store->props.size());
            for (const auto & [key, held] : store->props) { stale.push_back(key); }
            for (const std::string & key : stale) { (void)store->erase(key); }
            for (const auto & [name, text] : dataset_entries(*doc_, id)) {
                store->set(name, c.string(text));
            }
            return proxy;
        });
    // The proxy is reachable only from that lambda, and a lambda's captures are
    // not a GC edge, but `retained` is. Without this the map is collected out from
    // under an element nothing else refers to.
    reader->retained.push_back(proxy);
    obj.define_accessor("dataset", value::object(reader), value::undefined());
}

} // namespace ctbrowser::shell
