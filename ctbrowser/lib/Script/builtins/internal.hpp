#pragma once
// Private to lib/Script/builtins/. NOT installed and in no file set:
// include/ctbrowser/script/builtins.hpp declares exactly one function,
// install_builtins(), which is the entire public surface of the standard
// library. This header exists only so the implementation can be more than one
// file.

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
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/regex.hpp>

// The JavaScript standard library: a SUBSET, chosen by what pages actually
// call rather than by what the spec lists.

namespace ctbrowser::script {

// --- argument helpers, the same vocabulary the DOM bindings use -----------

[[nodiscard]] inline value arg_at(std::span<value> args, std::size_t i) {
    return i < args.size() ? args[i] : value::undefined();
}
[[nodiscard]] inline double num_at(std::span<value> args, std::size_t i) {
    return i < args.size() ? context::to_number(args[i]) : 0.0;
}
// ToNumber (7.1.4) and ToString (7.1.17) both REFUSE A SYMBOL with a TypeError,
// and context::to_number_value / to_string cannot say so: to_string is also
// ToPropertyKey, which a symbol must pass through. So the refusal sits on the
// argument helpers every built-in reads through, and no method has to
// remember it. FALSE means the TypeError is in flight; the callers below
// answer a harmless default and the method returns into the unwound frame.
[[nodiscard]] inline bool numeric_arg(context & cx, value v) {
    if (!v.is_kind(heap_kind::symbol)) { return true; }
    cx.throw_error("TypeError", "Cannot convert a Symbol value to a number");
    return false;
}
[[nodiscard]] inline bool stringable_arg(context & cx, value v) {
    if (!v.is_kind(heap_kind::symbol)) { return true; }
    cx.throw_error("TypeError", "Cannot convert a Symbol value to a string");
    return false;
}
// ToString of an argument: a symbol refuses, everything else converts.
[[nodiscard]] inline std::string string_arg(context & cx, value v) {
    return stringable_arg(cx, v) ? cx.to_string(v) : std::string{};
}
[[nodiscard]] inline std::string str_at(context & cx, std::span<value> args, std::size_t i) {
    return i < args.size() ? string_arg(cx, args[i]) : std::string{};
}

// ToIntegerOrInfinity (7.1.5) over an argument that MAY BE AN OBJECT, and the
// three non-terminating string methods are why it exists.
//
// `num_at` above is the STATIC context::to_number, which cannot run a user
// `valueOf` and therefore answers NaN for every object. NaN is where
// `repeat`, `padStart` and `padEnd` broke: each wrote
//
//     static_cast<std::size_t>(std::clamp(num_at(a, 0), 0.0, 1000000.0))
//
// and `std::clamp(NaN, lo, hi)` is NaN - neither comparison in it is true - so
// the cast is undefined behaviour, which on x86-64 is `cvttsd2si`'s indefinite
// value: 0x8000000000000000, or 9.2e18 as a size_t. `"abc".padStart(NaN)` and
// `"x".repeat({valueOf: () => 3})` then asked for a 9-exabyte string, which is
// the 2 TIMEOUTs and 6 CRASHes test262 measured in built-ins/String on
// 2026-09-02.
//
// Infinity is PRESERVED rather than clamped: the callers have to tell "too
// long" from "as long as you like" because the specification makes one of them
// a RangeError.
[[nodiscard]] inline double integer_arg(context & cx, std::span<value> args, std::size_t i) {
    if (i < args.size() && !numeric_arg(cx, args[i])) { return 0.0; }
    const double n = i < args.size() ? cx.to_number_value(args[i]) : 0.0;
    return std::isnan(n) ? 0.0 : std::trunc(n);
}

// THE LONGEST STRING THIS ENGINE WILL BUILD, and why there is a limit at all.
//
// ECMA-262 caps a String at 2^53-1 code units and leaves the real limit to the
// implementation - V8 throws "Invalid string length" past 2^29-24. Here a
// string is UTF-8 bytes in a std::string, so the limit is memory. What this
// replaces is a silent clamp to a million, which answered a SHORTER STRING THAN
// ASKED FOR: `"x".repeat(2000000).length` was 1000000 and nothing said so. A
// ceiling that THROWS is the honest form of the same protection.
inline constexpr double max_string_length = 268435456.0; // 2^28 bytes

// Was an OPTIONAL index supplied at all? Absent and an explicit `undefined`
// mean the same thing, and testing the argument COUNT alone gets that wrong:
// `"abc".slice(1, undefined)` is "bc" because the end defaults to the length,
// but a count test sees two arguments, coerces `undefined` to 0 and returns "".
[[nodiscard]] inline bool has_index(std::span<value> args, std::size_t i) {
    return i < args.size() && !args[i].is_undefined();
}

// Clamp a possibly-negative, possibly-huge index the way the array and string
// methods all do: negative counts from the end, out of range clamps.
[[nodiscard]] inline std::size_t clamp_index(double raw, std::size_t length) {
    if (std::isnan(raw)) { return 0; }
    if (raw < 0) {
        const double from_end = static_cast<double>(length) + raw;
        return from_end < 0 ? 0 : static_cast<std::size_t>(from_end);
    }
    return raw > static_cast<double>(length) ? length : static_cast<std::size_t>(raw);
}

// ToLength, 7.1.20 - ToIntegerOrInfinity clamped into [0, 2^53-1]. A negative
// `length` is ZERO, not an error and not a huge unsigned number, which is the
// difference between `Array.prototype.map.call({length: -1}, f)` being `[]` and
// being a loop that never ends.
inline constexpr double max_safe_integer = 9007199254740991.0; // 2^53 - 1
// 6.1.7: an array index is at most 2^32-2, so an Array's length is at most
// 2^32-1 and ArrayCreate REFUSES anything larger with a RangeError. That is
// what `Array.prototype.map.call({length: 4294967296}, f)` asserts.
inline constexpr double max_array_length = 4294967295.0; // 2^32 - 1
[[nodiscard]] inline double to_length(double n) {
    if (std::isnan(n)) { return 0.0; }
    const double truncated = std::trunc(n);
    if (truncated <= 0.0) { return 0.0; }
    return truncated > max_safe_integer ? max_safe_integer : truncated;
}

namespace detail {

// The receiver as its concrete type, or null when a method was called on
// something else. Every prototype method starts with one of these, and
// returning undefined rather than crashing is what makes `[].push.call(3)`
// harmless instead of fatal.
[[nodiscard]] inline array_object * this_array(context & cx) {
    const value self = cx.current_this();
    return self.is_array() ? static_cast<array_object *>(self.as_heap()) : nullptr;
}
[[nodiscard]] inline std::string this_string(context & cx) {
    const value self = cx.current_this();
    if (self.is_string()) { return static_cast<string_object *>(self.as_heap())->text; }
    return string_arg(cx, self);
}

// thisNumberValue (21.1.3), WHICH THIS FILE DID NOT HAVE - and the cycle that
// cost 45 of test262's 54 SIGSEGVs.
//
// Every Number.prototype method opened with `context::to_number(current_this())`,
// the STATIC coercion, which answers NaN for an object receiver. `toString` and
// `toPrecision` then fell back to `c.to_string(c.current_this())` for the NaN
// case - and ToString of an object is ToPrimitive, which calls the receiver's
// own `toString`, which is this native again:
//
//     Number.prototype.toString()      // `this` is Number.prototype, an object
//       -> to_string -> to_primitive_string -> invoke -> to_string -> ...
//
// The specification does not coerce here at all: `this` is a Number or an
// object with a [[NumberData]] slot, and anything else is a TypeError. There
// are no wrapper objects in this engine (`new Number(x)` is a conversion - see
// install_number), so the only object with [[NumberData]] is `Number.prototype`
// itself, whose slot is +0 by 21.1.3 - which is exactly why
// `Number.prototype.toString()` is specified to return "0".
[[nodiscard]] inline double this_number_value(context & cx, const char * method) {
    const value self = cx.current_this();
    if (self.is_number()) { return self.as_number(); }
    // A WRAPPER (`new Number(5)`) carries its [[NumberData]] in the slot - see
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

// --- PRIMITIVE WRAPPERS, 7.1.18 ToObject --------------------------------------
//
// The wrapper is an ordinary object on the prototype its kind names, with the
// primitive in the private-keyed slot `primitive_slot_key` (value.hpp says why
// a private key IS an internal slot here). `typeof` says "object", `==` and
// arithmetic reach the primitive through valueOf, and Number.prototype's,
// Boolean.prototype's and String.prototype's own methods read the slot.
[[nodiscard]] inline value wrap_primitive(context & cx, value self, value primitive) {
    auto * obj = static_cast<object_object *>(self.as_heap());
    if (!obj->prototype.is_object()) {
        const context::proto_kind kind = primitive.is_number()    ? context::proto_kind::number
                                         : primitive.is_boolean() ? context::proto_kind::boolean
                                         : primitive.is_string()  ? context::proto_kind::string
                                         : primitive.is_kind(heap_kind::symbol)
                                             ? context::proto_kind::symbol
                                             : context::proto_kind::bigint;
        if (object_object * table = cx.prototype(kind)) { obj->prototype = value::object(table); }
    }
    obj->define(primitive_slot_key, primitive, attr_none);
    return self;
}
// ToObject of a primitive. An object passes through; null and undefined are
// the caller's refusal.
[[nodiscard]] inline value box_primitive(context & cx, value v) {
    if (v.is_object_like() || v.is_nullish()) { return v; }
    const value made = cx.make_object();
    return wrap_primitive(cx, made, v);
}
// IS A CONSTRUCTOR BEING RUN UNDER `new`? A native has no new.target: what it
// has is `this`, which context::construct makes as a fresh, EMPTY instance
// before the call, and which a plain call never supplies. `Number.call({}, 5)`
// is the one spelling this cannot tell apart, and no page writes it.
[[nodiscard]] inline bool constructing_this(value self) {
    if (!self.is_object()) { return false; }
    auto * obj = static_cast<object_object *>(self.as_heap());
    return obj->props.empty() && !obj->accessors.any;
}

// A REAL ITERATOR over a list that already exists - what `keys()`, `values()`
// and `entries()` answer on an Array, a Map and a Set, and what
// String.prototype[@@iterator] answers over the characters. Defined in
// collections/array_iteration.cpp, which says why it answers both protocols.
[[nodiscard]] value list_iterator(context & cx, value items, const char * tag);

// --- AN Array.prototype METHOD'S RECEIVER, WHICH NEED NOT BE AN ARRAY ------
//
// Every one of them is specified GENERIC: `this` is ToObject'd and then read
// through [[Get]] with string index keys, so
// `Array.prototype.reduce.call({length: 2, 0: 'a', 1: 'b'}, f)` is not a
// curiosity. It is 91 of reduce's 260 test262 files, 89 of reduceRight's, and
// about 580 across the nine iteration methods (counted over the corpus at the
// pinned hash, 2026-09-07). Every method here opened with `this_array()`,
// which answers nullptr for anything that is not an array_object, and then
// returned a default - so all 580 measured "no exception" or a wrong answer.
//
// Reads go through context::lookup_index and context::store_index, which are
// the SAME [[Get]] and [[Set]] the interpreter's `a[i]` uses. That is not a
// slower path for a real array - lookup_index tests `is_array()` first and
// indexes `items` - and it is the only one that gets an accessor, a prototype
// chain and a Proxy right for anything else.

// LengthOfArrayLike, 7.3.18.
//
// FOR A REAL ARRAY THIS IS `items.size()` AND NOT the `length` property, which
// is unchanged from what these methods have always done and is deliberate:
// array_object records an index it refused to materialise in `sparse` and
// raises `length` to cover it, so `a[4294967295] = 'x'` makes `length` four
// billion from one assignment. Iterating to it would be four billion [[Get]]s
// and a TIMEOUT where there is an answer today. array_object's own comment
// names that deviation ("the array built-ins walk items and do not consult
// sparse"); this keeps it rather than widening it.
[[nodiscard]] inline double array_like_length(context & cx, value self) {
    if (self.is_array()) {
        return static_cast<double>(static_cast<array_object *>(self.as_heap())->length());
    }
    const value raw = cx.lookup_property(self, "length");
    if (!numeric_arg(cx, raw)) { return 0.0; }
    return to_length(cx.to_number_value(raw));
}

[[nodiscard]] inline value element_at(context & cx, value self, double i) {
    return cx.lookup_index(self, value::number(i));
}

// Set(O, P, V, true) - 23.1.3's writes all carry Throw=true, so a write that
// does not land (a frozen array, a non-writable `length`, a String receiver's
// index) is a TypeError even in sloppy code. context::store_index records the
// refusal in store_rejected_ and strict_store_check turns it into the throw.
//
// FALSE MEANS THE THROW IS IN FLIGHT and the method must return at once: a
// second throw_error on the way out would consume a second handler
// (context::reentry_scope says why that loses the page's own `try`), and a
// walk that carries on after its first refused write is doing work the
// specification stopped.
//
// WHETHER A DIRECT throw_error HAPPENED is read off context::current_stack: a
// throw the native raised itself is not parked (that is `call`'s doing, and
// throw_pending sees only that) and the landing clears `thrown_`, but the
// landing also moves the handler frame's `ip` to the catch block or pops
// frames above it, and the trace prints both. An uncaught throw fails the
// run, which throw_pending does see. (A compiled frame prints no offset, so
// a catch in one is invisible here; the interpreted tier is what runs the
// suites.) The snapshot costs a string, so the dense-array write below, which
// no JavaScript can refuse or observe, skips it.
[[nodiscard]] inline bool threw_since(context & cx, const std::string & before) {
    return cx.throw_pending() || cx.current_stack() != before;
}
// The same question off context::unwinds(), which costs nothing: a native
// that has read `length` off a revoked proxy has already thrown and landed,
// and must not throw a second time over the handler it consumed.
struct unwind_watch {
    context & cx;
    std::size_t before;
    explicit unwind_watch(context & c) : cx(c), before(c.unwinds()) {}
    [[nodiscard]] bool threw() const { return cx.throw_pending() || cx.unwinds() != before; }
};
[[nodiscard]] inline array_object * dense_array_this(value self); // below
[[nodiscard]] inline bool put_element(context & cx, value self, double i, value v) {
    if (self.is_array()) {
        auto * arr = static_cast<array_object *>(self.as_heap());
        // A typed array coerces and drops out of range, never refuses; an
        // ordinary dense one that is extensible and writable never refuses an
        // index at or below its size. Neither can run a line of JavaScript.
        const bool typed = arr->is_view() || arr->elements != element_kind::none;
        if (typed ||
            (dense_array_this(self) != nullptr && arr->extensible && arr->elements_writable &&
             i >= 0 && i <= static_cast<double>(arr->items.size()))) {
            cx.store_index(self, value::number(i), v);
            return true;
        }
    }
    const std::string before = cx.current_stack();
    cx.clear_store_rejected();
    cx.store_index(self, value::number(i), v);
    if (cx.throw_pending()) { return false; }
    cx.strict_store_check(number_to_string(i));
    return !threw_since(cx, before);
}
// CreateDataPropertyOrThrow(A, k, v), 7.3.5 - a DEFINE, not a [[Set]], so a
// non-writable but configurable slot on a species-made result is overwritten
// rather than refused; the refusal is a TypeError. A dense array takes
// put_element's fast path, which is the same operation there.
[[nodiscard]] inline bool create_element(context & cx, value target, double k, value v) {
    if (target.is_array() && dense_array_this(target) != nullptr) {
        return put_element(cx, target, k, v);
    }
    context::property_descriptor wanted;
    wanted.has_value = wanted.has_writable = wanted.has_enumerable = wanted.has_configurable = true;
    wanted.held = v;
    wanted.writable = wanted.enumerable = wanted.configurable = true;
    if (cx.define_own_property(target, number_to_string(k), wanted)) { return true; }
    if (!cx.throw_pending()) {
        cx.throw_error("TypeError", "Cannot define element " + number_to_string(k));
    }
    return false;
}
// HasProperty over an index - what makes the iteration methods SKIP A HOLE.
//
// Free on a dense array with no holes and no element attributes of its own
// (array_object::element_attrs): every index below its size is present. For
// anything else this is the real HasProperty, and it is what makes
// `Array.prototype.forEach.call({length: 3, 1: 'x'}, f)` call back once rather
// than three times.
[[nodiscard]] inline bool has_element(context & cx, value self, double i) {
    if (self.is_array()) {
        auto * arr = static_cast<array_object *>(self.as_heap());
        if (arr->sparse.empty() && arr->element_attrs.empty()) {
            return i >= 0 && i < static_cast<double>(arr->length());
        }
    }
    return cx.has_property(self, value::number(i));
}

[[nodiscard]] inline bool delete_element(context & cx, value self, double i) {
    if (cx.delete_own_property(self, number_to_string(i))) { return !cx.throw_pending(); }
    if (cx.throw_pending()) { return false; }
    cx.throw_error("TypeError", "Cannot delete property '" + number_to_string(i) + "'");
    return false;
}
[[nodiscard]] inline bool put_length(context & cx, value self, double len) {
    const std::string before = cx.current_stack();
    cx.clear_store_rejected();
    cx.store_property(self, "length", value::number(len));
    if (cx.throw_pending()) { return false; }
    cx.strict_store_check("length");
    return !threw_since(cx, before);
}

// THE FAST PATH'S RECEIVER: a real, ORDINARY Array, whose elements are its own
// std::vector and can therefore be pushed, erased and reversed in place.
//
// A mutating method is one algorithm with two spellings of the storage, not
// two algorithms - this is the branch at the top of each that keeps ordinary
// array code at the speed it was while `this` being anything else takes the
// specified [[Get]]/[[Set]]/[[Delete]] walk.
//
// A TYPED array is NOT one: a view's elements are bytes in somebody else's
// ArrayBuffer and `items` is empty by construction (see array_object::viewed),
// so vector surgery on one would silently do nothing. It goes the generic way,
// where store_index coerces and refuses to grow it, which is what a typed
// array is for.
//
// NOR IS ONE WITH A HOLE, AN ACCESSOR ELEMENT OR A NON-WRITABLE `length`
// (array_object::element_attrs, length_writable): those are exactly the cases
// where vector surgery would skip a getter, fill a hole or write a length the
// specification refuses, so they take the generic walk too.
[[nodiscard]] inline array_object * dense_array_this(value self) {
    if (!self.is_array()) { return nullptr; }
    auto * arr = static_cast<array_object *>(self.as_heap());
    if (arr->is_view() || arr->elements != element_kind::none) { return nullptr; }
    return arr->element_attrs.empty() && arr->length_writable ? arr : nullptr;
}

// A STRING RECEIVER CANNOT BE MUTATED, and the mutating methods have to say so.
//
// Every Set and DeletePropertyOrThrow in 23.1.3's mutating algorithms carries
// Throw=true, so a write that does not land is a TypeError EVEN IN SLOPPY MODE
// - which is the one place these differ from a bare `s[0] = 'x'`. ToObject of a
// string is a String exotic object whose indices and whose `length` are all
// non-writable, so every write in push, pop, shift, unshift, splice, reverse
// and sort fails on one. This engine has no wrapper objects and
// context::store_property drops a write to a primitive silently, so the
// refusal is spelled out here instead.
//
// The other primitives are NOT refused: ToObject(true) is a fresh Boolean
// object with an ordinary, writable `length`, which is why
// `Array.prototype.push.call(true)` is specified to answer 0 rather than throw.
[[nodiscard]] inline bool mutable_receiver(context & cx, value self, const char * method) {
    const value * slot = primitive_slot(self);
    if (!self.is_string() && (slot == nullptr || !slot->is_string())) { return true; }
    cx.throw_error("TypeError",
                   std::string{"Array.prototype."} + method + " cannot modify a String");
    return false;
}

// THE CEILING ON A GENERIC INDEX WALK, and why a mutating method needs one at
// all.
//
// `length` on an array-LIKE is whatever the object says it is, up to 2^53-1,
// and 23.1.3's algorithms walk every index below it: `reverse` swaps len/2
// pairs, `shift` and `unshift` move len-1 elements, `sort` reads len. On a real
// Array that is bounded by the elements that exist. On `{length: 2**53-1}` it
// is bounded by nothing.
//
// The specification's own answer is that the first getter to throw ends the
// walk - which is exactly what test262's `*-near-integer-limit` files assert.
// THIS ENGINE CANNOT DO THAT: a JavaScript throw unwinds the INTERPRETER's
// frames (context::unwind_to_handler) and a native's C++ loop keeps running, so
// there is no signal a loop here could read. The remaining choice is between a
// hang and an error, and a ceiling that THROWS is the honest form of the same
// protection `max_string_length` above is written on.
//
// THE FAST PATH IS UNTOUCHED: a real Array reverses, shifts and sorts through
// its std::vector and never reaches here, so `new Array(1e8).reverse()` still
// works. Only a non-Array receiver claiming a length past 2^24 is refused.
inline constexpr double max_generic_walk = 16777216.0; // 2^24
[[nodiscard]] inline bool generic_walk_ok(context & cx, double len) {
    if (len <= max_generic_walk) { return true; }
    cx.throw_error("RangeError", "the array-like's length is too large to walk");
    return false;
}

// ToObject(this value), 23.1.3's first step in every method: a primitive is
// BOXED (detail::box_primitive) so that `Array.prototype.push.call(true)` sets
// `length` on a Boolean wrapper and answers 0 rather than refusing a write to
// a primitive, and a String receiver's non-writable indices refuse through
// the wrapper as 10.4.3 says. null and undefined pass through to
// coercible_this, which is step 1's TypeError.
[[nodiscard]] inline value array_this(context & cx) {
    return box_primitive(cx, cx.current_this());
}

// RequireObjectCoercible on `this`, which every Array.prototype method begins
// with (as ToObject, 7.1.18, whose step 1 is the same refusal). The methods
// answered a default instead, so `Array.prototype.forEach.call(null, f)` did
// nothing quietly - and 141 of built-ins/Array's failures are "Expected a
// TypeError to be thrown but no exception was thrown at all" (2026-09-07).
//
// FALSE means the throw is already in flight and the caller must return at
// once; it does not mean "carry on with a default".
[[nodiscard]] inline bool coercible_this(context & cx, value self, const char * method) {
    if (!self.is_nullish()) { return true; }
    cx.throw_error("TypeError",
                   std::string{"Array.prototype."} + method + " called on null or undefined");
    return false;
}

// IsArray, 7.2.2: a Proxy is asked through to its target, and a revoked one
// (both slots null) is a TypeError - FALSE with the throw in flight.
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
    // A TYPED ARRAY IS NOT AN Array exotic object (7.2.2 step 2 asks for the
    // exotic kind), though it is an array_object here: Array.isArray, concat's
    // spreading and ArraySpeciesCreate all say no to one.
    out = v.is_array() &&
          static_cast<const array_object *>(v.as_heap())->elements == element_kind::none &&
          !static_cast<const array_object *>(v.as_heap())->is_view();
    return true;
}

// IsCallable, 7.2.3, at the one place every iteration method checks it: an
// absent or non-function callback is a TypeError BEFORE anything is read.
[[nodiscard]] inline bool callable_arg(context & cx, value fn, const char * what) {
    if (fn.is_callable()) { return true; }
    cx.throw_error("TypeError", std::string{what} + " is not a function");
    return false;
}

// ArrayCreate, 10.4.2.2 step 1: a length past 2^32-1 is a RangeError rather
// than an allocation. `map`, `filter`, `with`, `toReversed`, `toSorted` and
// `toSpliced` all build a fresh Array and all inherit it, and every one of them
// goes through array_object::set_js_length - which is the same refusal AND the
// cap that keeps a four-billion-element result from being materialised.
[[nodiscard]] inline array_object * new_array_of_length(context & cx, value out, double len) {
    auto * made = static_cast<array_object *>(out.as_heap());
    if (made->set_js_length(len)) { return made; }
    cx.throw_error("RangeError", "Invalid array length");
    return nullptr;
}

// ArraySpeciesCreate, 10.4.2.3: the object an Array.prototype method fills. A
// fresh Array of `len` unless the receiver IS an array whose `constructor` -
// or that constructor's @@species - is some other constructor, which is then
// constructed with `len`. Undefined means a throw is in flight. (The
// cross-realm Array test of step 4 has nowhere to apply: one context, one
// realm.)
[[nodiscard]] inline value array_species_create(context & cx, value original, double len) {
    const unwind_watch watch{cx};
    bool is_array = false;
    if (!is_array_value(cx, original, is_array) || watch.threw()) { return value::undefined(); }
    value ctor = value::undefined();
    if (is_array) {
        ctor = cx.lookup_property(original, "constructor");
        if (watch.threw()) { return value::undefined(); }
        if (ctor.is_object_like()) {
            ctor = cx.lookup_property(ctor, "@@species");
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
    const value args[1] = {value::number(len)};
    const value out = cx.construct(ctor, args);
    return watch.threw() || !out.is_object_like() ? value::undefined() : out;
}

[[nodiscard]] inline object_object * new_table(context & cx) {
    return static_cast<object_object *>(cx.make_object().as_heap());
}

// `attr_builtin` - { writable: true, enumerable: FALSE, configurable: true } -
// because clause 17 says so of every method in clauses 19 through 28, and
// because an enumerable one is visible to `for (k in Math)`, to
// `Object.keys(Array.prototype)` and to `JSON.stringify` of anything that
// inherits it. That is not a detail: a page that spreads or serialises an
// object was getting the standard library's own methods mixed into its data.
// A BUILT-IN VALUE PROPERTY: `Math.PI`, `Number.MAX_VALUE`, `X.prototype` -
// { writable: false, enumerable: false, configurable: false } for all of them
// (21.3.1, 21.1.2, and each constructor's own clause). Enumerable is what put
// `PI` and `LN2` in `Object.keys(Math)`, which is how
// `Object.defineProperties(obj, Math)` came to be handed a Number as a
// descriptor and throw.
// `Table` is object_object or native_object; both define(name, value, attrs).
template <class Table> void constant(Table * table, std::string_view name, value v) {
    table->define(name, v, attr_none);
}

// A BUILT-IN FUNCTION'S OWN `length` AND `name`, both { false, false, true }
// (10.2.5, and clause 17 for every method in 19 through 28). They are real
// table entries rather than answers synthesised on demand, and the difference
// is measurable:
//
// * `name` WAS synthesised, by context::own_property, out of the C++ object.
//   A synthesised slot cannot refuse a write and cannot be deleted, so
//   test262's verifyProperty saw its isWritable() probe land and its
//   isConfigurable() delete fail, and every `name.js` in the corpus reported
//   BOTH at once - "name descriptor should not be writable; name descriptor
//   should be configurable", 33 times in built-ins/Array alone (measured
//   2026-09-07). An own entry answers all three the way the specification
//   does, because store_property and delete_own_property already consult the
//   attribute bits of a native's table.
// * `length` was ABSENT: a native_fn takes a span and records no arity
//   anywhere, which docs/test262.md names as a known gap. 39 more of
//   built-ins/Array said "length should be an own property". The arity cannot
//   be recovered from the C++ side at all, so it is passed at the INSTALL
//   SITE, from the specification's own clause for that method.
//
// `length` is defined FIRST so that OwnPropertyKeys of a built-in reads
// ["length", "name"], which is the creation order every other engine has.
inline void install_arity(context & cx, native_object * fn, double arity) {
    fn->define("length", value::number(arity), attr_configurable);
    fn->define("name", cx.string(fn->name), attr_configurable);
}

// The arity-less form installs `name` and NOT `length`, which is the honest
// answer for a built-in whose specified arity has not been checked at its
// install site: an absent property is a gap, a wrong one is a wrong answer.
// A BUILT-IN METHOD HAS NO [[Construct]] (clause 17): `new Math.abs()` is a
// TypeError, and isConstructor.js asks Reflect.construct exactly that.
[[nodiscard]] inline native_object * method_native(context & cx, std::string name, native_fn fn) {
    auto * made = cx.allocate<native_object>(std::move(name), std::move(fn));
    made->is_constructor = false;
    return made;
}
// A getter or setter, for define_accessor: a function that is not a constructor.
[[nodiscard]] inline value accessor_fn(context & cx, std::string name, native_fn fn) {
    return value::object(method_native(cx, std::move(name), std::move(fn)));
}
// `Table` is an object_object, or a NATIVE: a built-in that is both callable
// and a namespace - `Object(x)` coerces and `Object.keys` is a static - has to
// be a native carrying properties, and its statics are installed exactly like
// a table's.
template <class Table> void method(context & cx, Table * table, std::string name, native_fn fn) {
    auto * made = method_native(cx, std::move(name), std::move(fn));
    made->define("name", cx.string(made->name), attr_configurable);
    table->define(made->name, value::object(made), attr_builtin);
}
template <class Table>
void method(context & cx, Table * table, std::string name, double arity, native_fn fn) {
    auto * made = method_native(cx, std::move(name), std::move(fn));
    install_arity(cx, made, arity);
    table->define(made->name, value::object(made), attr_builtin);
}

// WHICH HALF OF OwnPropertyKeys A CALLER WANTS. 20.1.2.10
// (getOwnPropertyNames) and 20.1.2.11 (getOwnPropertySymbols) are the same walk
// filtered two different ways, and 7.3.7/7.3.24 want it unfiltered - a symbol
// key is copied by Object.assign and read by Object.defineProperties, which is
// the one place OwnPropertyKeys and EnumerableOwnProperties differ.
enum class key_filter : std::uint8_t {
    strings,
    symbols,
    all
};
// EVERY OWN KEY OF ANY VALUE, including the synthesised ones - and a proxy's
// ownKeys trap. Defined in objects/operations.cpp.
[[nodiscard]] std::vector<std::string> own_property_names(context & cx, value of,
                                                          key_filter which = key_filter::strings);

} // namespace detail

// The install_* functions, one per global the standard library defines.
//
// THESE WERE IN AN ANONYMOUS NAMESPACE when they all shared one translation
// unit. Spread across five, they need external linkage and one shared
// declaration - and `builtins_detail` rather than plain `script` so that
// nothing here can collide with a name another subsystem defines.
//
// The split is safe for the reason the original file states about itself: each
// of these "builds one table and defines one global, and none of them reads
// anything the others wrote".
namespace builtins_detail {

void install_math(context & cx, std::uint64_t seed);
void install_array(context & cx);
// The second half of install_array. One function of 1,212 lines was split at
// the seam before the callback-taking methods; the halves share only the two
// objects passed here, and install_array calls this at exactly the point the
// code used to continue, so every property lands in the order it always did.
void install_array_iteration(context & cx, native_object * array_ctor, object_object * array_proto);
// find / findIndex / findLast / findLastIndex: one [[Get]]-every-index walk,
// answering the item or its index, undefined or -1 on a miss.
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
void install_function(context & cx);
void install_typed_arrays(context & cx);
void install_dynamic_function(context & cx);
void install_generator(context & cx);
void install_class_defined(context & cx);
// See iterator_open_name and its two siblings.
void install_destructuring_iteration(context & cx);

// Used by more than one of those, so defined once here rather than duplicated.
// inline, because a header five translation units include may not define a
// function once per unit.
// `Type.prototype.constructor === Type`, and `Type.name` is its name.
//
// Both are how code identifies a value without trusting `instanceof` - which a
// page can defeat, and which does not work across realms.
// `Object.getPrototypeOf(x).constructor.name` is the standard walk, and with
// either half missing it yields undefined, which compares equal to the other
// undefined it is being tested against and reports a false match.
// `arity` is the constructor's own `length`, from its clause - 1 for Array,
// String, Number, Boolean, Object and every Error, 7 for Date. It is passed
// rather than derived because a native_fn takes a span and records none.
inline void link_constructor(context & cx, object_object * table, const char * name, double arity,
                             value ctor) {
    if (table == nullptr) { return; }
    // `X.prototype.constructor` is { true, false, true } (clause 17), and
    // `X.name` and `X.length` are { false, false, true } (10.2.5). Enumerable
    // in either place is what put "constructor" in
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
