// dom_bindings - `element.dataset`, the DOMStringMap over `data-*` attributes.

#include "internal.hpp"

#include <ctbrowser/dom/dataset.hpp>

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_dataset(context & cx, script::object_object & obj, node_id id) {
    // THE STORE IS THE PROXY'S TARGET, and it is what `for (k in el.dataset)`
    // and `Object.keys` walk - so it holds the element's data-* names, refilled
    // on every read of `dataset` below. `DOMStringMap.prototype` goes in front
    // of it, which costs nothing: a prototype chain that ends in undefined
    // falls through to the builtin Object.prototype tables anyway, so
    // `dataset-prototype.html`'s "Properties on Object.prototype should shine
    // through" still holds.
    auto * store = cx.allocate<script::object_object>();
    auto * handler = cx.allocate<script::object_object>();
    // ONE ATTRIBUTE, FOUND THE WAY THE SPECIFICATION FINDS IT: by computing
    // every supported property name from the attribute list and comparing, not
    // by mangling the key and looking that up. The two disagree exactly where
    // `dataset-delete.html` and `dataset-get.html` say they must.
    const auto value_of = [this, id](std::string_view key) -> std::optional<std::string> {
        return dataset_value(*doc_, id, key);
    };
    // A MISS FALLS THROUGH TO THE TARGET, which is how `dataset.toString` finds
    // Object.prototype's - but NOT past a name the refill left on the target.
    // The store holds the element's data-* names so that enumeration can walk
    // them, and a page that kept `var ds = el.dataset` across a
    // `removeAttribute` must still read undefined out of it: the DOCUMENT is
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
        // ALREADY LOWERCASE by construction, so this interns as written rather
        // than folding: the only characters the mangle can emit above 'z' are
        // the ones it copied, and folding them would be folding the author's.
        //
        // IN NO NAMESPACE, EXPLICITLY. The qualified `set_attribute` changes the
        // FIRST attribute with that name whatever namespace it is in, so an
        // element already carrying `data-my-custom-attr` in two namespaces of
        // its own had one of THOSE rewritten instead of gaining a third
        // attribute - which is `custom-attrs.html`, whole and entire. A
        // data-* attribute is a null-namespace attribute by definition: it is
        // the same rule `value_of` above reads by.
        (void)doc_->set_attribute_ns(id, atoms_->intern(""), atoms_->intern(name),
                                     c.to_string(args[2]));
        mutated();
        return value::boolean(true);
    });
    // `delete el.dataset.fooBar` REMOVES `data-foo-bar` - a supported property
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
    // A supported name is an own DATA property of the map - { value, writable,
    // enumerable, configurable } all true (Web IDL 3.9.3 step 2.3) - read off
    // the DOCUMENT, not the store: `dataset-binding.window.js` sets the
    // attribute after taking `dataset` and asks at once.
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
    // AN ACCESSOR, so the target can be refilled before the page sees it.
    // `d.setAttribute('data-foo', 'v')` does not go through this object at all,
    // so a store filled once at install would enumerate whatever the element
    // carried when it was first wrapped - and the corpus sets the attributes
    // AFTER reading nothing out of `dataset`. The values are refilled with the
    // keys because `Object.keys` and JSON.stringify read them off the target;
    // a read of one property still goes through the `get` trap, which asks the
    // document, so nothing here can go stale between two statements.
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
    // THE PROXY IS REACHABLE ONLY FROM THAT LAMBDA, and a lambda's captures are
    // not a GC edge - `retained` is. Without this the map is collected out from
    // under an element nothing else refers to.
    reader->retained.push_back(proxy);
    obj.define_accessor("dataset", value::object(reader), value::undefined());
}

} // namespace ctbrowser::shell
