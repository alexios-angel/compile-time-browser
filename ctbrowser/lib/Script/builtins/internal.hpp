#pragma once
// Private to lib/Script/builtins/. NOT installed and in no file set:
// include/ctbrowser/script/builtins.hpp still declares exactly one function,
// install_builtins(), which is the entire public surface of the standard
// library. This header exists only so the implementation can be more than one
// file - it was 4,118 lines in one until 2026-08-09.

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numbers>
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

#include <map>
#include <memory>

// The JavaScript standard library.
//
// the engine had NONE of this. The whole property surface was `.length`, numeric
// indexing and named lookup on plain objects - no `arr.push`, no `str.split`,
// no `Math.floor`, no `JSON.parse`. A VM can be complete and still useless if
// nothing can be done with a value once you have one, and that is what this
// closes.
//
// It is a SUBSET, chosen by what pages actually call rather than by what the
// spec lists. The omissions that matter are named at the bottom of this file
// rather than left to be discovered.

namespace ctbrowser::script {

// --- argument helpers, the same vocabulary the DOM bindings use -----------

[[nodiscard]] inline value arg_at(std::span<value> args, std::size_t i) {
    return i < args.size() ? args[i] : value::undefined();
}
[[nodiscard]] inline double num_at(std::span<value> args, std::size_t i) {
    return i < args.size() ? context::to_number(args[i]) : 0.0;
}
[[nodiscard]] inline std::string str_at(context & cx, std::span<value> args, std::size_t i) {
    return i < args.size() ? cx.to_string(args[i]) : std::string{};
}

// ToIntegerOrInfinity, 7.1.5 - the coercion EVERY string and array index is
// specified to go through, and the one this file did not have.
//
// NaN BECOMES ZERO, and that is the whole point. `"abc".at(NaN)` used to cast
// NaN straight to `std::size_t`, which is undefined behaviour, and the engine
// HUNG rather than answering - so a page calling `s.at(x)` with an undefined
// `x` froze, with no error and nothing in the console. A missing argument is
// `undefined` and ToNumber(undefined) is NaN, so this path is reached far more
// often than a literal NaN would suggest: `.at()`, `.at(undefined)` and
// `.at({})` all landed on it, as did `.substr(NaN)`.
//
// Truncation is TOWARD ZERO, not floor: `"abc".at(-0.5)` is `at(0)`, not
// `at(-1)`.
[[nodiscard]] inline double index_at(std::span<value> args, std::size_t i) {
    const double n = i < args.size() ? context::to_number(args[i]) : 0.0;
    return std::isnan(n) ? 0.0 : std::trunc(n);
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
    return self.is_string() ? static_cast<string_object *>(self.as_heap())->text
                            : cx.to_string(self);
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
    if (self.is_object() && self.as_heap() == cx.prototype(context::proto_kind::number)) {
        return 0.0;
    }
    cx.throw_error("TypeError", std::string{method} + " requires that 'this' be a Number");
    return std::nan("");
}

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
    return to_length(cx.to_number_value(cx.lookup_property(self, "length")));
}

[[nodiscard]] inline value element_at(context & cx, value self, double i) {
    return cx.lookup_index(self, value::number(i));
}

inline void put_element(context & cx, value self, double i, value v) {
    cx.store_index(self, value::number(i), v);
}

// HasProperty over an index - what makes the iteration methods SKIP A HOLE.
//
// Free on a dense array, which cannot have one: `delete a[i]` is specified to
// leave a hole and array_object is a std::vector with nowhere to put one (see
// context::delete_own_property, which answers true and removes nothing), so
// every index below its size is present. For anything else this is the real
// HasProperty, and it is what makes
// `Array.prototype.forEach.call({length: 3, 1: 'x'}, f)` call back once rather
// than three times.
[[nodiscard]] inline bool has_element(context & cx, value self, double i) {
    if (self.is_array()) {
        auto * arr = static_cast<array_object *>(self.as_heap());
        if (arr->sparse.empty()) { return i >= 0 && i < static_cast<double>(arr->length()); }
    }
    return cx.has_property(self, value::number(i));
}

