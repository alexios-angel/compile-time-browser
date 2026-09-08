// ctbrowser.script context - the prototype chain and the object-shape
// operations the opcodes call: own keys, accessors, spread, `in`, `instanceof`.
//
// One of six files carved out of a 1,396-line vm/objects.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/vm.hpp>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

value context::own_keys(value source) {
    value out = make_array();
    auto * keys = static_cast<array_object *>(out.as_heap());
    if (source.is_object()) {
        // In DEFINITION ORDER, data and accessors interleaved - which is what a
        // page sees from for-in and has to match Object.keys. for-in
        // enumerates STRING keys only, and ENUMERABLE ones only: 13.7.5.15
        // filters on [[Enumerable]], which is why a built-in method never turns
        // up in a `for (k in Math)`.
        static_cast<object_object *>(source.as_heap())
            ->each_own_enumerable_key(
                [&](const std::string & name) { keys->items.push_back(string(name)); });
    } else if (source.is_array()) {
        const std::size_t n = static_cast<array_object *>(source.as_heap())->items.size();
        for (std::size_t i = 0; i < n; ++i) { keys->items.push_back(string(std::to_string(i))); }
    }
    return out;
}

void context::define_accessor(value target, const std::string & name, value getter, value setter) {
    if (target.is_object()) {
        static_cast<object_object *>(target.as_heap())->define_accessor(name, getter, setter);
    } else if (target.is_kind(heap_kind::function)) {
        // a `static get` on a class, which IS the constructor closure
        static_cast<closure_object *>(target.as_heap())->define_accessor(name, getter, setter);
    }
}

void context::copy_own_properties(value target, value source) {
    if (!target.is_object()) { return; }
    auto * into = static_cast<object_object *>(target.as_heap());
    if (source.is_object()) {
        // A COPY OF THE SOURCE'S ENTRIES FIRST: `set` can reallocate the
        // target's storage, and target and source may be the same object.
        //
        // ENUMERABLE OWN PROPERTIES ONLY. `{...o}` is CopyDataProperties
        // (7.3.25), which skips a non-enumerable one - so spreading a class
        // instance no longer drags its prototype's plumbing along.
        //
        // A SYMBOL KEY IS COPIED, unlike in Object.keys or for-in:
        // CopyDataProperties takes OwnPropertyKeys, which reports both. That is
        // the one place the enumerable walk must NOT filter symbols, and
        // filtering them cost four tests (…/spread-obj-symbol-property.js).
        //
        // ...and an accessor is READ rather than copied: the spec does a Get,
        // so what lands on the target is the getter's answer as a data property.
        std::vector<std::pair<std::string, value>> entries;
        auto * from = static_cast<object_object *>(source.as_heap());
        from->each_own_entry([&](const std::string & name, std::uint8_t attrs) {
            if ((attrs & attr_enumerable) != 0) {
                entries.emplace_back(name, lookup_property(source, name));
            }
        });
        for (const auto & [name, item] : entries) { into->set(name, item); }
    } else if (source.is_array()) {
        const std::vector<value> items = static_cast<array_object *>(source.as_heap())->items;
        for (std::size_t i = 0; i < items.size(); ++i) { into->set(std::to_string(i), items[i]); }
    }
}

value context::get_prototype(value target) {
    return target.is_object() ? static_cast<object_object *>(target.as_heap())->prototype
                              : value::undefined();
}

void context::set_prototype(value target, value proto) {
    if (target.is_object()) { static_cast<object_object *>(target.as_heap())->prototype = proto; }
}

