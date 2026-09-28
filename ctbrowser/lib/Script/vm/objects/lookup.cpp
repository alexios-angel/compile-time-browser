// ctbrowser.script context - property reads: `lookup_index` and
// `lookup_property`, the two hottest reads in the engine.
//
// One of six files carved out of a 1,396-line vm/objects.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

namespace {

// An accessor hit on the read path: the getter runs with the ORIGINAL
// receiver, whichever prototype the entry was found on; a set-only accessor
// reads as undefined.
value call_getter(context & cx, const accessor_entry & entry, value receiver) {
    return entry.getter.is_callable() ? cx.call(entry.getter, std::span<const value>{}, receiver)
                                      : value::undefined();
}

} // namespace

// `Uint8Array.prototype` for u8: the constructor's own prototype object, found
// through the global it is installed as (value.hpp, typed_array_global_name),
// or null before install_typed_arrays has run. A free function with external
// linkage - vm.hpp is not widened for it - declared again by the two other
// files that ask (chain.cpp's instance_of, builtins/objects/operations.cpp's
// prototype_of).
object_object * typed_array_prototype(context & cx, element_kind kind) {
    const char * name = typed_array_global_name(kind);
    if (name == nullptr) { return nullptr; }
    const value ctor = cx.global(name);
    if (!ctor.is_kind(heap_kind::native)) { return nullptr; }
    value * proto = static_cast<native_object *>(ctor.as_heap())->find("prototype");
    return proto != nullptr && proto->is_object() ? static_cast<object_object *>(proto->as_heap())
                                                  : nullptr;
}

namespace {

// OrdinaryGet step 3 on an array (10.1.8.1): the [[Prototype]]'s [[Get]] of
// `name`, with the array as the receiver. The tail of lookup_property's array
// arm, and what an index the array does not have - a hole, or one past the
// end - reads through to.
value array_inherited(context & cx, value target, const array_object & arr,
                      const std::string & name) {
    // AN EXPLICIT [[Prototype]] - a subclass instance's, or one a page set -
    // is the whole chain from here: it reaches Array.prototype (or the kind's)
    // through its own links. See array_object::prototype.
    if (!arr.prototype.is_null()) [[unlikely]] {
        if (arr.prototype.is_object()) {
            return cx.lookup_along(static_cast<object_object *>(arr.prototype.as_heap()), target,
                                   name);
        }
        if (arr.prototype.is_heap() && !arr.prototype.is_string()) {
            return cx.lookup_property(arr.prototype, name);
        }
        return value::undefined(); // an explicit null
    }
    // A TYPED array's own methods first, then every array's, then every
    // object's - which is the chain JavaScript actually has, and the reason
    // `[1,2].hasOwnProperty(...)` and `bytes.subarray(...)` both work.
    if (arr.elements != element_kind::none) {
        // The kind's own prototype (23.2.7, reached through its global),
        // then %TypedArray%.prototype behind it.
        for (object_object * table = typed_array_prototype(cx, arr.elements); table != nullptr;) {
            if (value * found = table->find(name)) { return *found; }
            if (accessor_entry * entry = table->find_accessor(name)) {
                return call_getter(cx, *entry, target);
            }
            table = table->prototype.is_object()
                        ? static_cast<object_object *>(table->prototype.as_heap())
                        : nullptr;
        }
        if (object_object * table = cx.prototype(context::proto_kind::typed_array)) {
            if (value * found = table->find(name)) { return *found; }
        }
        if (object_object * table = cx.prototype(context::proto_kind::array)) {
            if (value * found = table->find(name)) { return *found; }
        }
        return cx.from_object_prototype(target, name);
    }
    // Array.prototype's accessors as well as its data: a getter defined there
    // for an index is what a hole in every array reads.
    if (object_object * table = cx.prototype(context::proto_kind::array)) {
        return cx.lookup_along(table, target, name);
    }
    return cx.from_object_prototype(target, name);
}

} // namespace

std::optional<context::property_descriptor> context::find_private_element(value target,
                                                                          const std::string & key) {
    if (!target.is_object_like()) { return std::nullopt; }
    property_descriptor found;
    if (own_property(target, key, found)) { return found; }
    // A method/accessor definition stays on its declaration home. The own
    // brand retains that home even when the receiver has a different prototype.
    // Its storage key is separate from fields, so the home is not itself branded.
    const std::size_t colon = key.rfind(':');
    if (colon == std::string::npos) { return std::nullopt; }
    const std::string brand = std::string{private_key_prefix} + key.substr(colon);
    if (!own_property(target, brand, found) ||
        (!found.held.is_object() && !found.held.is_kind(heap_kind::function))) {
        return std::nullopt;
    }
    const value home = found.held;
    if (!own_property(home, key + ":method", found)) { return std::nullopt; }
    return found;
}

