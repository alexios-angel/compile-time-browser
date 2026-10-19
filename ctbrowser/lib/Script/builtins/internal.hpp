//===- internal.hpp - Shared helpers for the builtins -----------*- C++ -*-===//
//
// Private to lib/Script/builtins/: not installed and in no file set. The
// public surface of the JavaScript standard library is the single function
// install_builtins() in include/ctbrowser/script/builtins.hpp; this header
// holds what the files implementing it share: argument coercion, the generic
// Array.prototype receiver helpers, property-installation helpers and the
// install_* declarations. Bare section numbers refer to ECMA-262.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/number_format.hpp>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/regex.hpp>

// The JavaScript standard library implemented here is a subset, chosen by what
// pages actually call rather than by what the spec lists.

namespace ctbrowser::script {

// --- Argument helpers, the same vocabulary the DOM bindings use -----------

/// Argument `i`, or undefined when fewer were passed.
[[nodiscard]] inline value arg_at(std::span<value> args, std::size_t i) {
    return i < args.size() ? args[i] : value::undefined();
}
/// Argument `i` through the static context::to_number, which cannot run a user
/// `valueOf` and so answers NaN for any object; 0 when absent.
[[nodiscard]] inline double num_at(std::span<value> args, std::size_t i) {
    return i < args.size() ? context::to_number(args[i]) : 0.0;
}
/// ToNumber (7.1.4) and ToString (7.1.17) both refuse a Symbol with a
/// TypeError, and context::to_number_value / to_string cannot say so: to_string
/// is also ToPropertyKey, which a symbol must pass through. So the refusal sits
/// on the argument helpers every built-in reads through, and no method has to
/// remember it. False means the TypeError is in flight; the callers below
/// answer a harmless default and the method returns into the unwound frame.
[[nodiscard]] inline bool numeric_arg(context & cx, value v) {
    if (!v.is_kind(heap_kind::symbol)) { return true; }
    cx.throw_error("TypeError", "Cannot convert a Symbol value to a number");
    return false;
}
/// The ToString half of numeric_arg: false, with a TypeError in flight, for a
/// Symbol.
[[nodiscard]] inline bool stringable_arg(context & cx, value v) {
    if (!v.is_kind(heap_kind::symbol)) { return true; }
    cx.throw_error("TypeError", "Cannot convert a Symbol value to a string");
    return false;
}
/// ToString of an argument: ToPrimitive first, then reject even a Symbol
/// returned by an object. context::to_string also serves ToPropertyKey and
/// deliberately preserves Symbols there.
[[nodiscard]] inline std::string string_arg(context & cx, value v) {
    if (v.is_object_like()) {
        const context::rooted keep{cx, v};
        const context::reentry_scope guard{cx};
        if (guard.overflowed() || !cx.to_primitive_hint(v, "string", v)) { return {}; }
    }
    return stringable_arg(cx, v) ? cx.to_string(v) : std::string{};
}
/// string_arg of argument `i`; "" when absent.
[[nodiscard]] inline std::string str_at(context & cx, std::span<value> args, std::size_t i) {
    return i < args.size() ? string_arg(cx, args[i]) : std::string{};
}

/// ToIntegerOrInfinity (7.1.5) of argument `i`, running a user `valueOf` if the
/// argument is an object (num_at cannot, and answers NaN). NaN becomes 0.
/// Infinity is kept, not clamped, so callers can tell "too long" (a RangeError)
/// from "unbounded". Never feed NaN to std::clamp followed by a size_t cast:
/// that is undefined behaviour.
[[nodiscard]] inline double integer_arg(context & cx, std::span<value> args, std::size_t i) {
    if (i < args.size() && !numeric_arg(cx, args[i])) { return 0.0; }
    const double n = i < args.size() ? cx.to_number_value(args[i]) : 0.0;
    return std::isnan(n) ? 0.0 : std::trunc(n);
}

/// The longest string, in UTF-8 bytes, that a built-in will build (2^28).
/// Longer requests throw a RangeError instead of silently returning a shorter
/// string. ECMA-262 allows up to 2^53-1 code units and V8 stops near 2^29.
inline constexpr double max_string_length = 268435456.0; // 2^28 bytes

/// Whether an optional index was supplied at all. Absent and an explicit
/// `undefined` mean the same thing, and testing the argument count alone gets
/// that wrong: `"abc".slice(1, undefined)` is "bc" because the end defaults to
/// the length, but a count test sees two arguments, coerces `undefined` to 0
/// and returns "".
[[nodiscard]] inline bool has_index(std::span<value> args, std::size_t i) {
    return i < args.size() && !args[i].is_undefined();
}

/// Clamps a possibly-negative, possibly-huge index the way the array and string
/// methods all do: negative counts from the end, out of range clamps.
[[nodiscard]] inline std::size_t clamp_index(double raw, std::size_t length) {
    if (std::isnan(raw)) { return 0; }
    if (raw < 0) {
        const double from_end = static_cast<double>(length) + raw;
        return from_end < 0 ? 0 : static_cast<std::size_t>(from_end);
    }
    return raw > static_cast<double>(length) ? length : static_cast<std::size_t>(raw);
}

/// Number.MAX_SAFE_INTEGER, the upper bound of ToLength.
inline constexpr double max_safe_integer = 9007199254740991.0; // 2^53 - 1
/// 6.1.7: an array index is at most 2^32-2, so an Array's length is at most
/// 2^32-1 and ArrayCreate refuses anything larger with a RangeError. That is
/// what `Array.prototype.map.call({length: 4294967296}, f)` asserts.
inline constexpr double max_array_length = 4294967295.0; // 2^32 - 1
/// ToLength (7.1.20): ToIntegerOrInfinity clamped into [0, 2^53-1]. A negative
/// `length` is zero, not an error and not a huge unsigned number, which is the
/// difference between `Array.prototype.map.call({length: -1}, f)` being `[]`
/// and being a loop that never ends.
[[nodiscard]] inline double to_length(double n) {
    if (std::isnan(n)) { return 0.0; }
    const double truncated = std::trunc(n);
    if (truncated <= 0.0) { return 0.0; }
    return truncated > max_safe_integer ? max_safe_integer : truncated;
}

/// Whether anything on an ordinary array's prototype chain could answer an
/// index: a setter, a read-only property, a proxy. Defined in
/// vm/objects/store.cpp, beside the write that asks it; the fast paths that add
/// elements (push, unshift) ask it too before they skip the [[Set]] they stand
/// in for.
[[nodiscard]] bool array_chain_holds_indices(context & cx, const array_object & arr);

namespace detail {

/// The receiver as an array_object, or null when `this` is anything else.
[[nodiscard]] inline array_object * this_array(context & cx) {
    const value self = cx.current_this();
    return self.is_array() ? static_cast<array_object *>(self.as_heap()) : nullptr;
}
/// The receiver's text: a string primitive directly, anything else through
/// string_arg (ToPrimitive, then ToString).
[[nodiscard]] inline std::string this_string(context & cx) {
    const value self = cx.current_this();
    if (self.is_string()) { return static_cast<string_object *>(self.as_heap())->text; }
    return string_arg(cx, self);
}

/// thisNumberValue (21.1.3): the Number behind `this` for a Number.prototype
/// method. Accepts a Number primitive, a Number wrapper (whose value is in
/// primitive_slot), or Number.prototype itself (whose [[NumberData]] is +0, so
/// `Number.prototype.toString()` is "0"). Anything else is a TypeError, and the
/// answer is then NaN. It never coerces: ToString on an object would call the
/// receiver's own toString, which is this method again, and recurse until the
/// stack overflows.
[[nodiscard]] inline double this_number_value(context & cx, const char * method) {
    const value self = cx.current_this();
    if (self.is_number()) { return self.as_number(); }
    // A wrapper (`new Number(5)`) carries its [[NumberData]] in the slot - see
    // primitive_slot in value.hpp and box_primitive below.
    if (value * slot = primitive_slot(self); slot != nullptr && slot->is_number()) {
        return slot->as_number();
    }
    if (self.is_object() && self.as_heap() == cx.prototype(context::proto_kind::number)) {
        return 0.0;
    }
    cx.throw_error("TypeError", std::string{method} + " requires that 'this' be a Number");
    return std::nan("");
}

// --- Primitive wrappers, 7.1.18 ToObject --------------------------------------

/// Turns the fresh object `self` into the wrapper of `primitive`: an ordinary
/// object on the prototype its kind names, with the primitive in the
/// private-keyed slot `primitive_slot_key` (value.hpp says why a private key is
/// an internal slot here). `typeof` says "object", `==` and arithmetic reach the
/// primitive through valueOf, and Number.prototype's, Boolean.prototype's and
/// String.prototype's own methods read the slot. A prototype `self` already has
/// (a subclass instance) is kept.
[[nodiscard]] inline value wrap_primitive(context & cx, value self, value primitive) {
    return cx.wrap_primitive(self, primitive);
}
/// ToObject of a primitive. An object passes through; null and undefined are
/// returned unchanged for the caller to refuse.
[[nodiscard]] inline value box_primitive(context & cx, value v) {
    return cx.box_primitive(v);
}
/// Whether a native constructor is being run under `new`. A native has no
/// new.target: what it has is `this`, which context::construct makes as a
/// fresh, empty instance before the call, and which a plain call never
/// supplies. `Number.call({}, 5)` is the one spelling this cannot tell apart,
/// and no page writes it.
[[nodiscard]] inline bool constructing_this(value self) {
    if (!self.is_object()) { return false; }
    auto * obj = static_cast<object_object *>(self.as_heap());
    return obj->props.empty() && !obj->accessors.any;
}
/// SetFunctionName (10.2.9) with a property key: a symbol's name is its
/// description in brackets - "" with none - and `prefix` ("get", "set") goes
/// in front. A function with an own `name` already keeps it: a class whose
/// static member is called `name` defined that one itself.
inline void set_function_name(context & cx, value fn, value key, std::string_view prefix) {
    if (!fn.is_kind(heap_kind::function)) { return; }
    auto * closure = static_cast<closure_object *>(fn.as_heap());
    if (closure->find("name") != nullptr) { return; }
    std::string name;
    if (key.is_kind(heap_kind::symbol)) {
        const auto described = static_cast<symbol_object *>(key.as_heap())->description_value();
        name = described ? "[" + std::string{*described} + "]" : "";
    } else {
        name = cx.to_string(key);
        // A Private Name is named by its description, `#y` (10.2.9 step 3):
        // its key (value/base.hpp) less the lead byte and the `:<class>`.
        if (is_private_key(name)) { name = name.substr(1, name.find(':') - 1); }
    }
    if (!prefix.empty()) { name = std::string{prefix} + " " + name; }
    closure->define("name", cx.string(std::move(name)), attr_configurable);
}
/// GetPrototypeFromConstructor (10.1.14) for a native that makes its own
/// object. `class A extends Array` reaches Array through super() with `this`
/// the instance [[Construct]] made from A.prototype; Array answers an array of
/// its own, and this gives that array the instance's prototype when it is not
/// the intrinsic, so the answer is an A (ArrayCreate's proto argument, and
/// AllocateTypedArray's). bind_this_name then makes it `this`.
inline void adopt_subclass_prototype(context & cx, value made, const object_object * intrinsic) {
    if (!made.is_array()) { return; }
    const value self = cx.current_this();
    if (!constructing_this(self)) { return; }
    const value proto = static_cast<object_object *>(self.as_heap())->prototype;
    if (!proto.is_object() || proto.as_heap() == intrinsic) { return; }
    static_cast<array_object *>(made.as_heap())->prototype = proto;
}

/// An iterator over a list that already exists: what `keys()`, `values()` and
/// `entries()` answer on an Array, a Map and a Set, and what
/// String.prototype[@@iterator] answers over the characters. Defined in
/// collections/array_iteration.cpp, which says why it answers both protocols.
[[nodiscard]] value list_iterator(context & cx, value items, const char * tag);
/// CreateArrayIterator (23.1.5.1): a live iterator over an array-like or a
/// typed array - `kind` 0 keys, 1 values, 2 entries - sharing the one
/// %ArrayIteratorPrototype%. Also in collections/array_iteration.cpp.
[[nodiscard]] value array_iterator(context & cx, value target, double kind);
/// The one prototype of an iterator kind - %MapIteratorPrototype% and the rest
/// - made on first use with `next` and the tag (array_iteration.cpp).
[[nodiscard]] value iterator_kind_prototype(context & cx, const char * tag, native_fn next);

// --- An Array.prototype method's receiver, which need not be an Array ------
//
// Every Array.prototype method is generic: `this` goes through ToObject and is
// read with [[Get]] on string index keys, so
// `Array.prototype.reduce.call({length: 2, 0: 'a', 1: 'b'}, f)` must work.
// Reads and writes use context::lookup_index and store_index, the same [[Get]]
// and [[Set]] as the interpreter's `a[i]`. That costs a real array nothing
// (lookup_index tests `is_array()` first and indexes `items`) and handles
// accessors, prototype chains and proxies.

/// LengthOfArrayLike (7.3.18). For a real Array (with no own `length`
/// shadowing it) this is js_length(), read straight from the object rather
/// than through a property lookup. A typed array whose `length` an own
/// property shadows reads that property. Anything else goes through
/// [[Get]]("length") and ToLength.
[[nodiscard]] inline double array_like_length(context & cx, value self) {
    if (self.is_array()) {
        auto * arr = static_cast<array_object *>(self.as_heap());
        // A typed array's `length` is a prototype getter an own property may
        // shadow (Object.defineProperty(ta, "length", {value: 4000}) is what
        // Array.prototype.concat's spreading then reads); an Array's is its own.
        const bool shadowed = arr->elements != element_kind::none && arr->named &&
                              (arr->named->find("length") != nullptr ||
                               arr->named->find_accessor("length") != nullptr);
        // js_length, not the vector's size: a sparse tail (past dense_limit)
        // is length the vector does not hold, so after `x.length = 4294967295`
        // the vector's size would read back as 0.
        if (!shadowed) { return static_cast<double>(arr->js_length()); }
    }
    const value raw = cx.lookup_property(self, "length");
    if (!numeric_arg(cx, raw)) { return 0.0; }
    return to_length(cx.to_number_value(raw));
}

/// [[Get]] of index `i` on any receiver.
[[nodiscard]] inline value element_at(context & cx, value self, double i) {
    return cx.lookup_index(self, value::number(i));
}

// Set(O, P, V, true): 23.1.3's writes all carry Throw=true, so a write that
// does not land (a frozen array, a non-writable `length`, a String receiver's
// index) is a TypeError even in sloppy code. context::store_index records the
// refusal in store_rejected_ and strict_store_check turns it into the throw.
//
// False means the throw is in flight and the method must return at once: a
// second throw_error on the way out would consume a second handler
// (context::reentry_scope says why that loses the page's own `try`), and a
// walk that carries on after its first refused write is doing work the
// specification stopped.
//
// Whether the native itself called throw_error is read from
// context::mark_stack. That throw is not parked, so throw_pending misses it,
// and landing it clears `thrown_`; but landing also moves the handler frame's
// `ip` to the catch block or pops frames above it, and the mark records both
// (a compiled frame's `ip` is its landing pad, so a catch there shows too). An
// uncaught throw fails the run, which throw_pending does see. The mark is cheap
// to compare, which matters because charCodeAt calls this twice per character.

/// True if a throw happened since `before` was taken (see above).
[[nodiscard]] inline bool threw_since(context & cx, const context::stack_mark & before) {
    return cx.throw_pending() || cx.mark_stack() != before;
}
/// The same question as threw_since, asked of context::unwinds(), which costs
/// nothing: a native that has read `length` off a revoked proxy has already
/// thrown and landed, and must not throw a second time over the handler it
/// consumed.
struct unwind_watch {
    context & cx;
    std::size_t before;
    explicit unwind_watch(context & c) : cx(c), before(c.unwinds()) {}
    [[nodiscard]] bool threw() const { return cx.throw_pending() || cx.unwinds() != before; }
    /// Call after a callback that did not throw: one that threw and caught
    /// inside itself moved the count, and the next read must not take that for
    /// its own.
    void rearm() { before = cx.unwinds(); }
};
[[nodiscard]] inline array_object * dense_array_this(value self); // below
/// Set(O, i, v, true): writes index `i`, taking a direct store when nothing
/// can refuse or observe it. False means a TypeError is in flight.
[[nodiscard]] inline bool put_element(context & cx, value self, double i, value v) {
    if (self.is_array()) {
        auto * arr = static_cast<array_object *>(self.as_heap());
        // A typed array coerces and drops out of range, never refuses; an
        // ordinary one with no element attributes of its own that is
        // extensible and writable never refuses an index at or below its size
        // - a hole there included (array_object::holes_begin). Neither can
        // run a line of JavaScript.
        const bool typed = arr->is_view() || arr->elements != element_kind::none;
        if (typed ||
            (arr->element_attrs.empty() && arr->length_writable && arr->extensible &&
             arr->elements_writable && i >= 0 && i <= static_cast<double>(arr->items.size()) &&
             !array_chain_holds_indices(cx, *arr))) {
            cx.store_index(self, value::number(i), v);
            return true;
        }
    }
    const context::stack_mark before = cx.mark_stack();
    cx.clear_store_rejected();
    cx.store_index(self, value::number(i), v);
    if (cx.throw_pending()) { return false; }
    cx.strict_store_check(number_to_string(i));
    return !threw_since(cx, before);
}
/// CreateDataPropertyOrThrow(A, k, v) (7.3.5): a define, not a [[Set]], so a
/// non-writable but configurable slot on a species-made result is overwritten
/// rather than refused; the refusal is a TypeError. A dense array takes
/// put_element's fast path, which is the same operation there.
[[nodiscard]] inline bool create_element(context & cx, value target, double k, value v) {
    // The same operation as a [[Set]] on a dense array - unless its chain
    // holds an index, which a Set would ask and a define must not
    // (Array.prototype[0] read-only does not stop `[].concat(x)`).
    if (array_object * dense = dense_array_this(target);
        dense != nullptr && !array_chain_holds_indices(cx, *dense)) {
        return put_element(cx, target, k, v);
    }
    context::property_descriptor wanted;
    wanted.has_value = wanted.has_writable = wanted.has_enumerable = wanted.has_configurable = true;
    wanted.held = v;
    wanted.writable = wanted.enumerable = wanted.configurable = true;
    // A throwing defineProperty trap answers true (context::define_own_property
    // says why), so true alone is not success: without the watch, splice's
    // species copy onto a proxy whose trap throws at "2" would walk on through
    // 2^53 indices.
    const unwind_watch watch{cx};
    const bool defined = cx.define_own_property(target, number_to_string(k), wanted);
    if (watch.threw()) { return false; }
    if (defined) { return true; }
    cx.throw_error("TypeError", "Cannot define element " + number_to_string(k));
    return false;
}
/// HasProperty over an index: what makes the iteration methods skip a hole.
///
/// Free on an array with no element attributes of its own
/// (array_object::element_attrs): every index below its size that is not in
/// its run of holes is present. For anything else - an index past the end
/// included, which Array.prototype may answer for - this is the real
/// HasProperty, and it is what makes
/// `Array.prototype.forEach.call({length: 3, 1: 'x'}, f)` call back once rather
/// than three times.
[[nodiscard]] inline bool has_element(context & cx, value self, double i) {
    if (self.is_array()) {
        auto * arr = static_cast<array_object *>(self.as_heap());
        if (arr->element_attrs.empty() && i >= 0 && i < static_cast<double>(arr->length()) &&
            !arr->in_hole_run(static_cast<std::size_t>(i))) {
            return true;
        }
    }
    return cx.has_property(self, value::number(i));
}

/// DeletePropertyOrThrow of index `i`. A proxy trap that throws answers true,
/// as its define does, so the unwind is checked first. False means a TypeError
/// is in flight.
[[nodiscard]] inline bool delete_element(context & cx, value self, double i) {
    const unwind_watch watch{cx};
    const bool deleted = cx.delete_own_property(self, number_to_string(i));
    if (watch.threw()) { return false; }
    if (deleted) { return true; }
    cx.throw_error("TypeError", "Cannot delete property '" + number_to_string(i) + "'");
    return false;
}
/// Set(O, "length", len, true); false means a TypeError is in flight.
[[nodiscard]] inline bool put_length(context & cx, value self, double len) {
    const context::stack_mark before = cx.mark_stack();
    cx.clear_store_rejected();
    cx.store_property(self, "length", value::number(len));
    if (cx.throw_pending()) { return false; }
    cx.strict_store_check("length");
    return !threw_since(cx, before);
}

/// The fast path's receiver: a real, ordinary Array, whose elements are its own
/// std::vector and can therefore be pushed, erased and reversed in place, or
/// null.
///
/// A mutating method is one algorithm with two spellings of the storage, not
/// two algorithms: this is the branch at the top of each that keeps ordinary
/// array code fast while any other `this` takes the specified
/// [[Get]]/[[Set]]/[[Delete]] walk.
///
/// A typed array is not one: a view's elements are bytes in somebody else's
/// ArrayBuffer and `items` is empty by construction (see array_object::viewed),
/// so vector surgery on one would silently do nothing. It goes the generic way,
/// where store_index coerces and refuses to grow it, which is what a typed
/// array is for.
///
/// Nor is an array with a hole, an accessor element or a non-writable `length`
/// (array_object::element_attrs and holes_begin, length_writable): those are
/// exactly the cases where vector surgery would skip a getter, fill a hole or
/// write a length the specification refuses, so they take the generic walk
/// too.
[[nodiscard]] inline array_object * dense_array_this(value self) {
    if (!self.is_array()) { return nullptr; }
    auto * arr = static_cast<array_object *>(self.as_heap());
    if (arr->is_view() || arr->elements != element_kind::none) { return nullptr; }
    // ...and every element must be in `items`: a sparse tail
    // (array_object::sparse, or a length set far past the end) is elements the
    // vector does not hold, so vector surgery would get
    // `x.length = 4294967295; x.push()` wrong.
    return arr->element_attrs.empty() && !arr->has_hole_run() && arr->length_writable &&
                   arr->js_length() == arr->items.size()
               ? arr
               : nullptr;
}

/// Refuses a String receiver, primitive or wrapper, with a TypeError.
/// Every write in 23.1.3's mutating methods (push, pop, shift, unshift, splice,
/// reverse, sort) uses Throw=true, and a String's indices and `length` are
/// non-writable, so the write must throw even in sloppy mode. Other primitives
/// are allowed: ToObject(true) has a writable `length`, so
/// `Array.prototype.push.call(true)` returns 0.
[[nodiscard]] inline bool mutable_receiver(context & cx, value self, const char * method) {
    const value * slot = primitive_slot(self);
    if (!self.is_string() && (slot == nullptr || !slot->is_string())) { return true; }
    cx.throw_error("TypeError",
                   std::string{"Array.prototype."} + method + " cannot modify a String");
    return false;
}

// The ceiling on a generic index walk, and why a mutating method needs one.
//
// `length` on an array-like is whatever the object says it is, up to 2^53-1,
// and 23.1.3's algorithms walk every index below it: `reverse` swaps len/2
// pairs, `shift` and `unshift` move len-1 elements, `sort` reads len. On a real
// Array that is bounded by the elements that exist. On `{length: 2**53-1}` it
// is bounded by nothing.
//
// The specification's answer is that the first getter to throw ends the walk,
// which is what test262's `*-near-integer-limit` files assert. A native loop
// cannot always see that a getter threw. generic_walk_ok is the up-front
// refusal for walks that cannot check; generic_walk below checks after every
// step, so it stops at the first throw and refuses only a walk that really runs
// max_generic_walk steps. Either way a ceiling that throws is preferred to a
// hang, as with max_string_length.
//
// The fast path never reaches here: a real Array reverses, shifts and sorts
// through its std::vector, so `new Array(1e8).reverse()` still works. Only a
// non-Array receiver claiming a length past 2^24 is refused.
inline constexpr double max_generic_walk = 16777216.0; // 2^24
/// False, with a RangeError in flight, when `len` exceeds max_generic_walk.
[[nodiscard]] inline bool generic_walk_ok(context & cx, double len) {
    if (len <= max_generic_walk) { return true; }
    cx.throw_error("RangeError", "the array-like's length is too large to walk");
    return false;
}

/// The max_generic_walk ceiling, charged one step at a time. step() is called
/// before each index and threw() after each read, so a walk over
/// `{length: 2**53 - 1}` stops at the first throwing getter, as 23.1.3
/// requires. Only a walk that takes max_generic_walk steps without a throw
/// gets the RangeError.
struct generic_walk {
    context & cx;
    unwind_watch watch{cx};
    double steps = 0;
    explicit generic_walk(context & c) : cx(c) {}
    /// Call before each step: false, with a RangeError in flight, once the
    /// budget is spent.
    [[nodiscard]] bool step() {
        steps += 1.0;
        return steps <= max_generic_walk || generic_walk_ok(cx, steps);
    }
    [[nodiscard]] bool threw() const { return watch.threw(); }
    void rearm() { watch.rearm(); } // see unwind_watch::rearm
};

/// ToObject(this value), 23.1.3's first step in every method: a primitive is
/// boxed (detail::box_primitive) so that `Array.prototype.push.call(true)` sets
/// `length` on a Boolean wrapper and answers 0 rather than refusing a write to
/// a primitive, and a String receiver's non-writable indices refuse through
/// the wrapper as 10.4.3 says. null and undefined pass through to
/// coercible_this, which is step 1's TypeError.
[[nodiscard]] inline value array_this(context & cx) {
    return box_primitive(cx, cx.current_this());
}

/// RequireObjectCoercible on `this` (7.1.18 step 1), the first check in every
/// Array.prototype method: null or undefined throws a TypeError. False means
/// the throw is in flight and the caller must return at once; it does not mean
/// "carry on with a default".
[[nodiscard]] inline bool coercible_this(context & cx, value self, const char * method) {
    if (!self.is_nullish()) { return true; }
    cx.throw_error("TypeError",
                   std::string{"Array.prototype."} + method + " called on null or undefined");
    return false;
}

/// True for an arguments object: an array_object on Object.prototype that
/// carries the marker context::make_arguments_object defines. It is not an
/// Array exotic object (7.2.2), and Object.prototype.toString reports it as
/// "[object Arguments]".
[[nodiscard]] inline bool is_arguments_object(value v) {
    if (!v.is_array()) { return false; }
    const auto * arr = static_cast<const array_object *>(v.as_heap());
    return arr->named && arr->named->find(CTBROWSER_PRIVATE_KEY("Arguments")) != nullptr;
}
/// IsArray (7.2.2) into `out`. Proxies are followed to their target, at most 64
/// levels (a guard against a pathological chain). A revoked proxy (null
/// handler) throws a TypeError and returns false. A typed array or an
/// arguments object is not an Array.
[[nodiscard]] inline bool is_array_value(context & cx, value v, bool & out) {
    for (int hops = 0; hops < 64 && v.is_kind(heap_kind::proxy); ++hops) {
        auto * p = static_cast<proxy_object *>(v.as_heap());
        if (p->handler.is_null()) {
            cx.throw_error("TypeError",
                           "Cannot perform 'IsArray' on a proxy that has been revoked");
            out = false;
            return false;
        }
        v = p->target;
    }
    // A typed array is not an Array exotic object (7.2.2 step 2 asks for the
    // exotic kind), though it is an array_object here: Array.isArray, concat's
    // spreading and ArraySpeciesCreate all say no to one.
    out = v.is_array() &&
          static_cast<const array_object *>(v.as_heap())->elements == element_kind::none &&
          !static_cast<const array_object *>(v.as_heap())->is_view() && !is_arguments_object(v);
    return true;
}

/// IsCallable (7.2.3), at the one place every iteration method checks it: an
/// absent or non-function callback is a TypeError before anything is read.
[[nodiscard]] inline bool callable_arg(context & cx, value fn, const char * what) {
    if (fn.is_callable()) { return true; }
    cx.throw_error("TypeError", std::string{what} + " is not a function");
    return false;
}

/// ArrayCreate (10.4.2.2 step 1): sets the fresh array `out` to length `len`,
/// or throws a RangeError and returns null past 2^32-1. `map`, `filter`,
/// `with`, `toReversed`, `toSorted` and `toSpliced` all build a fresh Array
/// this way, through array_object::set_js_length, which is both the refusal
/// and the cap that keeps a four-billion-element result from being
/// materialised.
[[nodiscard]] inline array_object * new_array_of_length(context & cx, value out, double len) {
    auto * made = static_cast<array_object *>(out.as_heap());
    if (made->set_js_length(len)) { return made; }
    cx.throw_error("RangeError", "Invalid array length");
    return nullptr;
}

/// ArraySpeciesCreate (10.4.2.3): the object an Array.prototype method fills. A
/// fresh Array of `len` unless the receiver is an array whose `constructor` -
/// or that constructor's @@species - is some other constructor, which is then
/// constructed with `len`. Undefined means a throw is in flight. (The
/// cross-realm Array test of step 4 has nowhere to apply: one context, one
/// realm.)
[[nodiscard]] inline value array_species_create(context & cx, value original, double len) {
    const unwind_watch watch{cx};
    bool is_array = false;
    if (!is_array_value(cx, original, is_array) || watch.threw()) { return value::undefined(); }
    value ctor = value::undefined();
    if (is_array) {
        ctor = cx.lookup_property(original, "constructor");
        if (watch.threw()) { return value::undefined(); }
        if (ctor.is_object_like()) {
            ctor = cx.lookup_property(ctor, CTBROWSER_SYMBOL_KEY("species"));
            if (watch.threw()) { return value::undefined(); }
            if (ctor.is_null()) { ctor = value::undefined(); }
        }
    }
    if (ctor.is_undefined() || (ctor.is_heap() && ctor.as_heap() == cx.global("Array").as_heap())) {
        const value out = cx.make_array();
        return new_array_of_length(cx, out, len) == nullptr ? value::undefined() : out;
    }
    if (!is_constructor(ctor)) {
        cx.throw_error("TypeError", "Array species constructor is not a constructor");
        return value::undefined();
    }
    // `length` is a mathematical value (10.4.2.3), so never -0: `slice(0, -0)`
    // constructs with +0.
    const value args[1] = {value::number(len + 0.0)};
    const value out = cx.construct(ctor, args);
    return watch.threw() || !out.is_object_like() ? value::undefined() : out;
}

/// A fresh empty ordinary object, to fill as a prototype or namespace.
[[nodiscard]] inline object_object * new_table(context & cx) {
    return static_cast<object_object *>(cx.make_object().as_heap());
}

/// Defines a built-in value property such as `Math.PI`, `Number.MAX_VALUE` or
/// `X.prototype` as { writable: false, enumerable: false, configurable: false }
/// (21.3.1, 21.1.2, and each constructor's own clause). An enumerable one would
/// appear in `Object.keys(Math)`. `Table` is object_object or native_object;
/// both define(name, value, attrs).
template <class Table> void constant(Table * table, std::string_view name, value v) {
    table->define(name, v, attr_none);
}

/// Defines a built-in function's own `length` and `name` as
/// { writable: false, enumerable: false, configurable: true } (10.2.5). They
/// are real table entries, so a write is refused and a delete succeeds, as
/// test262's verifyProperty expects. The arity comes from the method's spec
/// clause, because a native_fn records none. `length` is defined first so
/// OwnPropertyKeys reads ["length", "name"], as in other engines.
inline void install_arity(context & cx, native_object * fn, double arity) {
    fn->define("length", value::number(arity), attr_configurable);
    fn->define("name", cx.string(fn->name), attr_configurable);
}

/// A native function that is not a constructor: per clause 17 a built-in
/// method has no [[Construct]], so `new Math.abs()` is a TypeError.
[[nodiscard]] inline native_object * method_native(context & cx, std::string name, native_fn fn) {
    auto * made = cx.allocate<native_object>(std::move(name), std::move(fn));
    made->is_constructor = false;
    return made;
}
/// Defines a global function of clause 19 (isNaN, parseInt, eval): a
/// method_native with its own `length` and `name`. context::define_native
/// allocates a bare native, which suits the compiler's private entry points
/// but not a function a page can inspect with verifyProperty.
inline void global_fn(context & cx, std::string name, double arity, native_fn fn) {
    auto * made = method_native(cx, name, std::move(fn));
    install_arity(cx, made, arity);
    cx.define_global(std::move(name), value::object(made));
}
/// A getter or setter, for define_accessor: a function that is not a
/// constructor.
[[nodiscard]] inline value accessor_fn(context & cx, std::string name, native_fn fn) {
    return value::object(method_native(cx, std::move(name), std::move(fn)));
}
/// The `name` of a method installed under property key `key`. A method keyed
/// by a well-known symbol such as `Symbol.iterator` is named
/// "[Symbol.iterator]" (SetFunctionName on a symbol with a description); the
/// property key itself is unchanged.
[[nodiscard]] inline std::string method_name_for_key(const std::string & key) {
    if (key.starts_with(symbol_key_mark) && !key.starts_with(symbol_key_prefix) &&
        !key.starts_with(registered_symbol_key_prefix)) {
        return "[Symbol." + key.substr(2) + "]";
    }
    return key;
}
/// Installs a built-in method with `name` (attr_builtin: writable,
/// non-enumerable, configurable). This form defines `name` but not `length`,
/// for built-ins whose specified arity has not been checked; a missing
/// `length` is better than a wrong one. Prefer the overload that takes
/// `arity`. `Table` is an object_object, or a native_object for a built-in that
/// is both callable and a namespace (`Object(x)` coerces and `Object.keys` is
/// a static).
template <class Table> void method(context & cx, Table * table, std::string name, native_fn fn) {
    auto * made = method_native(cx, method_name_for_key(name), std::move(fn));
    made->define("name", cx.string(made->name), attr_configurable);
    table->define(name, value::object(made), attr_builtin);
}
/// Installs a built-in method with its own `length` (`arity`, from the
/// method's spec clause) and `name`.
template <class Table>
void method(context & cx, Table * table, std::string name, double arity, native_fn fn) {
    auto * made = method_native(cx, method_name_for_key(name), std::move(fn));
    install_arity(cx, made, arity);
    table->define(name, value::object(made), attr_builtin);
}

/// Which half of OwnPropertyKeys a caller wants. 20.1.2.10
/// (getOwnPropertyNames) and 20.1.2.11 (getOwnPropertySymbols) are the same
/// walk filtered two different ways, and 7.3.7/7.3.24 want it unfiltered: a
/// symbol key is copied by Object.assign and read by Object.defineProperties,
/// which is the one place OwnPropertyKeys and EnumerableOwnProperties differ.
enum class key_filter : std::uint8_t {
    strings,
    symbols,
    all
};
/// Every own key of any value, including the synthesised ones, and a proxy's
/// ownKeys trap. Defined in objects/operations.cpp.
[[nodiscard]] std::vector<std::string> own_property_names(context & cx, value of,
                                                          key_filter which = key_filter::strings);

} // namespace detail

// The install_* functions, one per global the standard library defines.
//
// They have external linkage because they are defined in several .cpp files.
// The `builtins_detail` namespace keeps them from colliding with other
// subsystems. They can run in any order: each builds one table and defines one
// global, and none reads what another wrote.
namespace builtins_detail {

void install_math(context & cx, std::uint64_t seed);
void install_array(context & cx);
/// The callback-taking half of install_array (forEach, map, reduce, ...).
/// install_array calls it at a fixed point so properties are defined in a
/// stable order; the two halves share only the constructor and prototype
/// passed here.
void install_array_iteration(context & cx, native_object * array_ctor, object_object * array_proto);
/// find / findIndex / findLast / findLastIndex: one [[Get]]-every-index walk,
/// answering the item or its index, undefined or -1 on a miss.
[[nodiscard]] value array_find(context & cx, std::span<value> a, const char * name, bool backwards,
                               bool want_index);
void install_string(context & cx);
void install_base64(context & cx);
void install_uri(context & cx);
void install_structured_clone(context & cx);
void install_boolean(context & cx);
void install_number(context & cx);
void install_object(context & cx);
void install_json(context & cx);
void install_date(context & cx);
void install_globals(context & cx);
void install_promise(context & cx);
void install_regexp(context & cx);
void install_symbol(context & cx);
void install_collections(context & cx);
void install_errors(context & cx);
void install_proxy(context & cx);
void install_shadow_realm(context & cx); // objects/shadow_realm.cpp
void install_function(context & cx);
void install_typed_arrays(context & cx);
void install_dynamic_function(context & cx);
void install_generator(context & cx);
void install_class_defined(context & cx);
// Installs the three natives an array destructuring pattern is compiled to
// (see iterator_open_name in objects/destructuring.cpp).
void install_destructuring_iteration(context & cx);
void install_iterator(context & cx);   // collections/iterator.cpp; called from install_promise
void install_weak_refs(context & cx);  // collections/weak.cpp; called from install_promise
void install_disposable(context & cx); // collections/disposable.cpp; called from install_promise

/// Links constructor and prototype: sets `Type.prototype.constructor` and the
/// constructor's own `length` (`arity`) and `name`. Code that identifies a
/// value through `Object.getPrototypeOf(x).constructor.name` needs both
/// halves. `arity` comes from the constructor's spec clause (1 for most, 7 for
/// Date), because a native_fn records none. Does nothing when `table` is
/// null.
inline void link_constructor(context & cx, object_object * table, const char * name, double arity,
                             value ctor) {
    if (table == nullptr) { return; }
    // `X.prototype.constructor` is { true, false, true } (clause 17), and
    // `X.name` and `X.length` are { false, false, true } (10.2.5). Enumerable
    // in either place would put "constructor" in
    // `Object.keys(SomeClass.prototype)`. `length` is defined before `name` so
    // OwnPropertyKeys reads ["length", "name"], the creation order 10.2.5 and
    // every other engine give.
    table->define("constructor", ctor, attr_builtin);
    if (ctor.is_kind(heap_kind::native)) {
        auto * fn = static_cast<native_object *>(ctor.as_heap());
        fn->define("length", value::number(arity), attr_configurable);
        fn->define("name", cx.string(name), attr_configurable);
    } else if (ctor.is_object()) {
        auto * obj = static_cast<object_object *>(ctor.as_heap());
        obj->define("length", value::number(arity), attr_configurable);
        obj->define("name", cx.string(name), attr_configurable);
    }
}

} // namespace builtins_detail

} // namespace ctbrowser::script
