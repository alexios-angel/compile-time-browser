//===- ctbrowser/lib/Script/vm/objects/lookup.cpp - Property reads --------===//
//
// Property reads on script values: `lookup_index` and `lookup_property`, the
// two hottest reads in the engine, and the prototype-chain walks behind them.
// Members of `context`, declared in include/ctbrowser/script/vm.hpp.
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

namespace {

/// Reads an accessor property: the getter runs with the original receiver,
/// whichever prototype the entry was found on; a set-only accessor reads as
/// undefined.
value call_getter(context & cx, const accessor_entry & entry, value receiver) {
    return entry.getter.is_callable() ? cx.call(entry.getter, std::span<const value>{}, receiver)
                                      : value::undefined();
}

} // namespace

/// Returns the typed-array constructor's prototype for `kind` (for example
/// `Uint8Array.prototype` for u8), found through the global it is installed as
/// (value.hpp, typed_array_global_name), or null before install_typed_arrays
/// has run. It has external linkage but is not in vm.hpp; the two other users
/// (chain.cpp's instance_of, builtins/objects/operations.cpp's prototype_of)
/// declare it themselves.
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

/// OrdinaryGet step 3 on an array (10.1.8.1): the [[Prototype]]'s [[Get]] of
/// `name`, with the array as the receiver. This is the tail of
/// lookup_property's array arm, and what an index the array does not have (a
/// hole, or one past the end) reads through to.
value array_inherited(context & cx, value target, const array_object & arr,
                      const std::string & name) {
    // An explicit [[Prototype]] (a subclass instance's, or one a page set) is
    // the whole chain from here: it reaches Array.prototype (or the kind's)
    // through its own links. See array_object::prototype.
    if (!arr.prototype.is_null()) [[unlikely]] {
        if (arr.prototype.is_object()) {
            return cx.lookup_along(static_cast<object_object *>(arr.prototype.as_heap()), target,
                                   name);
        }
        if (arr.prototype.is_heap() && !arr.prototype.is_string()) {
            return cx.lookup_property(arr.prototype, name);
        }
        return value::undefined(); // An explicit null.
    }
    // A typed array's own methods first, then every array's, then every
    // object's. That is the chain JavaScript has, and why both
    // `[1,2].hasOwnProperty(...)` and `bytes.subarray(...)` work.
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

/// Finds the private element `key` on `target`: an own field, or a private
/// method or accessor reached through the brand that records its home object.
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

/// Converts an internal property key back to the script value it names: a
/// symbol for the symbol-key encodings, otherwise a string.
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
    // A string key, or a private name's, which only the VM's own paths (a
    // private accessor's SetFunctionName) ever ask for.
    return minted_string(key);
}

/// [[Get]](name, receiver) on `base`: walks `base`'s chain, asking proxies and
/// running getters with `receiver` as `this`. Chains are capped at 64 levels
/// so a cyclic chain cannot hang the read.
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

