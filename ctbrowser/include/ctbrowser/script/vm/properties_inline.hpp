#pragma once
//===- properties_inline.hpp - Inline property members ----------*- C++ -*-===//
//
// Inline definitions of `context`'s property and value helpers: numeric-key
// tests, ToInt32/ToUint32, implicit prototype chains, the clock, and the
// promise hooks the standard library installs.
//
//===----------------------------------------------------------------------===//

#include "../vm.hpp"
#include "ctbrowser/core/number.hpp"
#include "ctbrowser/core/number_format.hpp"

namespace ctbrowser::script {

/// Whether CanonicalNumericIndexString (7.1.21) is not undefined for `key`,
/// that is, whether the key is the canonical spelling of a Number: every array
/// index, and "-0", "1.5", "-1", "NaN", "Infinity", "4294967295" besides. On a
/// typed array every such key names an element, valid or not, and never an
/// ordinary property (10.4.5): `ta["1.5"]` is undefined whatever
/// %TypedArray%.prototype holds, while `ta["1.0"]` and `ta["+1"]` are ordinary
/// properties, not being canonical.
[[nodiscard]] inline bool canonical_numeric_key(std::string_view key) {
    if (key == "-0") { return true; }
    if (key.empty()) { return false; }
    const char c = key.front();
    if (!((c >= '0' && c <= '9') || c == '-' || c == 'I' || c == 'N')) { return false; }
    return ctbrowser::number_to_string(ctbrowser::string_to_number(key)) == key;
}

/// Whether a function is a method without a `prototype` property (15.4.4
/// DefineMethod makes it with MakeMethod and never MakeConstructor; accessors
/// likewise). Such a function carries a [[HomeObject]] (`__home`, the
/// compiler's slot) and was not given a `prototype` of its own, as a class
/// constructor is at its definition. A generator method keeps one (15.5.4 /
/// 15.6.4 give it a prototype object). An object literal's method has its
/// home wired only when it says `super` or `eval(`, so one that does neither
/// still answers with a prototype.
[[nodiscard]] inline bool method_without_prototype(closure_object & closure) {
    return closure.find("__home") != nullptr &&
           (closure.proto == nullptr || !closure.proto->is_generator);
}

/// Whether this Number is an array index (6.1.7: an integral 0 .. 2^32-2).
/// -0 is one, since its ToString is "0". A Number key that is not one (1.5,
/// -1, NaN) is the property named by its ToString, never an element a
/// truncation reached.
[[nodiscard]] inline bool array_index_number(double d) noexcept {
    return d >= 0 && d <= static_cast<double>(array_object::max_index) && d == std::trunc(d);
}

/// ECMA-262 ToInt32 / ToUint32: NaN and the infinities are 0, everything else
/// truncates toward zero and wraps modulo 2^32.
[[nodiscard]] inline auto context::to_int32(value v) -> std::int32_t {
    return ctbrowser::number_to_int32(to_number(v));
}

[[nodiscard]] inline auto context::to_uint32(value v) -> std::uint32_t {
    return ctbrowser::number_to_uint32(to_number(v));
}

/// The proto_kind for a function value's [[Prototype]]: generator_function,
/// async_generator_function or async_function for those forms, and `function`
/// for everything else, including every native.
[[nodiscard]] inline auto context::function_proto_kind(value v) noexcept -> proto_kind {
    if (!v.is_kind(heap_kind::function)) { return proto_kind::function; }
    const function_proto * fp = static_cast<closure_object *>(v.as_heap())->proto;
    if (fp == nullptr) { return proto_kind::function; }
    if (fp->is_generator) {
        return fp->is_async ? proto_kind::async_generator_function : proto_kind::generator_function;
    }
    return fp->is_async ? proto_kind::async_function : proto_kind::function;
}

/// The implicit prototypes for a value's kind, most derived first.
///
/// Property lookup falls back to these tables (see lookup_property), and
/// anything else that asks for a value's prototype chain, such as
/// `instanceof`, has to see the same ones or it disagrees with `.`.
///
/// Three entries because that is the deepest chain there is here (a typed
/// array is TypedArray.prototype, then Array.prototype, then
/// Object.prototype); nullptr pads the rest.
[[nodiscard]] inline auto context::implicit_prototypes(value v) const
    -> std::array<object_object *, 3> {
    // The current realm's, asked once rather than per entry.
    const auto & prototypes = current_realm().intrinsics->prototypes;
    const auto table = [&prototypes](proto_kind kind) {
        return prototypes[static_cast<std::size_t>(kind)];
    };
    if (v.is_array()) {
        auto * arr = static_cast<array_object *>(v.as_heap());
        if (arr->elements != element_kind::none) {
            return {table(proto_kind::typed_array), table(proto_kind::array),
                    table(proto_kind::object)};
        }
        return {table(proto_kind::array), table(proto_kind::object), nullptr};
    }
    if (v.is_string()) { return {table(proto_kind::string), table(proto_kind::object), nullptr}; }
    if (v.is_number()) { return {table(proto_kind::number), table(proto_kind::object), nullptr}; }
    // So `(1n).toString(16)` finds a method at all: a bigint is a primitive,
    // so it has no own properties and the prototype is the only place a
    // method can live.
    if (v.is_kind(heap_kind::bigint)) {
        return {table(proto_kind::bigint), table(proto_kind::object), nullptr};
    }
    if (v.is_boolean()) { return {table(proto_kind::boolean), table(proto_kind::object), nullptr}; }
    if (v.is_kind(heap_kind::symbol)) {
        return {table(proto_kind::symbol), table(proto_kind::object), nullptr};
    }
    if (v.is_kind(heap_kind::function) || v.is_kind(heap_kind::native)) {
        const proto_kind own = function_proto_kind(v);
        if (own != proto_kind::function && table(own) != nullptr) {
            return {table(own), table(proto_kind::function), table(proto_kind::object)};
        }
        return {table(proto_kind::function), table(proto_kind::object), nullptr};
    }
    if (v.is_object()) { return {table(proto_kind::object), nullptr, nullptr}; }
    return {nullptr, nullptr, nullptr};
}

/// Sets the current realm's prototype table for `kind` (see
/// realm_intrinsics::prototypes).
inline auto context::set_prototype(proto_kind kind, object_object * table) -> void {
    current_realm().intrinsics->prototypes[static_cast<std::size_t>(kind)] = table;
}

[[nodiscard]] inline auto context::prototype(proto_kind kind) const -> object_object * {
    return current_realm().intrinsics->prototypes[static_cast<std::size_t>(kind)];
}

/// Installs the clock behind clock_ms(). With no clock installed, time is
/// pinned at fixed_epoch_base (2026-01-01T00:00:00Z) so runs are reproducible.
inline auto context::set_clock(std::function<double()> clock) -> void {
    clock_ = std::move(clock);
}

[[nodiscard]] inline auto context::clock_ms() const -> double {
    return clock_ ? clock_() : fixed_epoch_base;
}

/// Installs the hook that makes an `async` function's return value a promise.
/// The VM cannot build one itself (a promise is an ordinary object carrying
/// then/catch/finally natives, and those live in the standard library), so
/// builtins installs these hooks. Without them (a VM with no builtins) an
/// async function returns its plain value, which `await` still handles.
inline auto context::set_pending_promise_factory(std::function<value(context &)> make) -> void {
    pending_promise_factory_ = std::move(make);
}

inline auto context::set_promise_settler(std::function<void(context &, value, value, bool)> settle)
    -> void {
    promise_settler_ = std::move(settle);
}

inline auto context::set_promise_factory(std::function<value(context &, value, bool)> make)
    -> void {
    promise_factory_ = std::move(make);
}

/// HostPromiseRejectionTracker (27.2.1.9), for the host to wire
/// `unhandledrejection` / `rejectionhandled` from. Called with `handled` false
/// when a promise is rejected while no reaction was ever attached to it
/// (RejectPromise step 7, [[PromiseIsHandled]] false), and with `handled` true
/// when a reaction is later attached to such a promise (PerformPromiseThen
/// step 9: `then`, `catch`, `finally`, an `await`). No JS semantics change and
/// nothing is dispatched here; without a tracker the calls are dropped.
/// [[PromiseIsHandled]] is the promise's private
/// CTBROWSER_PRIVATE_KEY("PromiseIsHandled") slot (builtins/async.cpp).
inline auto context::set_rejection_tracker(std::function<void(value promise, bool handled)> track)
    -> void {
    rejection_tracker_ = std::move(track);
}

inline auto context::set_checkpoint_hook(std::function<void()> hook) -> void {
    checkpoint_hook_ = std::move(hook);
}

inline auto context::track_promise_rejection(value promise, bool handled) -> void {
    if (rejection_tracker_) { rejection_tracker_(promise, handled); }
}

/// [[PromiseIsHandled]]: read and set through one member so attach_resume (an
/// await is a PerformPromiseThen) and the standard library agree.
[[nodiscard]] inline auto context::promise_is_handled(value promise) -> bool {
    if (!promise.is_object()) { return true; }
    const value * flag = static_cast<object_object *>(promise.as_heap())
                             ->find(std::string_view{CTBROWSER_PRIVATE_KEY("PromiseIsHandled")});
    return flag != nullptr && truthy(*flag);
}

/// PerformPromiseThen steps 9 and 11 for `promise`: report "handled" to the
/// tracker if it was rejected unhandled, then set the flag.
inline auto context::mark_promise_handled(value promise) -> void {
    if (!promise.is_object() || promise_is_handled(promise)) { return; }
    auto * p = static_cast<object_object *>(promise.as_heap());
    const value * rejected = p->find(promise_rejected_key);
    const value * settled = p->find(promise_settled_key);
    if (settled != nullptr && truthy(*settled) && rejected != nullptr && truthy(*rejected)) {
        track_promise_rejection(promise, true);
    }
    p->define(std::string_view{CTBROWSER_PRIVATE_KEY("PromiseIsHandled")}, value::boolean(true),
              attr_none);
}

/// Makes a pending promise, which a host uses to model work that finishes
/// later (a fetch off the event loop, a decode, a file read) and that `await`
/// can suspend on. settle_promise resolves or rejects it.
///
/// The standard library owns what settling means, including queueing the
/// handlers, so both go through the hooks it installed rather than reaching
/// into the object's properties.
[[nodiscard]] inline auto context::make_pending_promise() -> value {
    return pending_promise_factory_ ? pending_promise_factory_(*this) : value::undefined();
}

inline auto context::settle_promise(value promise, value with, bool rejected) -> void {
    if (promise_settler_) { promise_settler_(*this, promise, with, rejected); }
}

[[nodiscard]] inline auto context::make_promise(value v, bool rejected) -> value {
    return promise_factory_ ? promise_factory_(*this, v, rejected) : v;
}

/// What an async function's `return` does: op::wrap_promise, the one half of
/// async that never suspends.
///
/// Here rather than in run_loop because ahead-of-time compiled code calls it
/// too: `ct_aot_wrap_promise` is this member and nothing else, so the
/// interpreter and compiled code cannot spell the already-a-promise test
/// differently. Three properties a caller must not "simplify":
///
///   is_object() is exactly heap_kind::object (value.hpp), so an array, a
///   function or a proxy returned from an async function is always
///   re-wrapped. That is the interpreter's behaviour and therefore correct by
///   definition.
///
///   The shape test is an own promise_result_key slot, which is not the test
///   is_pending_promise uses (promise_settled_key present and false). The two
///   are deliberately different, and unifying them would change what
///   `return somePendingPromise` does inside an async function: a pending
///   promise carries a result slot of undefined, so it is passed through
///   unwrapped here.
///
///   With no promise_factory_ installed make_promise is the identity, so the
///   result is not always an object and nothing may fold it that way. A
///   context without install_builtins has no factory at all.
[[nodiscard]] inline auto context::wrap_in_promise(value v) -> value {
    if (v.is_object() &&
        static_cast<object_object *>(v.as_heap())->find(promise_result_key) != nullptr) {
        return v;
    }
    return make_promise(v, false);
}

} // namespace ctbrowser::script