// DeletePropertyOrThrow over an index (7.3.9), and Set(O, "length", n, true).
// The two writes the MUTATING methods are built out of, named so that push,
// pop, shift, unshift, splice and reverse read like their clauses in 23.1.3
// rather than like calls on the context.
inline void delete_element(context & cx, value self, double i) {
    cx.delete_index(self, value::number(i));
}
inline void put_length(context & cx, value self, double len) {
    cx.store_property(self, "length", value::number(len));
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
[[nodiscard]] inline array_object * dense_array_this(value self) {
    if (!self.is_array()) { return nullptr; }
    auto * arr = static_cast<array_object *>(self.as_heap());
    return arr->is_view() || arr->elements != element_kind::none ? nullptr : arr;
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
    if (!self.is_string()) { return true; }
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

[[nodiscard]] inline object_object * new_table(context & cx) {
    return static_cast<object_object *>(cx.make_object().as_heap());
}

// A native allocated from inside another native - `bind` returns one.
[[nodiscard]] inline native_object * cx_native(context & cx, std::string name, native_fn fn) {
    return cx.allocate<native_object>(std::move(name), std::move(fn));
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
inline void constant(object_object * table, std::string_view name, value v) {
    table->define(name, v, attr_none);
}
inline void constant(native_object * table, std::string_view name, value v) {
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
inline void method(context & cx, object_object * table, std::string name, native_fn fn) {
    auto * made = cx.allocate<native_object>(std::move(name), std::move(fn));
    made->define("name", cx.string(made->name), attr_configurable);
    table->define(made->name, value::object(made), attr_builtin);
}
inline void method(context & cx, object_object * table, std::string name, double arity,
                   native_fn fn) {
    auto * made = cx.allocate<native_object>(std::move(name), std::move(fn));
    install_arity(cx, made, arity);
    table->define(made->name, value::object(made), attr_builtin);
}
// The same, on a NATIVE. A built-in that is both callable and a namespace -
// `Object(x)` coerces and `Object.keys` is a static - has to be a native
// carrying properties, and its statics are installed exactly like a table's.
inline void method(context & cx, native_object * table, std::string name, native_fn fn) {
    auto * made = cx.allocate<native_object>(std::move(name), std::move(fn));
    made->define("name", cx.string(made->name), attr_configurable);
    table->define(made->name, value::object(made), attr_builtin);
}
inline void method(context & cx, native_object * table, std::string name, double arity,
                   native_fn fn) {
    auto * made = cx.allocate<native_object>(std::move(name), std::move(fn));
    install_arity(cx, made, arity);
    table->define(made->name, value::object(made), attr_builtin);
}

// --- promises ---------------------------------------------------------------
//
// SETTLED-ONLY, like the previous engine's. A promise here is an ordinary object carrying
// `__value` and `__rejected`, created ALREADY settled: there is no job queue,
// no microtask checkpoint, and nothing pending. `then` therefore runs its
// callback IMMEDIATELY rather than after the current turn.
//
// That is enough for the shape real pages are written in - `await fetch(url)`,
// `.then(r => r.json())`, `try { await f() } catch (e)` - because every source
// of asynchrony ctbrowser has (assets, timers, rAF) either resolves at once or
// goes through the event loop instead. It is NOT enough for code that depends
// on ordering between a `then` and the surrounding statements, and code written
// against a real event loop can observe the difference.
[[nodiscard]] inline value make_promise(context & cx, value v, bool rejected);

// A promise can be PENDING now.
//
// It was settled-only: created already resolved, `then` running its callback
// immediately, `new Promise(executor)` absent entirely because an executor
// implies pending state. That was enough for `await fetch(url)` and not enough
// for anything that waits - and p5.js starts with
// `Promise.all([waitForDocumentReady(), waitingForTranslator]).then(_globalInit)`,
// so the library could not begin without it.
//
// TODO: a microtask queue. A handler should run at the end of the turn, not
// the moment the promise settles - drain it from run_due_callbacks, after
// timers and before rAF, and again after each event dispatch.
// What is still missing, and it is a real difference: there is no MICROTASK
// QUEUE. A handler runs the moment the promise settles rather than at the end
// of the turn, so code that depends on ordering between a `then` and the
// statements after it sees them in the wrong order. Every promise here settles
// either synchronously or from the event loop, where the distinction does not
// arise; a page written against a real queue can observe it.
//
// State lives on the object: `__value` and `__rejected` as before, plus
// `__settled` and `__handlers`. Keeping the old two means everything that read
// them still works.
[[nodiscard]] inline value make_promise(context & cx, value v, bool rejected);
inline void settle(context & cx, value promise, value with, bool rejected);

// Run one registered handler and settle the promise it produced.
inline void deliver(context & cx, value handler_record, value settled, bool rejected) {
    auto * record = static_cast<object_object *>(handler_record.as_heap());
    value * on_ok = record->find("ok");
    value * on_err = record->find("err");
    value * next = record->find("next");
    const value handler = rejected ? (on_err == nullptr ? value::undefined() : *on_err)
                                   : (on_ok == nullptr ? value::undefined() : *on_ok);
    // A RESUMPTION IS A PROMISE HANDLER. `await` registers the suspended frame
    // on the awaited promise's own handler list, so it queues and orders with
    // every `then` rather than being a second mechanism that races them.
    if (value * waiting = record->find("co"); waiting != nullptr) {
        cx.resume(*waiting, settled, rejected);
        return;
    }
    if (next == nullptr) { return; }
    // `finally` RUNS EITHER WAY AND CHANGES NOTHING. Its callback takes no
    // argument, its return value is ignored, and the outcome - value or
    // rejection - passes straight through to the next promise. It used to call
    // its callback the moment it was registered and hand back the SAME promise,
    // so it ran before the rejection it was supposed to follow and a chain
    // after it saw the wrong link.
    if (value * on_finally = record->find("fin"); on_finally != nullptr) {
        if (on_finally->is_callable()) { (void)cx.call(*on_finally, std::span<const value>{}); }
        settle(cx, *next, settled, rejected);
        return;
    }
    if (!handler.is_callable()) {
        // No handler for how this settled: it passes straight through, so a
        // rejection survives a bare `.then(f)` and a later `.catch` sees it.
        settle(cx, *next, settled, rejected);
        return;
    }
    const value args[1] = {settled};
    const value produced = cx.call(handler, args);
    // A handler returning a promise ADOPTS it, which is what makes a chain of
    // `then`s that each do async work run in order rather than all at once.
    if (produced.is_object()) {
        auto * inner = static_cast<object_object *>(produced.as_heap());
        if (inner->find("__settled") != nullptr) {
            value * inner_settled = inner->find("__settled");
            value * inner_value = inner->find("__value");
            value * inner_rejected = inner->find("__rejected");
            if (context::truthy(*inner_settled)) {
                settle(cx, *next, inner_value == nullptr ? value::undefined() : *inner_value,
                       inner_rejected != nullptr && context::truthy(*inner_rejected));
            } else {
                // still pending: chain onto it
                value * handlers = inner->find("__handlers");
                if (handlers != nullptr && handlers->is_array()) {
                    object_object * record2 = new_table(cx);
                    record2->set("next", *next);
                    static_cast<array_object *>(handlers->as_heap())
                        ->items.push_back(value::object(record2));
                }
            }
            return;
        }
    }
    settle(cx, *next, produced, false);
}

// THE JOB. Delivery is queued rather than run, and a job is a callable plus
// values so the collector traces it - so the C++ work has to be reachable
// through a value, which is what this native is. One per context, made on
// demand and remembered.
[[nodiscard]] inline value delivery_job(context & cx) {
    static const std::string slot = "__deliverJob";
    if (const value existing = cx.global(slot); existing.is_callable()) { return existing; }
    value made =
        value::object(cx.allocate<native_object>(slot, [](context & c, std::span<value> a) {
            if (a.size() >= 3 && a[0].is_object()) {
                deliver(c, a[0], a[1], context::truthy(a[2]));
            }
            return value::undefined();
        }));
    cx.define_global(slot, made);
    return made;
}

// Queue one delivery for the end of the turn.
inline void enqueue_delivery(context & cx, value record, value settled, bool rejected) {
    cx.queue_microtask(delivery_job(cx), {record, settled, value::boolean(rejected)});
}

inline void settle(context & cx, value promise, value with, bool rejected) {
    if (!promise.is_object()) { return; }
    auto * p = static_cast<object_object *>(promise.as_heap());
    value * already = p->find("__settled");
    if (already != nullptr && context::truthy(*already)) { return; } // settle once
    p->define("__value", with, attr_builtin);
    p->define("__rejected", value::boolean(rejected), attr_builtin);
    p->define("__settled", value::boolean(true), attr_builtin);
    value * handlers = p->find("__handlers");
    if (handlers == nullptr || !handlers->is_array()) { return; }
    // COPIED before draining: a handler may register another on this same
    // promise, and appending to the vector being walked invalidates it.
    const std::vector<value> pending = static_cast<array_object *>(handlers->as_heap())->items;
    static_cast<array_object *>(handlers->as_heap())->items.clear();
    // QUEUED, not called. `p.then(f); after();` must run `after` first, and a
    // handler that runs the instant a promise settles can also reenter code
    // that is halfway through its own work.
    for (const value & record : pending) { enqueue_delivery(cx, record, with, rejected); }
}

// `then`/`catch`/`finally` all reduce to: remember what to do for each way this
// can settle, and either do it now or when it settles.
//
// `on_finally` is the third form: one callback for BOTH outcomes, with no say
// in either - its return value is ignored and the outcome passes through. It
// goes in the same record so it queues and orders like everything else, rather
// than being a special case at the call site.
inline value settle_with(context & cx, value on_ok, value on_err,
                         value on_finally = value::undefined()) {
    const value self = cx.current_this();
    if (!self.is_object()) { return self; }
    auto * promise = static_cast<object_object *>(self.as_heap());

    const value next = make_promise(cx, value::undefined(), false);
    static_cast<object_object *>(next.as_heap())
        ->define("__settled", value::boolean(false), attr_builtin);
    object_object * record = new_table(cx);
    record->set("ok", on_ok);
    record->set("err", on_err);
    record->set("next", next);
    if (!on_finally.is_undefined()) { record->set("fin", on_finally); }

    value * settled = promise->find("__settled");
    if (settled != nullptr && context::truthy(*settled)) {
        value * held = promise->find("__value");
        value * state = promise->find("__rejected");
        // Already settled, so there is nothing to wait FOR - but the handler
        // still runs at the end of the turn rather than here. `Promise
        // .resolve(1).then(f); after();` orders them the same way as the
        // pending case, which is the whole point of a queue.
        enqueue_delivery(cx, value::object(record), held == nullptr ? value::undefined() : *held,
                         state != nullptr && context::truthy(*state));
        return next;
    }
    value * handlers = promise->find("__handlers");
    if (handlers != nullptr && handlers->is_array()) {
        static_cast<array_object *>(handlers->as_heap())->items.push_back(value::object(record));
    }
    return next;
}

// then/catch/finally, once for the program rather than three natives per
// promise. They were already receiver-based - settle_with reads current_this -
// so nothing about them was per-instance; and having them here is what makes `p
// instanceof Promise` answerable at all, since a promise then has a prototype to
// walk. Built lazily so a context with no promise ever made pays nothing.
[[nodiscard]] inline object_object * promise_prototype(context & cx) {
    if (object_object * existing = cx.prototype(context::proto_kind::promise)) { return existing; }
    object_object * table = new_table(cx);
    method(cx, table, "then", [](context & c, std::span<value> a) {
        return settle_with(c, a.empty() ? value::undefined() : a[0],
                           a.size() > 1 ? a[1] : value::undefined());
    });
    method(cx, table, "catch", [](context & c, std::span<value> a) {
        return settle_with(c, value::undefined(), a.empty() ? value::undefined() : a[0]);
    });
    method(cx, table, "finally", [](context & c, std::span<value> a) {
        return settle_with(c, value::undefined(), value::undefined(), arg_at(a, 0));
    });
    cx.set_prototype(context::proto_kind::promise, table);
    return table;
}

[[nodiscard]] inline value make_promise(context & cx, value v, bool rejected) {
    object_object * promise = new_table(cx);
    promise->prototype = value::object(promise_prototype(cx));
    promise->define("__value", v, attr_builtin);
    promise->define("__rejected", value::boolean(rejected), attr_builtin);
    promise->define("__settled", value::boolean(true), attr_builtin);
    promise->define("__handlers", cx.make_array(), attr_builtin);
    return value::object(promise);
}

// --- JSON -----------------------------------------------------------------

// QuoteJSONString, 25.5.2.3. The escape TABLE is the specification's, and two
// of its rows were missing: U+0008 and U+000C have the short forms \b and \f
// and were being written as the six-character \u0008 and \u000c forms by the
// fall-through below. Both spellings parse back to the same character, so
// nothing round-tripped wrong; what they cost is every byte-for-byte comparison
// against another engine's output, which is what value-string-escape-ascii.js
// is.
//
// A byte at or above 0x80 passes THROUGH. Strings here are UTF-8 and JSON is a
// UTF-8 format, so the escaping stops at the C0 controls; \uXXXX for a
// non-ASCII code point would be legal and is not what any other engine emits.
inline void quote_json(std::string_view text, std::string & out) {
    out += '"';
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                out += hex[static_cast<unsigned char>(c) & 0xF];
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

// THE SERIALISER IS A STATE OBJECT because 25.5.2 says it is.
//
// What used to be here was one free function of (value, string &): no
// ReplacerFunction, no PropertyList, no gap, no stack. That is not four
// missing features, it is one - every one of them is a field of the
// specification's `state` record, threaded through SerializeJSONProperty, and
// none can be added without the record. The cost of not having it was not only
// the options: with no stack there was no cycle check either, so
// `a = []; a[0] = a; JSON.stringify(a)` recursed until the C++ stack ran out.
// The specification makes that a TypeError, and a TypeError is a page's own bug
// reported to it rather than a dead process.
struct json_writer {
    explicit json_writer(context & c) : cx(c) {}

    context & cx;
    std::string indent;                     // 25.5.2 state.[[Indent]]
    std::string gap;                        // 25.5.2 state.[[Gap]]
    value replacer = value::undefined();    // state.[[ReplacerFunction]]
    std::vector<std::string> property_list; // state.[[PropertyList]]
    bool has_property_list = false;
    // state.[[Stack]], the cycle check. Raw pointers rather than values: the
    // question SerializeJSONObject asks is identity, and nothing pushed here
    // outlives the call that pushed it.
    std::vector<const heap_object *> stack;
    // A throw already happened and the answer is worthless. A native here
    // throws by calling context::throw_error, which unwinds the VM and RETURNS
    // - so every recursive step has to be told to stop rather than discovering
    // it.
    bool failed = false;

    // SerializeJSONProperty, 25.5.2.2, from step 2 - its step 1 is the [[Get]],
    // and the CALLER does that: an array's elements are its std::vector and are
    // not in a property table at all, so reading one is lookup_index and
    // reading an object's member is lookup_property. Passing the value in keeps
    // the two spellings of Get at the two places that know which they need.
    //
    // False means the value is not serialisable at all (undefined, a function,
    // a symbol) and its holder must OMIT it - which is a different answer from
    // the string "null", and is why this returns a bool rather than a string.
    [[nodiscard]] bool serialize(value holder, const std::string & key, value v,
                                 std::string & out) {
        if (failed) { return false; }
        // Steps 2-3: an Object or a BigInt is asked for `toJSON` FIRST, before
        // the replacer sees it. A Date's ISO string comes from there.
        if (v.is_object_like() || v.is_kind(heap_kind::bigint)) {
            const value to_json = cx.lookup_property(v, "toJSON");
            if (to_json.is_callable()) {
                const value args[1] = {cx.string(key)};
                v = cx.call(to_json, args, v);
            }
        }
        // Step 4: the replacer is called with the HOLDER as its receiver and
        // (key, value) as its arguments, so it can see which object a value
        // came out of.
        if (replacer.is_callable()) {
            const value args[2] = {cx.string(key), v};
            v = cx.call(replacer, args, holder);
        }
        if (v.is_null()) {
            out += "null";
            return true;
        }
        if (v.is_boolean()) {
            out += v.as_boolean() ? "true" : "false";
            return true;
        }
        if (v.is_string()) {
            quote_json(static_cast<const string_object *>(v.as_heap())->text, out);
            return true;
        }
        if (v.is_number()) {
            // NaN AND THE INFINITIES ARE NOT JSON. Step 9 serialises every
            // non-finite number as null, and emitting the bare words instead
            // produces output that NO JSON PARSER WILL READ BACK - so a page
            // round-tripping its own data through JSON.parse got a SyntaxError
            // from bytes this engine wrote.
            out += std::isfinite(v.as_number()) ? number_to_string(v.as_number()) : "null";
            return true;
        }
        // A BIGINT IS NOT JSON and there is no lossless spelling for one, so
        // step 10 throws rather than picking between a string and a rounded
        // number.
        if (v.is_kind(heap_kind::bigint)) {
            failed = true;
            cx.throw_error("TypeError", "Do not know how to serialize a BigInt");
            return false;
        }
        if (v.is_array()) { return write_array(v, out); }
        if (v.is_object_like() && !v.is_callable()) { return write_object(v, out); }
        // undefined, a function and a symbol are all OMITTED - which is why
        // round-tripping a value through JSON can lose fields.
        return false;
    }

    // The cycle check, 25.5.2.4/25.5.2.5 step 1. False means it has thrown.
    [[nodiscard]] bool enter(value v) {
        const heap_object * self = v.as_heap();
        for (const heap_object * seen : stack) {
            if (seen == self) {
                failed = true;
                cx.throw_error("TypeError", "Converting circular structure to JSON");
                return false;
            }
        }
        stack.push_back(self);
        return true;
    }

    // How a member list becomes the finished text: 25.5.2.4 step 9 and
    // 25.5.2.5 step 10, which are the same shape twice. With no gap it is one
    // line; with one, every member sits on its own line indented by the INNER
    // indent and the closing bracket by the enclosing one - which is why both
    // indents are parameters rather than read off `indent`. The field has been
    // restored to `stepback` by the time this runs.
    void join(const std::vector<std::string> & parts, const std::string & inner,
              const std::string & stepback, char open, char close, std::string & out) const {
        out += open;
        if (!parts.empty()) {
            const std::string separator = gap.empty() ? std::string{","} : ",\n" + inner;
            if (!gap.empty()) {
                out += '\n';
                out += inner;
            }
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) { out += separator; }
                out += parts[i];
            }
            if (!gap.empty()) {
                out += '\n';
                out += stepback;
            }
        }
        out += close;
    }

    // SerializeJSONArray, 25.5.2.5. An element that is not serialisable is
    // "null" here where an object's member is omitted - the two differ because
    // an array's indices have to stay where they are.
    [[nodiscard]] bool write_array(value v, std::string & out) {
        if (!enter(v)) { return false; }
        const std::string stepback = indent;
        indent += gap;
        std::vector<std::string> parts;
        auto * arr = static_cast<array_object *>(v.as_heap());
        // ITEMS, NOT `length`. An array records an index it refused to
        // materialise and raises `length` over it (see array_object::sparse),
        // so walking to `length` would turn `a[4294967295] = 1` into four
        // billion "null"s. The deviation is array_object's own and every array
        // built-in shares it.
        const std::size_t count = arr->items.size();
        for (std::size_t i = 0; i < count && !failed; ++i) {
            std::string each;
            const value item = cx.lookup_index(v, value::number(static_cast<double>(i)));
            if (!serialize(v, std::to_string(i), item, each)) { each = "null"; }
            parts.push_back(std::move(each));
        }
        const std::string inner = indent;
        indent = stepback;
        stack.pop_back();
        if (failed) { return false; }
        join(parts, inner, stepback, '[', ']', out);
        return true;
    }

    // SerializeJSONObject, 25.5.2.4.
    [[nodiscard]] bool write_object(value v, std::string & out) {
        if (!enter(v)) { return false; }
        const std::string stepback = indent;
        indent += gap;
        std::vector<std::string> keys;
        if (has_property_list) {
            keys = property_list;
        } else if (v.is_object()) {
            // ENUMERABLE OWN STRING KEYS ONLY - EnumerableOwnProperties, step
            // 5. A symbol key is filtered out by each_own_enumerable_key for
            // the reason it always was: this engine spells one
            // "@@sym:N:description" and keeps it in the ordinary property
            // table, so without the filter the internal spelling was serialised
            // into the page's own data.
            static_cast<const object_object *>(v.as_heap())
                ->each_own_enumerable_key([&](const std::string & key) { keys.push_back(key); });
        }
        std::vector<std::string> parts;
        for (const std::string & key : keys) {
            if (failed) { break; }
            std::string each;
            if (!serialize(v, key, cx.lookup_property(v, key), each)) { continue; }
            std::string member;
            quote_json(key, member);
            member += ':';
            if (!gap.empty()) { member += ' '; }
            member += each;
            parts.push_back(std::move(member));
        }
        const std::string inner = indent;
        indent = stepback;
        stack.pop_back();
        if (failed) { return false; }
        join(parts, inner, stepback, '{', '}', out);
        return true;
    }
};

// 25.5.2 steps 4 through 8: the second and third arguments, which decide what
// the serialiser IS before it has seen a value.
inline void read_stringify_options(json_writer & state, value replacer, value space) {
    if (replacer.is_callable()) {
        state.replacer = replacer;
    } else if (replacer.is_array()) {
        // A PROPERTY LIST IS A SET, in insertion order: step 4.b.iii.3 appends
        // only a name that is not already there, so
        // `JSON.stringify(o, ["a", "a"])` writes `a` once.
        for (const value & each : static_cast<array_object *>(replacer.as_heap())->items) {
            std::string name;
            if (each.is_string()) {
                name = static_cast<const string_object *>(each.as_heap())->text;
            } else if (each.is_number()) {
                name = number_to_string(each.as_number());
            } else {
                continue; // anything else contributes nothing to the list
            }
            if (std::find(state.property_list.begin(), state.property_list.end(), name) ==
                state.property_list.end()) {
                state.property_list.push_back(std::move(name));
            }
        }
        state.has_property_list = true;
    }
    // TEN IS THE CEILING for both forms (steps 6 and 7), and a number is
    // ToIntegerOrInfinity'd rather than rounded: `JSON.stringify(o, null, 1.9)`
    // indents by one space.
    if (space.is_number()) {
        const double n = space.as_number();
        const double count = std::isnan(n) ? 0.0 : std::min(10.0, std::trunc(n));
        if (count >= 1) { state.gap.assign(static_cast<std::size_t>(count), ' '); }
    } else if (space.is_string()) {
        const std::string & text = static_cast<const string_object *>(space.as_heap())->text;
        state.gap = text.substr(0, std::min<std::size_t>(10, text.size()));
    }
}

// --- JSON.parse -----------------------------------------------------------
//
// THE GRAMMAR IS JSON's, NOT JavaScript's, and it is far narrower than what
// this reader used to accept. 25.5.1 parses the source against the JSON grammar
// and throws a SyntaxError when it does not fit; the previous reader returned
// `undefined` for a malformed document and accepted `+1`, `01`, `1.`, `.5`, a
// raw control character inside a string, an unknown escape, and anything at all
// AFTER the value. Answering `undefined` instead of throwing is the worse half
// of that: `JSON.parse(x)` inside a try/catch - which is how a page validates
// input - could not fail, so a truncated response became `undefined` and the
// fault surfaced somewhere else entirely.
struct json_reader {
    context & cx;
    std::string_view text;
    std::size_t at = 0;
    bool ok = true;

    // 25.5.1: JSON whitespace is these four characters and nothing else. A form
    // feed or a vertical tab is a SyntaxError, which is what
    // parse/invalid-whitespace.js asserts.
    void skip() {
        while (at < text.size() &&
               (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) {
            ++at;
        }
    }
    void fail() { ok = false; }
    [[nodiscard]] bool eat(char c) {
        if (at < text.size() && text[at] == c) {
            ++at;
            return true;
        }
        fail();
        return false;
    }

    // The whole document: one value, whitespace either side, and NOTHING after
    // it. The trailing check is the one this reader did not do at all, so
    // `JSON.parse("[1,2]junk")` answered [1,2].
    [[nodiscard]] value parse_text() {
        const value out = parse();
        skip();
        if (at != text.size()) { fail(); }
        return ok ? out : value::undefined();
    }

    [[nodiscard]] value parse() {
        skip();
        if (at >= text.size()) {
            fail();
            return value::undefined();
        }
        const char c = text[at];
        if (c == '{') { return parse_object(); }
        if (c == '[') { return parse_array(); }
        if (c == '"') {
            std::string s;
            if (!parse_string(s)) { return value::undefined(); }
            return cx.string(s);
        }
        if (text.compare(at, 4, "true") == 0) {
            at += 4;
            return value::boolean(true);
        }
        if (text.compare(at, 5, "false") == 0) {
            at += 5;
            return value::boolean(false);
        }
        if (text.compare(at, 4, "null") == 0) {
            at += 4;
            return value::null();
        }
        return parse_number();
    }

    // \uXXXX, exactly four hex digits. False rather than reading past the end
    // or treating a non-hex byte as a digit, which the old arithmetic did:
    // `(h | 0x20) - 'a' + 10` turns ANY byte into a number.
    [[nodiscard]] bool read_hex4(std::uint32_t & out) {
        if (at + 4 > text.size()) { return false; }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = text[at + static_cast<std::size_t>(i)];
            int digit = 0;
            if (h >= '0' && h <= '9') {
                digit = h - '0';
            } else if (h >= 'a' && h <= 'f') {
                digit = h - 'a' + 10;
            } else if (h >= 'A' && h <= 'F') {
                digit = h - 'A' + 10;
            } else {
                return false;
            }
            out = out * 16 + static_cast<std::uint32_t>(digit);
        }
        at += 4;
        return true;
    }

    static void append_utf8(std::uint32_t code, std::string & out) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    [[nodiscard]] bool parse_string(std::string & out) {
        if (!eat('"')) { return false; }
        while (at < text.size() && text[at] != '"') {
            const auto byte = static_cast<unsigned char>(text[at]);
            // A RAW CONTROL CHARACTER IS NOT A JSON STRING CHARACTER. A literal
            // newline between quotes has to be spelled \n, and accepting it
            // made this reader read documents no other one will.
            if (byte < 0x20) {
                fail();
                return false;
            }
            if (text[at] != '\\') {
                out += text[at++];
                continue;
            }
            ++at;
            if (at >= text.size()) {
                fail();
                return false;
            }
            const char escape = text[at++];
            switch (escape) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t code = 0;
                if (!read_hex4(code)) {
                    fail();
                    return false;
                }
                // A SURROGATE PAIR IS ONE CODE POINT. Encoding each half
                // separately produces CESU-8, which is not UTF-8 and which no
                // consumer of this engine's strings can read - so an astral
                // character came out of JSON.parse as two replacement
                // characters and went into the page's own data that way.
                if (code >= 0xD800 && code <= 0xDBFF && at + 1 < text.size() && text[at] == '\\' &&
                    text[at + 1] == 'u') {
                    const std::size_t saved = at;
                    at += 2;
                    std::uint32_t low = 0;
                    if (read_hex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        at = saved;
                    }
                }
                // A LONE SURROGATE cannot be spelled in UTF-8 and a string here
                // is UTF-8 bytes, so it becomes U+FFFD rather than an
                // ill-formed string. It is the same deviation that makes
                // `isWellFormed` unimplementable here.
                if (code >= 0xD800 && code <= 0xDFFF) { code = 0xFFFD; }
                append_utf8(code, out);
                break;
            }
            default: fail(); return false;
            }
        }
        if (!eat('"')) { return false; }
        return true;
    }

    // JSONNumber: an optional minus, an integer part with no leading zero, an
    // optional fraction that must have a digit after the point, and an optional
    // exponent that must have one after the marker. `+1`, `01`, `1.`, `.5` and
    // `1e` are each a SyntaxError and each used to parse.
    [[nodiscard]] value parse_number() {
        const std::size_t start = at;
        if (at < text.size() && text[at] == '-') { ++at; }
        if (at >= text.size() || text[at] < '0' || text[at] > '9') {
            fail();
            return value::undefined();
        }
        if (text[at] == '0') {
            ++at;
        } else {
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') { ++at; }
        }
        if (at < text.size() && text[at] == '.') {
            ++at;
            if (at >= text.size() || text[at] < '0' || text[at] > '9') {
                fail();
                return value::undefined();
            }
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') { ++at; }
        }
        if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
            ++at;
            if (at < text.size() && (text[at] == '+' || text[at] == '-')) { ++at; }
            if (at >= text.size() || text[at] < '0' || text[at] > '9') {
                fail();
                return value::undefined();
            }
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') { ++at; }
        }
        // from_chars, NOT strtod: strtod respects LC_NUMERIC, so on a host whose
        // locale writes decimals with a comma `JSON.parse("{\"n\":1.5}")` would
        // stop at the dot and read 1. Goldens are byte-compared across
        // platforms, so a locale-sensitive parser is a portability bug waiting
        // for the first machine that has one.
        const std::string_view digits = text.substr(start, at - start);
        double parsed = 0.0;
        std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
        return value::number(parsed);
    }

    [[nodiscard]] value parse_array() {
        auto * arr = static_cast<array_object *>(cx.make_array().as_heap());
        const value held = value::object(arr);
        ++at; // '['
        skip();
        if (at < text.size() && text[at] == ']') {
            ++at;
            return held;
        }
        while (ok) {
            arr->items.push_back(parse());
            if (!ok) { break; }
            skip();
            if (at < text.size() && text[at] == ',') {
                ++at;
                continue;
            }
            // No comma, so the array must end here. A trailing comma lands back
            // in parse() on the next round and fails there, which is what the
            // grammar says.
            (void)eat(']');
            break;
        }
        return held;
    }

    [[nodiscard]] value parse_object() {
        auto * obj = new_table(cx);
        const value held = value::object(obj);
        ++at; // '{'
        skip();
        if (at < text.size() && text[at] == '}') {
            ++at;
            return held;
        }
        while (ok) {
            skip();
            std::string key;
            if (!parse_string(key)) { break; }
            skip();
            if (!eat(':')) { break; }
            const value each = parse();
            if (!ok) { break; }
            obj->set(key, each);
            skip();
            if (at < text.size() && text[at] == ',') {
                ++at;
                continue;
            }
            (void)eat('}');
            break;
        }
        return held;
    }
};

