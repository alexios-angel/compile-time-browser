// ctbrowser.script context - property reads: `lookup_index` and
// `lookup_property`, the two hottest reads in the engine.
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

value context::lookup_index(value target, value key) {
    if (target.is_array() && key.is_number()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        const auto i = static_cast<std::ptrdiff_t>(key.as_number());
        if (arr->is_view()) {
            return i >= 0 && static_cast<std::size_t>(i) < arr->length()
                       ? value::number(view_get(*arr, static_cast<std::size_t>(i)))
                       : value::undefined();
        }
        if (i >= 0 && static_cast<std::size_t>(i) < arr->items.size()) {
            return arr->items[static_cast<std::size_t>(i)];
        }
        // AND THE SPARSE HALF, second and behind an empty test so that reading
        // past the end of an ordinary array is the one branch it was before.
        if (!arr->sparse.empty() && i >= 0 && static_cast<std::uint64_t>(i) <= 4294967295ull) {
            if (value * found = arr->find_sparse(static_cast<std::uint32_t>(i))) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_string() && key.is_number()) {
        const std::string & text = static_cast<string_object *>(target.as_heap())->text;
        const auto i = static_cast<std::size_t>(key.as_number());
        return i < text.size() ? string(std::string{text[i]}) : value::undefined();
    }
    return lookup_property(target, to_string(key));
}

value context::lookup_property(value target, const std::string & name) {
    // A PROXY ANSWERS FIRST, or hands the question to its target. This sits at
    // the top because a proxy's whole purpose is to be asked before anything
    // else looks at the object underneath it.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        const value trap = proxy_trap(target, "get");
        if (trap.is_callable()) {
            const value args[3] = {p->target, string(name), target};
            return call(trap, args, p->handler);
        }
        return lookup_property(p->target, name);
    }
    // Own properties first: a page that writes `arr.length = 0` or shadows a
    // method on one object must not be overridden by the prototype.
    if (target.is_object()) {
        // Own properties, then the object's OWN prototype chain (what a class
        // instance uses to find its methods), then the shared table. A depth
        // cap because a page can make the chain cyclic and a lookup must not
        // hang because of it.
        auto * obj = static_cast<object_object *>(target.as_heap());
        // HASHED ONCE FOR THE WHOLE CHAIN. Every level below is asked for the
        // same name, and each `find` used to hash it again.
        const prehashed_name key{name, hash_name(name)};
        for (int depth = 0; obj != nullptr && depth < 64; ++depth) {
            if (value * found = obj->find(key)) { return *found; }
            // An accessor found anywhere on the chain is CALLED, with the
            // original target as its receiver - a getter defined on a prototype
            // reads the instance, which is the entire point of putting one
            // there. The has_accessors test is why this costs nothing on the
            // objects that have none, which is nearly all of them.
            if (accessor_entry * entry = obj->find_accessor(name)) {
                if (entry->getter.is_callable()) {
                    return call(entry->getter, std::span<const value>{}, target);
                }
                return value::undefined(); // set-only: reading gives undefined
            }
            obj = obj->prototype.is_object()
                      ? static_cast<object_object *>(obj->prototype.as_heap())
                      : nullptr;
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(key)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        // js_length, NOT length(): an index too far out to materialise raises
        // `length` without allocating for it, and this is the one read that has
        // to see that. Everything else keeps items.size() and its bounds.
        if (name == "length") { return value::number(static_cast<double>(arr->js_length())); }
        // WHAT A VIEW KNOWS ABOUT ITS BUFFER. `new Uint8Array(f32.buffer)` is
        // how a page makes a second view of a different width over storage it
        // already has - Phaser does exactly that - and it needs `buffer` to
        // hand back something the constructor recognises as one.
        if (arr->is_view()) {
            const auto width = bytes_per_element(arr->elements);
            if (name == "byteLength") {
                return value::number(static_cast<double>(arr->view_length * width));
            }
            if (name == "byteOffset") { return value::number(arr->byte_offset); }
            if (name == "BYTES_PER_ELEMENT") { return value::number(static_cast<double>(width)); }
            if (name == "buffer") {
                value made = make_object();
                auto * buffer = static_cast<object_object *>(made.as_heap());
                const auto * bytes = static_cast<const array_object *>(arr->viewed.as_heap());
                buffer->set("byteLength", value::number(static_cast<double>(bytes->items.size())));
                buffer->set("length", value::number(static_cast<double>(bytes->items.size())));
                buffer->set("__bytes", arr->viewed);
                return made;
            }
        }
        if (arr->is_match) { // an exec() result carries index/input/groups
            if (name == "index") { return arr->index; }
            if (name == "input") { return arr->input; }
            if (name == "groups") { return arr->groups; }
        }
        // A TYPED array's own methods first, then every array's, then every
        // object's - which is the chain JavaScript actually has, and the reason
        // `[1,2].hasOwnProperty(...)` and `bytes.subarray(...)` both work.
        if (arr->elements != element_kind::none) {
            if (object_object * table = prototype(proto_kind::typed_array)) {
                if (value * found = table->find(name)) { return *found; }
            }
        }
        if (object_object * table = prototype(proto_kind::array)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_string()) {
        auto * str = static_cast<string_object *>(target.as_heap());
        if (name == "length") { return value::number(static_cast<double>(str->text.size())); }
        if (object_object * table = prototype(proto_kind::string)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    // A BigInt is a primitive with methods, exactly as a number is - and it
    // reaches its prototype the same way, because it has no own properties for
    // a lookup to find first. Without this `(255n).toString(16)` read
    // undefined and called it.
    if (target.is_kind(heap_kind::bigint)) {
        if (object_object * table = prototype(proto_kind::bigint)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_number()) {
        if (object_object * table = prototype(proto_kind::number)) {
            if (value * found = table->find(name)) { return *found; }
        }
        // ...THEN Object.prototype, because `Number.prototype`'s own prototype
        // IS Object.prototype and a primitive boxes on property access. Without
        // this `(5).hasOwnProperty(...)` read undefined - and library code does
        // exactly that on values whose type it has not checked. Phaser's tween
        // manager asks `hasOwnProperty` of a number while working out which
        // properties of a target to animate.
        //
        // NUMBERS, BOOLEANS AND STRINGS ALL LACKED THIS. Only arrays chained to
        // Object.prototype, and the comment there says it is "the chain
        // JavaScript actually has" - which was true of arrays and of nothing
        // else. The regression test asserted the string case on the assumption
        // it already worked, and it did not.
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    // A boolean is a value with methods too. `flag.toString()` is what a
    // template literal and a string concatenation both do underneath, and code
    // that calls it explicitly - to build a cache key, say - found nothing.
    if (target.is_boolean()) {
        if (object_object * table = prototype(proto_kind::boolean)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(target.as_heap());
        if (accessor_entry * entry = fn->find_accessor(name)) {
            return entry->getter.is_callable()
                       ? call(entry->getter, std::span<const value>{}, target)
                       : value::undefined();
        }
        if (value * found = fn->find(name)) { return *found; }
        // A BUILT-IN FUNCTION HAS A NAME, and it was undefined for every one
        // that is not a constructor - so test262's own assert.throws printed
        // "Expected a undefined to be thrown", 2,670 times, because it builds
        // its message out of `expectedErrorConstructor.name`. It lives on the
        // C++ object rather than in the table, which is why it is answered here
        // rather than installed on 400 natives.
        if (name == "name" && !fn->name_erased) { return string(fn->name); }
        // STATIC INHERITANCE through the constructor's own [[Prototype]] - the
        // same walk a closure does. `TypeError.__proto__` is `Error`, so a
        // static installed on Error is found through all six NativeErrors.
        for (value up = fn->proto_link; up.is_object() || up.is_callable();) {
            if (up.is_kind(heap_kind::native)) {
                auto * parent = static_cast<native_object *>(up.as_heap());
                if (accessor_entry * entry = parent->find_accessor(name)) {
                    return entry->getter.is_callable()
                               ? call(entry->getter, std::span<const value>{}, target)
                               : value::undefined();
                }
                if (value * found = parent->find(name)) { return *found; }
                up = parent->proto_link;
                continue;
            }
            if (up.is_object()) {
                if (value * found = static_cast<object_object *>(up.as_heap())->find(name)) {
                    return *found;
                }
            }
            break;
        }
        // ...then Function.prototype, so `nativeFn.call(...)` works too.
        if (object_object * table = prototype(proto_kind::function)) {
            if (value * found = table->find(name)) { return *found; }
        }
        // ...AND THEN Object.prototype, because Function.prototype's own
        // [[Prototype]] is Object.prototype. Without it `f.hasOwnProperty` and
        // `f.propertyIsEnumerable` were undefined on every function, which is
        // the same gap numbers, booleans and strings had until they were fixed
        // and functions were left out of.
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_kind(heap_kind::symbol)) {
        auto * sym = static_cast<symbol_object *>(target.as_heap());
        if (name == "description") { return string(sym->description); }
        if (object_object * table = prototype(proto_kind::symbol)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return value::undefined();
    }
    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        if (value * found = closure->find(name)) { return *found; }
        if (name == "prototype") { return ensure_prototype(target); }
        // `f.name` and `f.length`, read off the compiled function rather than
        // stored on every closure - most functions are never asked and an extra
        // two properties each is a real cost on a 4,754-function bundle.
        //
        // `name` is not cosmetic: identifying a value by
        // `Object.getPrototypeOf(x).constructor.name` is the standard walk that
        // works where instanceof does not, and an undefined name compares equal
        // to the other undefined it is being tested against - so a nameless
        // class reported a MATCH against anything else with no name.
        if (closure->proto != nullptr) {
            if (name == "name") { return string(closure->proto->name); }
            if (name == "length") { return value::number(closure->proto->param_count); }
        }
        // `static get w()` on a class - the constructor IS the closure, so its
        // accessors live here rather than on any object.
        if (accessor_entry * entry = closure->find_accessor(name)) {
            if (entry->getter.is_callable()) {
                return call(entry->getter, std::span<const value>{}, target);
            }
            return value::undefined();
        }
        // STATIC INHERITANCE: `class D extends B` makes D's own [[Prototype]]
        // B, so `D.staticMethod` finds B's. Babel wires this by hand with
        // _setPrototypeOf, and a real `extends` should do the same.
        for (value up = closure->proto_link; up.is_callable();) {
            if (up.is_kind(heap_kind::function)) {
                auto * parent = static_cast<closure_object *>(up.as_heap());
                if (value * found = parent->find(name)) { return *found; }
                up = parent->proto_link;
                continue;
            }
            auto * parent = static_cast<native_object *>(up.as_heap());
            if (accessor_entry * entry = parent->find_accessor(name)) {
                return entry->getter.is_callable()
                           ? call(entry->getter, std::span<const value>{}, target)
                           : value::undefined();
            }
            if (value * found = parent->find(name)) { return *found; }
            break;
        }
        // A FUNCTION IS AN OBJECT WITH A PROTOTYPE OF ITS OWN. `call`, `apply`
        // and `bind` live there, and p5.js cannot install a single event
        // listener without bind. Then Object.prototype, which is
        // Function.prototype's own [[Prototype]] - see the native arm above.
        if (object_object * table = prototype(proto_kind::function)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = prototype(proto_kind::object)) {
            if (value * found = table->find(name)) { return *found; }
        }
    }
    return value::undefined();
}

} // namespace ctbrowser::script