bool context::has_property(value target, value key) {
    // A PROXY ANSWERS `in` ITSELF, or hands it to the target.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = proxy_trap(target, "has");
        if (trap.is_callable()) {
            const value args[2] = {p->target, key};
            return truthy(call(trap, args, p->handler));
        }
        return !lookup_property(p->target, to_string(key)).is_undefined();
    }
    // --- HasProperty, 7.3.11: THE WHOLE CHAIN, not the own table ----------
    //
    // `in` used to answer about own DATA properties of an object_object and
    // about array indices, and about nothing else - so `'toString' in {}` was
    // false, `'x' in obj` was false for an accessor, `'length' in [1]` was
    // false, and an inherited property was invisible to the operator whose
    // entire job is to see one. It is also what ToPropertyDescriptor asks with,
    // which is how a descriptor object built by `new Con()` over a prototype
    // carrying a `writable` getter described nothing at all.
    //
    // own_property is the shared [[GetOwnProperty]] over all four tables, so
    // this is that walked up the chain: the explicit prototype links first,
    // then the implicit tables property lookup falls back to.
    const std::string name = to_string(key);
    property_descriptor found;
    if (own_property(target, name, found)) { return true; }
    value link = target.is_object() ? static_cast<object_object *>(target.as_heap())->prototype
                                    : value::undefined();
    // A depth cap because a page can make the chain cyclic, exactly as
    // lookup_property does.
    for (int depth = 0; depth < 64 && link.is_object(); ++depth) {
        if (own_property(link, name, found)) { return true; }
        link = static_cast<object_object *>(link.as_heap())->prototype;
    }
    for (object_object * table : implicit_prototypes(target)) {
        if (table != nullptr &&
            (table->find(name) != nullptr || table->find_accessor(name) != nullptr)) {
            return true;
        }
    }
    // ...and a function's STATIC chain, which is a third kind of link again.
    for (value up = target; up.is_callable();) {
        if (up.is_kind(heap_kind::function)) {
            auto * fn = static_cast<closure_object *>(up.as_heap());
            up = fn->proto_link;
        } else if (up.is_kind(heap_kind::native)) {
            up = value::null();
        } else {
            break;
        }
        if (!up.is_callable()) { break; }
        if (own_property(up, name, found)) { return true; }
    }
    return false;
}

bool context::instance_of(value target, value ctor) {
    value wanted = value::undefined();
    if (ctor.is_kind(heap_kind::function)) {
        wanted = ensure_prototype(ctor);
    } else if (ctor.is_kind(heap_kind::native)) {
        // A BUILT-IN constructor is a native, and every one of them is now
        // something a page can extend - `class E extends Error`. Without this,
        // `e instanceof Error` was false for every one.
        if (value * p = static_cast<native_object *>(ctor.as_heap())->find("prototype")) {
            wanted = *p;
        }
    } else if (ctor.is_object()) {
        if (value * p = static_cast<object_object *>(ctor.as_heap())->find("prototype")) {
            wanted = *p;
        }
    }
    if (!wanted.is_object()) { return false; }
    // A PROXY IS ASKED THROUGH TO ITS TARGET. 7.3.21 OrdinaryHasInstance calls
    // `[[GetPrototypeOf]]`, and a proxy's forwards to the object behind it - so
    // `x instanceof C` for a proxy is a question about that object's chain and
    // not about the proxy, which has no prototype field at all. `window` is a
    // proxy and so is every live DOM collection, and `document.images
    // instanceof HTMLCollection` was false for exactly this reason while the
    // target carried the right prototype the whole time. Bounded, because a
    // proxy of a proxy is legal and a cycle must not be.
    value subject = target;
    for (int hops = 0; hops < 8 && subject.is_kind(heap_kind::proxy); ++hops) {
        subject = static_cast<proxy_object *>(subject.as_heap())->target;
    }
    // The EXPLICIT chain first - a page's own classes, and every builtin whose
    // instances carry a prototype (Error, Map, Blob).
    value link = subject.is_object() ? static_cast<object_object *>(subject.as_heap())->prototype
                                     : value::undefined();
    for (int depth = 0; depth < 64 && link.is_object(); ++depth) {
        if (link.as_heap() == wanted.as_heap()) { return true; }
        link = static_cast<object_object *>(link.as_heap())->prototype;
    }
    // Then the IMPLICIT one. An array, a function, a string and a plain object
    // have no prototype field to walk - their chain is the tables property
    // lookup falls back to - so instanceof answered false for every builtin
    // while answering correctly for a page's own classes.
    //
    // OBJECT-LIKE ONLY. `5 instanceof Number` and `'x' instanceof String` are
    // FALSE in JavaScript however many methods a primitive resolves -
    // instanceof asks about a prototype chain and a primitive does not have
    // one. Applying the fallback to everything made both of those true, which
    // is the mirror image of the bug being fixed.
    if (subject.is_object_like()) {
        for (object_object * table : implicit_prototypes(subject)) {
            if (table != nullptr && table == wanted.as_heap()) { return true; }
        }
    }
    return false;
}

} // namespace ctbrowser::script