// InternalizeJSONProperty, 25.5.1.1 - the reviver walk.
//
// POST-ORDER: a child is revived and written back before its parent is offered
// to the reviver, so a reviver rebuilding a Date out of a string sees a
// finished object. A reviver returning `undefined` DELETES the property, which
// is how one filters, and is why this cannot be a plain map.
inline value internalize_json(context & cx, value holder, const std::string & key, value held,
                              value reviver, std::uint32_t depth) {
    // The recursion follows the parsed document's shape, so it is bounded by
    // nesting - but a reviver may graft an object onto itself and this walk
    // would then never end. The ceiling is the VM's own for the same reason the
    // VM has one.
    if (depth > context::reentry_ceiling) { return value::undefined(); }
    if (held.is_array()) {
        auto * arr = static_cast<array_object *>(held.as_heap());
        for (std::size_t i = 0; i < arr->items.size(); ++i) {
            const value revived =
                internalize_json(cx, held, std::to_string(i), arr->items[i], reviver, depth + 1);
            // A DELETED ELEMENT IS A HOLE, NOT A SHORTER ARRAY: 25.5.1.1 does
            // [[Delete]] and leaves `length` where it was. An array here has no
            // holes to write (see context::delete_own_property), so a deleted
            // element reads back as `undefined`, which is what it would be.
            if (i < arr->items.size()) { arr->items[i] = revived; }
        }
    } else if (held.is_object()) {
        // The key list is taken BEFORE the walk (step 3.d.i takes OwnPropertyKeys
        // once): a property the reviver adds must not be visited, and one it
        // deletes ahead of the cursor must not be either.
        auto * obj = static_cast<object_object *>(held.as_heap());
        std::vector<std::string> keys;
        obj->each_own_enumerable_key([&](const std::string & k) { keys.push_back(k); });
        for (const std::string & each : keys) {
            value * slot = obj->find(each);
            if (slot == nullptr) { continue; } // an earlier round deleted it
            const value revived = internalize_json(cx, held, each, *slot, reviver, depth + 1);
            if (revived.is_undefined()) {
                (void)obj->erase(each);
            } else {
                obj->set(each, revived);
            }
        }
    }
    const value args[2] = {cx.string(key), held};
    return cx.call(reviver, args, holder);
}

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
void install_string(context & cx);
void install_base64(context & cx);
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

