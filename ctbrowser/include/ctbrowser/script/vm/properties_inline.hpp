#pragma once

#include "../vm.hpp"

namespace ctbrowser::script {

// ECMA-262 ToInt32 / ToUint32: NaN and the infinities are 0, everything
// else truncates toward zero and wraps modulo 2^32.
[[nodiscard]] inline auto context::to_int32(value v) -> std::int32_t {
    return static_cast<std::int32_t>(to_uint32(v));
}

[[nodiscard]] inline auto context::to_uint32(value v) -> std::uint32_t {
    const double n = to_number(v);
    if (!std::isfinite(n)) { return 0; }
    const double truncated = std::trunc(n);
    return static_cast<std::uint32_t>(
        static_cast<std::int64_t>(std::fmod(truncated, 4294967296.0)));
}

// THE [[Prototype]] KIND OF A FUNCTION VALUE by its shape: the three above
// for the generator and async forms, `function` for the rest and for
// every native.
[[nodiscard]] inline auto context::function_proto_kind(value v) noexcept -> proto_kind {
    if (!v.is_kind(heap_kind::function)) { return proto_kind::function; }
    const function_proto * fp = static_cast<closure_object *>(v.as_heap())->proto;
    if (fp == nullptr) { return proto_kind::function; }
    if (fp->is_generator) {
        return fp->is_async ? proto_kind::async_generator_function : proto_kind::generator_function;
    }
    return fp->is_async ? proto_kind::async_function : proto_kind::function;
}

// THE IMPLICIT PROTOTYPES FOR A VALUE'S KIND, most derived first.
//
// Property lookup falls back to these tables (see lookup_property), and
// anything else that asks "what is this value's prototype chain" -
// `instanceof` - has to see the SAME ones or it disagrees with `.`.
//
// Three entries because that is the deepest chain there is here - a typed
// array is TypedArray.prototype, then Array.prototype, then
// Object.prototype - and nullptr pads the rest.
[[nodiscard]] inline auto context::implicit_prototypes(value v) const
    -> std::array<object_object *, 3> {
    const auto table = [this](proto_kind kind) { return prototype(kind); };
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
    // So `(1n).toString(16)` finds a method at all - a bigint is a
    // primitive, so it has no own properties and the prototype is the only
    // place a method can live.
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

inline auto context::set_prototype(proto_kind kind, object_object * table) -> void {
    prototypes_[static_cast<std::size_t>(kind)] = table;
}

[[nodiscard]] inline auto context::prototype(proto_kind kind) const -> object_object * {
    return prototypes_[static_cast<std::size_t>(kind)];
}

// 2026-01-01T00:00:00Z
inline auto context::set_clock(std::function<double()> clock) -> void {
    clock_ = std::move(clock);
}

[[nodiscard]] inline auto context::clock_ms() const -> double {
    return clock_ ? clock_() : fixed_epoch_base;
}

// How an `async` function's return value becomes a promise. The VM cannot
// build one itself - a promise is an ordinary object carrying then/catch/
// finally natives, and those live in the standard library - so builtins
// installs these hooks. Without them (a VM with no builtins) an async
// function returns its plain value, which `await` still handles.
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

// HostPromiseRejectionTracker (27.2.1.9), for the host to wire
// `unhandledrejection` / `rejectionhandled` from: called with `handled`
// false when a promise is rejected while no reaction was ever attached to
// it (RejectPromise step 7, [[PromiseIsHandled]] false), and with `handled`
// true when a reaction is later attached to such a promise
// (PerformPromiseThen step 9 - `then`, `catch`, `finally`, an `await`).
// No JS semantics change and nothing is dispatched here; without a
// tracker the calls are dropped. [[PromiseIsHandled]] is the promise's
// private `@#PromiseIsHandled` slot (builtins/async.cpp).
inline auto context::set_rejection_tracker(std::function<void(value promise, bool handled)> track)
    -> void {
    rejection_tracker_ = std::move(track);
}

inline auto context::track_promise_rejection(value promise, bool handled) -> void {
    if (rejection_tracker_) { rejection_tracker_(promise, handled); }
}

// [[PromiseIsHandled]]: read and set through one member so attach_resume
// (an await is a PerformPromiseThen) and the standard library agree.
[[nodiscard]] inline auto context::promise_is_handled(value promise) -> bool {
    if (!promise.is_object()) { return true; }
    const value * flag = static_cast<object_object *>(promise.as_heap())
                             ->find(std::string_view{"@#PromiseIsHandled"});
    return flag != nullptr && truthy(*flag);
}

// PerformPromiseThen steps 9 and 11 for `promise`: the tracker's "handle"
// when it was rejected unhandled, then the flag.
inline auto context::mark_promise_handled(value promise) -> void {
    if (!promise.is_object() || promise_is_handled(promise)) { return; }
    auto * p = static_cast<object_object *>(promise.as_heap());
    const value * rejected = p->find(std::string_view{"__rejected"});
    const value * settled = p->find(std::string_view{"__settled"});
    if (settled != nullptr && truthy(*settled) && rejected != nullptr && truthy(*rejected)) {
        track_promise_rejection(promise, true);
    }
    p->define(std::string_view{"@#PromiseIsHandled"}, value::boolean(true), attr_none);
}

// A PENDING promise, and settling one. What a host needs to model work that
// finishes later - a fetch off the event loop, a decode, a file read - now
// that `await` can actually suspend on one.
//
// The standard library owns what settling MEANS, including queueing the
// handlers, so both go through the hooks it installed rather than reaching
// into the object's properties.
[[nodiscard]] inline auto context::make_pending_promise() -> value {
    return pending_promise_factory_ ? pending_promise_factory_(*this) : value::undefined();
}

inline auto context::settle_promise(value promise, value with, bool rejected) -> void {
    if (promise_settler_) { promise_settler_(*this, promise, with, rejected); }
}

[[nodiscard]] inline auto context::make_promise(value v, bool rejected) -> value {
    return promise_factory_ ? promise_factory_(*this, v, rejected) : v;
}

// WHAT AN ASYNC FUNCTION'S `return` DOES - op::wrap_promise, and the one
// half of async that never suspends.
//
// Here rather than in run_loop because the compiled tier calls it too:
// `ct_aot_wrap_promise` is this member and nothing else, so the two tiers
// cannot spell the already-a-promise test differently. Three properties a
// caller must not "simplify":
//
//   is_object() is heap_kind::object EXACTLY (value.hpp), so an array, a
//   function or a proxy returned from an async function is ALWAYS
//   re-wrapped. That is the interpreter's behaviour and therefore correct
//   by definition.
//
//   The shape test is an own `__value`, which is NOT the test
//   is_pending_promise uses (`__settled` present and false). The two are
//   deliberately different and unifying them would change what
//   `return somePendingPromise` does inside an async function: a pending
//   promise carries an own __value of undefined, so it is passed through
//   UNWRAPPED here.
//
//   With no promise_factory_ installed make_promise is the IDENTITY, so
//   this is not "the result is always an object" and nothing may fold it
//   that way. A context without install_builtins has no factory at all.
[[nodiscard]] inline auto context::wrap_in_promise(value v) -> value {
    if (v.is_object() && static_cast<object_object *>(v.as_heap())->find("__value") != nullptr) {
        return v;
    }
    return make_promise(v, false);
}

} // namespace ctbrowser::script