value context::key_value(const std::string & key) {
    if (key.starts_with(symbol_key_prefix)) {
        const std::size_t at = key.find(':', symbol_key_prefix.size());
        return value::object(allocate<symbol_object>(
            at == std::string::npos ? std::string{} : key.substr(at + 1), key));
    }
    if (key.starts_with(registered_symbol_key_prefix)) {
        return value::object(
            allocate<symbol_object>(key.substr(registered_symbol_key_prefix.size()), key));
    }
    if (key.starts_with(symbol_key_mark)) {
        // A well-known symbol's [[Description]] is "Symbol.iterator" (6.1.5.1).
        return value::object(
            allocate<symbol_object>("Symbol." + key.substr(symbol_key_mark.size()), key));
    }
    // A string key, or a private name's - which only the VM's own paths
    // (a private accessor's SetFunctionName) ever ask for.
    return minted_string(key);
}

value context::get_with_receiver(value base, const std::string & name, value receiver) {
    int depth = 0;
    for (value at = base; at.is_object_like() && depth < 64; ++depth) {
        if (at.is_kind(heap_kind::proxy)) {
            auto * p = static_cast<proxy_object *>(at.as_heap());
            bool failed = false;
            const value trap = proxy_trap(at, "get", &failed);
            if (failed || throw_pending()) { return value::undefined(); }
            if (trap.is_callable()) {
                const value args[3] = {p->target, key_value(name), receiver};
                return call(trap, args, p->handler);
            }
            at = p->target;
            continue;
        }
        property_descriptor found;
        if (own_property(at, name, found)) {
            if (!found.is_accessor()) { return found.held; }
            return found.getter.is_callable() ? call(found.getter, {}, receiver)
                                              : value::undefined();
        }
        // The implicit tables (Object.prototype and the like) sit past the
        // explicit chain: lookup_property walks them, with `at` as the
        // receiver, which no getter of theirs observes.
        const value up = get_prototype(at);
        if (!up.is_object_like()) { return lookup_property(at, name); }
        at = up;
    }
    return value::undefined();
}

value context::lookup_index(value target, value key) {
    if (target.is_array() && key.is_number()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        // A Number that is not an index is the property its ToString names:
        // `a[1.5]` is a["1.5"], not a[1] - and on a typed array a numeric key
        // that names no element is undefined, never the chain (10.4.5.4).
        if (!array_index_number(key.as_number())) {
            if (arr->elements != element_kind::none) { return value::undefined(); }
            return lookup_property(target, to_string(key));
        }
        const auto i = static_cast<std::ptrdiff_t>(key.as_number());
        if (arr->is_view()) {
            if (i < 0 || static_cast<std::size_t>(i) >= arr->length()) {
                return value::undefined();
            }
            // A BigInt kind's element is a fresh bigint off the bytes; every
            // other kind's is view_get's double.
            return is_bigint_kind(arr->elements)
                       ? typed_element_get(*this, *arr, static_cast<std::size_t>(i))
                       : value::number(view_get(*arr, static_cast<std::size_t>(i)));
        }
        if (i >= 0 && static_cast<std::size_t>(i) < arr->items.size()) {
            // A HOLE READS THROUGH TO THE PROTOTYPES and an ACCESSOR element
            // calls its getter - see array_object::element_attrs and
            // holes_begin, both empty for every ordinary array and costing
            // this one test.
            if (!arr->element_attrs.empty() || arr->has_hole_run()) [[unlikely]] {
                const auto at = static_cast<std::uint32_t>(i);
                if (arr->is_hole(at)) {
                    return array_inherited(*this, target, *arr, std::to_string(at));
                }
                if (const std::uint8_t * e = arr->find_element_attrs(at);
                    e != nullptr && (*e & array_object::elem_accessor) != 0 && arr->named) {
                    if (accessor_entry * entry = arr->named->find_accessor(std::to_string(at))) {
                        return call_getter(*this, *entry, target);
                    }
                }
            }
            return arr->items[static_cast<std::size_t>(i)];
        }
        // AND THE SPARSE HALF, second and behind an empty test so that reading
        // past the end of an ordinary array is the one branch it was before.
        if (!arr->sparse.empty() && i >= 0 && static_cast<std::uint64_t>(i) <= 4294967295ull) {
            if (value * found = arr->find_sparse(static_cast<std::uint32_t>(i))) { return *found; }
        }
        // AN INDEX THE ARRAY DOES NOT HAVE is OrdinaryGet's step 3 like any
        // other absent key: `Array.prototype[1] = 1; [0][1]` is 1. A typed
        // array's is undefined without asking (10.4.5.4).
        if (arr->elements != element_kind::none) { return value::undefined(); }
        return array_inherited(*this, target, *arr, std::to_string(i));
    }
    if (target.is_string() && key.is_number() && array_index_number(key.as_number())) {
        // One CODE UNIT (10.4.3.5), a pair's half included.
        const auto & str = *static_cast<const string_object *>(target.as_heap());
        const auto i = static_cast<std::size_t>(key.as_number());
        return i < str.length() ? string(str.unit_text(i)) : value::undefined();
    }
    // ToObject(base) BEFORE the key's ToPropertyKey (GetValue step 3.a; 13.3.3
    // hands the key over unconverted): `null[k]` is the TypeError without
    // running k's toString - whose own throw, left pending under this one,
    // made the pair uncatchable.
    if (target.is_nullish()) [[unlikely]] {
        const bool plain = key.is_string() || key.is_number() || key.is_boolean();
        throw_error("TypeError", "Cannot read properties of " +
                                     std::string{target.is_null() ? "null" : "undefined"} +
                                     (plain ? " (reading '" + to_string(key) + "')" : ""));
        return value::undefined();
    }
    const std::size_t unwound = unwinds();
    const bool pending = throw_pending();
    const std::string name = to_string(key);
    if ((!pending && throw_pending()) || unwinds() != unwound) { return value::undefined(); }
    return lookup_property(target, name);
}