// Object
// One `Object.defineProperty`, used by both it and defineProperties.
//
// It is now a READER: it turns a JavaScript descriptor object into a
// context::property_descriptor and hands it to context::define_own_property,
// which is where 10.1.6.3's validation and the four property tables live. What
// this function still has to get right is the difference between a field that
// is ABSENT and one that is present-and-undefined, because the whole of
// ValidateAndApplyPropertyDescriptor is written in those terms:
//
// A descriptor with NO `value`, `get` or `set` describes ATTRIBUTES ONLY, and
// must leave the existing value alone. Writing undefined instead is how
// `Object.defineProperty(C, "prototype", {writable: false})` - which is what
// every Babel-transpiled class emits - wiped the prototype it had just filled
// in, and the class's methods vanished with it.
//
// And a FUNCTION IS AN OBJECT. Babel defines onto the constructor as well as
// onto its prototype, and `is_object()` is false for a closure, so half of
// every transpiled class was silently dropped.
// HasProperty AND Get, NOT a lookup in the descriptor's own table.
//
// 6.2.6.5 reads each field with HasProperty followed by Get, both of which walk
// the PROTOTYPE CHAIN and both of which run an accessor. Reading `from->find()`
// instead sees only own data properties - so a descriptor built by
// `new Con()` over a prototype carrying a `writable` getter described nothing
// at all. test262 devotes ~250 files in built-ins/Object/defineProperties to
// exactly that shape (15.2.3.7-5-b-*), and each one names the property it
// inherits.
[[nodiscard]] inline context::property_descriptor read_descriptor(context & cx, value from) {
    context::property_descriptor out;
    const auto field = [&](const char * name, bool & has, value & into) {
        if (cx.has_property(from, cx.string(name))) {
            has = true;
            into = cx.lookup_property(from, name);
        }
    };
    const auto flag = [&](const char * name, bool & has, bool & into) {
        value held = value::undefined();
        bool present = false;
        field(name, present, held);
        if (present) {
            has = true;
            into = cx.truthy(held);
        }
    };
    field("value", out.has_value, out.held);
    field("get", out.has_get, out.getter);
    field("set", out.has_set, out.setter);
    flag("writable", out.has_writable, out.writable);
    flag("enumerable", out.has_enumerable, out.enumerable);
    flag("configurable", out.has_configurable, out.configurable);
    return out;
}

inline bool define_one(context & cx, value target, const std::string & key, value descriptor) {
    return cx.define_own_property(target, key, read_descriptor(cx, descriptor));
}

} // namespace builtins_detail

} // namespace ctbrowser::script