/// `target[key]`: the element read, with a fast path for arrays, typed arrays
/// and strings indexed by number; everything else goes to lookup_property.
value context::lookup_index(value target, value key) {
    if (target.is_array() && key.is_number()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        // A Number that is not an index is the property its ToString names:
        // `a[1.5]` is a["1.5"], not a[1]. On a typed array a numeric key that
        // names no element is undefined, never the chain (10.4.5.4).
        if (!array_index_number(key.as_number())) {
            if (arr->elements != element_kind::none) { return value::undefined(); }
            return lookup_property(target, to_string(key));
        }
        const auto i = static_cast<std::ptrdiff_t>(key.as_number());
        if (arr->is_view()) {
            if (i < 0 || static_cast<std::size_t>(i) >= arr->length()) {
                return value::undefined();
            }
            // A BigInt kind's element is a fresh bigint built from the bytes;
            // every other kind's is view_get's double.
            return is_bigint_kind(arr->elements)
                       ? typed_element_get(*this, *arr, static_cast<std::size_t>(i))
                       : value::number(view_get(*arr, static_cast<std::size_t>(i)));
        }
        if (i >= 0 && static_cast<std::size_t>(i) < arr->items.size()) {
            // A hole reads through to the prototypes and an accessor element
            // calls its getter (see array_object::element_attrs and
            // holes_begin). Both are empty for an ordinary array, which then
            // pays only this one test.
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
        // Then the sparse elements, keyed by uint32 index (at most 4294967295).
        // The emptiness test comes first so reading past the end of an
        // ordinary array costs one branch.
        if (!arr->sparse.empty() && i >= 0 && static_cast<std::uint64_t>(i) <= 4294967295ull) {
            if (value * found = arr->find_sparse(static_cast<std::uint32_t>(i))) { return *found; }
        }
        // An index the array does not have goes to OrdinaryGet's step 3 like
        // any other absent key: `Array.prototype[1] = 1; [0][1]` is 1. A typed
        // array's is undefined without asking (10.4.5.4).
        if (arr->elements != element_kind::none) { return value::undefined(); }
        return array_inherited(*this, target, *arr, std::to_string(i));
    }
    if (target.is_string() && key.is_number() && array_index_number(key.as_number())) {
        // One code unit (10.4.3.5), half of a surrogate pair included.
        const auto & str = *static_cast<const string_object *>(target.as_heap());
        const auto i = static_cast<std::size_t>(key.as_number());
        return i < str.length() ? string(str.unit_text(i)) : value::undefined();
    }
    // ToObject(base) comes before the key's ToPropertyKey (GetValue step 3.a;
    // 13.3.3 hands the key over unconverted): `null[k]` throws the TypeError
    // without running k's toString, whose own throw would otherwise be left
    // pending under this one.
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

/// `target.name`: the property read for every kind of value, including
/// proxies, private names and the implicit prototypes of primitives.
value context::lookup_property(value target, const std::string & name) {
    // A private name is a brand check (7.3.31 PrivateGet): an object whose
    // class did not add the element throws TypeError, not undefined. A Proxy
    // carries only its own elements, never its target's. The hot path pays
    // two byte compares; the descriptor lookup runs only for private keys.
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
        // PrivateGet step 5.b: a private accessor with no getter (only a
        // `set #x`) throws TypeError too, where an ordinary accessor with no
        // getter reads undefined.
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
    // Reading a property of null or undefined is a TypeError (7.3.2 GetV ->
    // ToObject), not undefined. It is checked here rather than only in
    // op::get_prop because AOT-compiled code calls this member too, and the two
    // must report the same error.
    if (target.is_nullish()) [[unlikely]] {
        throw_error("TypeError", "Cannot read properties of " +
                                     std::string{target.is_null() ? "null" : "undefined"} +
                                     " (reading '" + name + "')");
        return value::undefined();
    }
    // A proxy answers first, or hands the question to its target: it must be
    // asked before anything looks at the object underneath it.
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
        // 10.5.8 step 6: target.[[Get]](P, Receiver) with the proxy as the
        // receiver, so a getter on the target sees the proxy as `this`.
        return get_with_receiver(p->target, name, target);
    }
    // Own properties first: a page that writes `arr.length = 0` or shadows a
    // method on one object must not be overridden by the prototype.
    if (target.is_object()) {
        // Own properties, then the object's own prototype chain (what a class
        // instance uses to find its methods), then the shared table. The depth
        // cap stops a page-made cyclic chain from hanging the lookup.
        auto * obj = static_cast<object_object *>(target.as_heap());
        // Hash once; every level of the chain is asked for the same name.
        const prehashed_name key{name, hash_name(name)};
        for (int depth = 0; obj != nullptr && depth < 64; ++depth) {
            if (value * found = obj->find(key)) { return *found; }
            // A String wrapper's `length` and indices are own properties read
            // from its [[StringData]] slot (10.4.3.1). They are checked only
            // after the own table missed (nothing in it can shadow them) and
            // only for those two shapes of name, so an ordinary object's
            // `.length` pays one comparison.
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
            // An accessor found anywhere on the chain is called with the
            // original target as its receiver, so a getter defined on a
            // prototype reads the instance. find_accessor is nearly free on
            // objects that have no accessors, which is nearly all of them.
            if (accessor_entry * entry = obj->find_accessor(name)) {
                return call_getter(*this, *entry, target);
            }
            if (obj->prototype.is_object()) {
                obj = static_cast<object_object *>(obj->prototype.as_heap());
                continue;
            }
            // A proxy on the chain is asked with `target` as the receiver
            // (10.1.8.1 step 3): its `get` trap, or its target's getter, sees
            // the object the read began on.
            if (obj->prototype.is_kind(heap_kind::proxy)) {
                return get_with_receiver(obj->prototype, name, target);
            }
            // A prototype that is not a plain object (an Array, as in
            // `foo.prototype = [1]`, or a function) continues the walk in its
            // own arm below. Its getters see it as the receiver rather than
            // `target`; nothing in the test corpus observes the difference.
            if (obj->prototype.is_heap() && !obj->prototype.is_string()) {
                return lookup_property(obj->prototype, name);
            }
            // An explicit null [[Prototype]] (object_object::prototype stores
            // it as undefined): the chain ends here, with no implicit
            // Object.prototype.
            if (obj->prototype.is_undefined()) { return value::undefined(); }
            obj = nullptr;
        }
        return from_object_prototype(target, name);
    }
    if (target.is_array()) {
        auto * arr = static_cast<array_object *>(target.as_heap());
        // js_length, not length(): an index too far out to materialise raises
        // `length` without allocating for it, and this is the one read that has
        // to see that. Everything else uses items.size() and its bounds.
        // A typed array's `length` is a prototype getter (23.2.3.19), so an own
        // property of that name a page defined shadows it; an Array's is its own.
        if (name == "length" &&
            (arr->elements == element_kind::none || !arr->named ||
             (arr->named->find(name) == nullptr && arr->named->find_accessor(name) == nullptr))) {
            return value::number(static_cast<double>(arr->js_length()));
        }
        // A canonical index spelled as a string (`a["0"]`, or a for-in key)
        // reads the element, not the prototype chain.
        if (std::uint32_t at = 0; object_object::array_index_key(name, at)) {
            return lookup_index(target, value::number(static_cast<double>(at)));
        }
        // ...and every other canonical numeric key names no element of a
        // typed array: undefined, without asking the prototypes (10.4.5.4).
        if (arr->elements != element_kind::none && canonical_numeric_key(name)) {
            return value::undefined();
        }
        // A view's `buffer`, `byteLength` and `byteOffset` are
        // %TypedArray%.prototype getters (23.2.3.2-4), reached via
        // array_inherited; answering inline would break `ta.buffer === ta.buffer`.
        // A named own property (array_object::named), data or accessor, wins
        // over the prototypes. An exec() result's index/input/groups are such
        // properties (builtins/text/regexp/execution.cpp).
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
        // An index spelled as a string (`s["1"]`, the key a for-in hands over)
        // is the same own property `s[1]` reads (10.4.3.5).
        if (std::uint32_t at = 0; object_object::array_index_key(name, at) && at < str->length()) {
            return string(str->unit_text(at));
        }
        // The prototype's accessors too, called with the primitive as the
        // receiver (GetV, 7.3.3), so a strict getter sees `typeof this` as
        // "string"; then its own chain behind it (lookup_along).
        if (object_object * table = prototype(proto_kind::string)) {
            return lookup_along(table, target, name);
        }
        return from_object_prototype(target, name);
    }
    // A BigInt is a primitive with methods, like a number, and reaches its
    // prototype the same way: it has no own properties to find first
    // (`(255n).toString(16)`).
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
        // ...then Object.prototype, which is Number.prototype's [[Prototype]];
        // a primitive boxes on property access, so `(5).hasOwnProperty(...)`
        // must work (Phaser's tween manager calls it on numbers).
        return from_object_prototype(target, name);
    }
    // A boolean has methods too (`flag.toString()`), through
    // Boolean.prototype and then Object.prototype.
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
        // A built-in's `name` is stored on the C++ object rather than
        // installed in each native's table (test262's assert.throws reads it).
        if (name == "name" && !fn->name_erased) { return string(fn->name); }
        // Static inheritance through the constructor's own [[Prototype]], the
        // same walk a closure does: `TypeError.__proto__` is `Error`, so a
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
        // ...then Object.prototype, Function.prototype's own [[Prototype]], so
        // `f.hasOwnProperty` works.
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
        // An own accessor (`static get w()` on a class whose constructor is
        // this closure, or one a page defined) comes before anything
        // synthesised: a `name` redefined as a getter is that getter's (bind
        // reads it).
        if (accessor_entry * entry = closure->find_accessor(name)) {
            return call_getter(*this, *entry, target);
        }
        if (name == "prototype") {
            return method_without_prototype(*closure) ? value::undefined()
                                                      : ensure_prototype(target);
        }
        // `f.name` and `f.length` are read from the compiled function rather
        // than stored on every closure: most functions are never asked, and two
        // extra properties each is a real cost on a large bundle. Libraries
        // identify values by `Object.getPrototypeOf(x).constructor.name`, so
        // `name` must be right.
        if (closure->proto != nullptr) {
            if (name == "name" && !closure->name_erased) {
                return string(closure->proto->display_name());
            }
            if (name == "length" && !closure->length_erased) {
                return value::number(closure->proto->length);
            }
            // A sloppy function's `caller` and `arguments` are null (Annex B's
            // implementation-defined answer, and every browser's); a strict
            // one reaches Function.prototype's %ThrowTypeError% accessor.
            if (!closure->proto->is_strict && !closure->proto->is_arrow &&
                !closure->proto->is_generator && (name == "caller" || name == "arguments")) {
                return value::null();
            }
        }
        // Static inheritance: `class D extends B` makes D's own [[Prototype]]
        // B, so `D.staticMethod` finds B's (Babel wires the same link by hand
        // with _setPrototypeOf).
        for (value up = closure->proto_link; up.is_callable();) {
            if (up.is_kind(heap_kind::function)) {
                auto * parent = static_cast<closure_object *>(up.as_heap());
                if (value * found = parent->find(name)) { return *found; }
                // `static get x()` on the parent runs with the subclass as `this`.
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
        // Then Function.prototype, where `call`, `apply` and `bind` live, and
        // Object.prototype behind it (see the native arm above). A generator or
        // async function has its own intrinsic in between (function_proto_kind):
        // `(function* () {}).constructor` is %GeneratorFunction%.
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

/// Walks `obj`'s prototype chain for `name`, calling any getter found with
/// `receiver` as `this`. A chain that ends without an explicit null falls back
/// to the implicit Object.prototype (from_object_prototype).
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

/// Looks `name` up on the implicit Object.prototype, which ends the chain of
/// every plain object, array, primitive and function that does not link to it
/// explicitly. Accessors are found too (e.g. `__proto__`, B.2.2.1) and run with
/// `receiver` as `this`.
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