value context::lookup_property(value target, const std::string & name) {
    // A PRIVATE NAME IS A BRAND CHECK (7.3.31 PrivateGet): an object whose
    // class did not add the element is the TypeError, not undefined. A Proxy
    // carries only its own elements, never its target's. Two byte compares
    // on the hot path; the descriptor lookup only for the keys the compiler spells.
    if (is_private_key(name)) [[unlikely]] {
        const auto found = find_private_element(target, name);
        if (!found) {
            const std::size_t colon = name.find(':');
            throw_error(
                "TypeError",
                "Cannot read private member " +
                    name.substr(1, colon == std::string::npos ? std::string::npos : colon - 1) +
                    " from an object whose class did not declare it");
            return value::undefined();
        }
        // PrivateGet step 5.b: a private ACCESSOR with no getter - only a
        // `set #x` - is the TypeError too, where an ordinary accessor with
        // no getter reads undefined.
        if (!found->is_accessor()) { return found->held; }
        if (!found->getter.is_callable()) {
            const std::size_t colon = name.find(':');
            throw_error(
                "TypeError",
                "'" + name.substr(1, colon == std::string::npos ? std::string::npos : colon - 1) +
                    "' was defined without a getter");
            return value::undefined();
        }
        return call(found->getter, {}, target);
    }
    // A PROPERTY OF null OR undefined IS A TypeError (7.3.2 GetV -> ToObject),
    // not undefined. Until 2026-09-12 it was undefined, and docs/script.md
    // recorded why that hurt: a missing object surfaced one step later under
    // the wrong name. Here rather than in op::get_prop alone because the
    // compiled tier calls this same member, so the two cannot disagree.
    if (target.is_nullish()) [[unlikely]] {
        throw_error("TypeError", "Cannot read properties of " +
                                     std::string{target.is_null() ? "null" : "undefined"} +
                                     " (reading '" + name + "')");
        return value::undefined();
    }
    // A PROXY ANSWERS FIRST, or hands the question to its target. This sits at
    // the top because a proxy's whole purpose is to be asked before anything
    // else looks at the object underneath it.
    if (target.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(target.as_heap());
        bool failed = false;
        const value trap = proxy_trap(target, "get", &failed);
        if (failed || throw_pending()) { return value::undefined(); }
        if (trap.is_callable()) {
            const value args[3] = {p->target, key_value(name), target};
            const std::size_t before = unwinds();
            const value answered = call(trap, args, p->handler);
            if (throw_pending() || unwinds() != before) { return value::undefined(); }
            // 10.5.8 steps 8-10, the invariants: over a non-configurable target
            // property the answer must be a non-writable data property's own
            // value, and undefined for an accessor with no getter.
            property_descriptor held;
            if (own_property(p->target, name, held) && held.has_configurable &&
                !held.configurable) {
                if (held.is_data() && held.has_writable && !held.writable &&
                    !held.held.same_value(answered)) {
                    throw_error("TypeError", "'get' on proxy: property '" + name +
                                                 "' is a read-only and non-configurable data "
                                                 "property on the proxy target but the proxy did "
                                                 "not return its actual value");
                    return value::undefined();
                }
                if (held.is_accessor() && !held.getter.is_callable() && !answered.is_undefined()) {
                    throw_error("TypeError",
                                "'get' on proxy: property '" + name +
                                    "' is a non-configurable accessor property on the "
                                    "proxy target and does not have a getter function, "
                                    "but the trap did not return 'undefined'");
                    return value::undefined();
                }
            }
            return answered;
        }
        // 10.5.8 step 6: target.[[Get]](P, Receiver) with the PROXY as the
        // receiver - a getter on the target sees the proxy as `this`.
        return get_with_receiver(p->target, name, target);
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
            // A STRING WRAPPER'S `length` AND INDICES are own properties off
            // its [[StringData]] slot (10.4.3.1), asked for only after the own
            // table missed - nothing in it can shadow them - and only for those
            // two shapes of name, so an ordinary object's `.length` pays one
            // comparison.
            if (depth == 0 &&
                (name == "length" || (!name.empty() && name[0] >= '0' && name[0] <= '9'))) {
                if (value * slot = primitive_slot(target); slot != nullptr && slot->is_string()) {
                    const auto & str = *static_cast<const string_object *>(slot->as_heap());
                    if (name == "length") {
                        return value::number(static_cast<double>(str.length()));
                    }
                    if (std::uint32_t at = 0;
                        object_object::array_index_key(name, at) && at < str.length()) {
                        return string(str.unit_text(at));
                    }
                }
            }
            // An accessor found anywhere on the chain is CALLED, with the
            // original target as its receiver - a getter defined on a prototype
            // reads the instance, which is the entire point of putting one
            // there. The has_accessors test is why this costs nothing on the
            // objects that have none, which is nearly all of them.
            if (accessor_entry * entry = obj->find_accessor(name)) {
                return call_getter(*this, *entry, target);
            }
            if (obj->prototype.is_object()) {
                obj = static_cast<object_object *>(obj->prototype.as_heap());
                continue;
            }
            // A PROXY on the chain is asked with `target` as the receiver
            // (10.1.8.1 step 3): its `get` trap, or its target's getter, sees
            // the object the read began on.
            if (obj->prototype.is_kind(heap_kind::proxy)) {
                return get_with_receiver(obj->prototype, name, target);
            }
            // A PROTOTYPE THAT IS NOT A PLAIN OBJECT - an Array
            // (`foo.prototype = [1]`), a function - carries on the walk in its
            // own arm below. Its getters see it as the receiver rather than
            // `target`, which nothing in the corpus observes.
            if (obj->prototype.is_heap() && !obj->prototype.is_string()) {
                return lookup_property(obj->prototype, name);
            }
            // An EXPLICIT null [[Prototype]] (object_object::prototype): the
            // chain ends here, with no implicit Object.prototype.
            if (obj->prototype.is_undefined()) { return value::undefined(); }
            obj = nullptr;
        }
        return from_object_prototype(target, name);
    }
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        // js_length, NOT length(): an index too far out to materialise raises
        // `length` without allocating for it, and this is the one read that has
        // to see that. Everything else keeps items.size() and its bounds.
        // A typed array's `length` is a prototype getter (23.2.3.19), so an own
        // property of that name a page defined shadows it; an Array's is its own.
        if (name == "length" &&
            (arr->elements == element_kind::none || !arr->named ||
             (arr->named->find(name) == nullptr && arr->named->find_accessor(name) == nullptr))) {
            return value::number(static_cast<double>(arr->js_length()));
        }
        // A CANONICAL INDEX SPELLED AS A STRING is the element: `a["0"]`, and
        // `for (i in a) a[i]` where i is always a string. It went to the
        // prototype (and, since the named table, would have gone there) - p5's
        // PrintWriter walks its writers exactly this way.
        if (std::uint32_t at = 0; object_object::array_index_key(name, at)) {
            return lookup_index(target, value::number(static_cast<double>(at)));
        }
        // ...and every OTHER canonical numeric key names no element of a
        // typed array: undefined, without asking the prototypes (10.4.5.4).
        if (arr->elements != element_kind::none && canonical_numeric_key(name)) {
            return value::undefined();
        }
        // A VIEW'S `buffer`, `byteLength` AND `byteOffset` ARE %TypedArray%.
        // prototype's getters (23.2.3.2-4), reached below: an inline answer
        // here used to make a FRESH bare object per `buffer` read, so
        // `ta.buffer === ta.buffer` was false and the answer was no ArrayBuffer.
        // (An exec() result's index/input/groups are named own properties,
        // below, like any other - builtins/text/regexp/execution.cpp.)
        // A NAMED OWN PROPERTY - see array_object::named. Data or accessor,
        // before the prototypes, as on any object.
        if (arr->named) {
            if (value * found = arr->named->find(name)) { return *found; }
            if (accessor_entry * entry = arr->named->find_accessor(name)) {
                return call_getter(*this, *entry, target);
            }
        }
        return array_inherited(*this, target, *arr, name);
    }
    if (target.is_string()) {
        auto * str = static_cast<string_object *>(target.as_heap());
        if (name == "length") { return value::number(static_cast<double>(str->length())); }
        // AN INDEX SPELLED AS A STRING - `s["1"]`, the key a for-in hands
        // over - is the same own property `s[1]` reads (10.4.3.5).
        if (std::uint32_t at = 0; object_object::array_index_key(name, at) && at < str->length()) {
            return string(str->unit_text(at));
        }
        // The prototype's accessors too, called with the PRIMITIVE as the
        // receiver (GetV, 7.3.3) - a strict getter sees `typeof this` as
        // "string" - and its own chain behind it (lookup_along).
        if (object_object * table = prototype(proto_kind::string)) {
            return lookup_along(table, target, name);
        }
        return from_object_prototype(target, name);
    }
    // A BigInt is a primitive with methods, exactly as a number is - and it
    // reaches its prototype the same way, because it has no own properties for
    // a lookup to find first. Without this `(255n).toString(16)` read
    // undefined and called it.
    if (target.is_kind(heap_kind::bigint)) {
        if (object_object * table = prototype(proto_kind::bigint)) {
            return lookup_along(table, target, name);
        }
        return from_object_prototype(target, name);
    }
    if (target.is_number()) {
        if (object_object * table = prototype(proto_kind::number)) {
            return lookup_along(table, target, name);
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
        return from_object_prototype(target, name);
    }
    // A boolean is a value with methods too. `flag.toString()` is what a
    // template literal and a string concatenation both do underneath, and code
    // that calls it explicitly - to build a cache key, say - found nothing.
    if (target.is_boolean()) {
        if (object_object * table = prototype(proto_kind::boolean)) {
            return lookup_along(table, target, name);
        }
        return from_object_prototype(target, name);
    }
    if (target.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(target.as_heap());
        if (accessor_entry * entry = fn->find_accessor(name)) {
            return call_getter(*this, *entry, target);
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
                    return call_getter(*this, *entry, target);
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
            if (accessor_entry * entry = table->find_accessor(name)) {
                return call_getter(*this, *entry, target);
            }
        }
        // ...AND THEN Object.prototype, because Function.prototype's own
        // [[Prototype]] is Object.prototype. Without it `f.hasOwnProperty` and
        // `f.propertyIsEnumerable` were undefined on every function, which is
        // the same gap numbers, booleans and strings had until they were fixed
        // and functions were left out of.
        return from_object_prototype(target, name);
    }
    if (target.is_kind(heap_kind::symbol)) {
        // Symbol.prototype's own table, accessors included - `description`
        // is its getter (20.4.3.2), which knows that Symbol() has none - then
        // Object.prototype behind it.
        if (object_object * table = prototype(proto_kind::symbol)) {
            if (value * found = table->find(name)) { return *found; }
            if (accessor_entry * entry = table->find_accessor(name)) {
                return call_getter(*this, *entry, target);
            }
        }
        return from_object_prototype(target, name);
    }
    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        if (value * found = closure->find(name)) { return *found; }
        // An OWN accessor - `static get w()` on a class, the constructor being
        // the closure, or one a page defined - before anything synthesised: a
        // `name` redefined as a getter is that getter's (bind reads it).
        if (accessor_entry * entry = closure->find_accessor(name)) {
            return call_getter(*this, *entry, target);
        }
        if (name == "prototype") {
            return method_without_prototype(*closure) ? value::undefined()
                                                      : ensure_prototype(target);
        }
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
            if (name == "name" && !closure->name_erased) {
                return string(closure->proto->display_name());
            }
            if (name == "length" && !closure->length_erased) {
                return value::number(closure->proto->length);
            }
            // A SLOPPY function's `caller` and `arguments` are null (Annex B's
            // implementation-defined answer, and every browser's); a strict
            // one reaches Function.prototype's %ThrowTypeError% accessor.
            if (!closure->proto->is_strict && !closure->proto->is_arrow &&
                !closure->proto->is_generator && (name == "caller" || name == "arguments")) {
                return value::null();
            }
        }
        // STATIC INHERITANCE: `class D extends B` makes D's own [[Prototype]]
        // B, so `D.staticMethod` finds B's. Babel wires this by hand with
        // _setPrototypeOf, and a real `extends` should do the same.
        for (value up = closure->proto_link; up.is_callable();) {
            if (up.is_kind(heap_kind::function)) {
                auto * parent = static_cast<closure_object *>(up.as_heap());
                if (value * found = parent->find(name)) { return *found; }
                // `static get x()` on the parent: called on the SUBCLASS.
                if (accessor_entry * entry = parent->find_accessor(name)) {
                    return call_getter(*this, *entry, target);
                }
                up = parent->proto_link;
                continue;
            }
            auto * parent = static_cast<native_object *>(up.as_heap());
            if (accessor_entry * entry = parent->find_accessor(name)) {
                return call_getter(*this, *entry, target);
            }
            if (value * found = parent->find(name)) { return *found; }
            break;
        }
        // A FUNCTION IS AN OBJECT WITH A PROTOTYPE OF ITS OWN. `call`, `apply`
        // and `bind` live there, and p5.js cannot install a single event
        // listener without bind. Then Object.prototype, which is
        // Function.prototype's own [[Prototype]] - see the native arm above.
        // A generator or async function has ITS intrinsic in between
        // (function_proto_kind): `(function* () {}).constructor` is
        // %GeneratorFunction%, and `.prototype` of that its prototype.
        if (const proto_kind own = function_proto_kind(target); own != proto_kind::function) {
            if (object_object * table = prototype(own)) {
                if (value * found = table->find(name)) { return *found; }
                if (accessor_entry * entry = table->find_accessor(name)) {
                    return call_getter(*this, *entry, target);
                }
            }
        }
        if (object_object * table = prototype(proto_kind::function)) {
            if (value * found = table->find(name)) { return *found; }
            if (accessor_entry * entry = table->find_accessor(name)) {
                return call_getter(*this, *entry, target);
            }
        }
        return from_object_prototype(target, name);
    }
    return value::undefined();
}

// THE IMPLICIT Object.prototype, for a value whose own chain does not link to
// it explicitly - which is every plain object, array, primitive and function.
// A data member was found here before; an ACCESSOR was not, so
// `Object.prototype.__proto__` (B.2.2.1) was invisible to `({}).__proto__` and
// visible to an object whose chain happened to reach the table by hand. The
// getter runs with the ORIGINAL receiver, as a getter anywhere on a chain does.
value context::lookup_along(object_object * obj, value receiver, const std::string & name) {
    const prehashed_name key{name, hash_name(name)};
    for (int depth = 0; obj != nullptr && depth < 64; ++depth) {
        if (value * found = obj->find(key)) { return *found; }
        if (accessor_entry * entry = obj->find_accessor(name)) {
            return call_getter(*this, *entry, receiver);
        }
        if (obj->prototype.is_object()) {
            obj = static_cast<object_object *>(obj->prototype.as_heap());
            continue;
        }
        if (obj->prototype.is_heap() && !obj->prototype.is_string()) {
            return lookup_property(obj->prototype, name);
        }
        if (obj->prototype.is_undefined()) { return value::undefined(); }
        obj = nullptr;
    }
    return from_object_prototype(receiver, name);
}

value context::from_object_prototype(value receiver, const std::string & name) {
    object_object * table = prototype(proto_kind::object);
    if (table == nullptr) { return value::undefined(); }
    if (value * found = table->find(name)) { return *found; }
    if (accessor_entry * entry = table->find_accessor(name)) {
        return call_getter(*this, *entry, receiver);
    }
    return value::undefined();
}

} // namespace ctbrowser::script
