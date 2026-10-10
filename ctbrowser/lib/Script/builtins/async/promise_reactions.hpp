//===- promise_reactions.hpp - Promise slots and jobs -----------*- C++ -*-===//
//
// The promise machinery shared by the Promise builtins (async.cpp), disposable
// stacks and the VM: the slot layout of a promise object, the resolving
// functions, the two reaction jobs, PerformPromiseThen and
// NewPromiseCapability. Section numbers are ECMA-262.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../collections/iterator_internal.hpp"
#include "../internal.hpp"
#include "../objects/internal.hpp"

namespace ctbrowser::script::detail::promise {

/// The four private-key slots every promise object carries (the promise_*_key
/// constants in value/objects.hpp). The VM reads the same keys, so they are
/// fixed: [[PromiseState]] is split into `settled` and `rejected`, `value` is
/// [[PromiseResult]], and `handlers` is the PromiseReaction list.
inline constexpr std::string_view settled_slot = promise_settled_key;

inline constexpr std::string_view value_slot = promise_result_key;

inline constexpr std::string_view rejected_slot = promise_rejected_key;

inline constexpr std::string_view handlers_slot = promise_reactions_key;

/// Private keys on Promise.prototype (no script can spell one, and
/// OwnPropertyKeys never lists one): the %Promise% intrinsic, the two job
/// natives and the `then` reader. Kept on the prototype so the collector
/// traces them like any property.
inline constexpr std::string_view intrinsic_key = CTBROWSER_PRIVATE_KEY("Promise");

inline constexpr std::string_view reaction_job_key = CTBROWSER_PRIVATE_KEY("PromiseReactionJob");

inline constexpr std::string_view thenable_job_key =
    CTBROWSER_PRIVATE_KEY("PromiseResolveThenableJob");

inline constexpr std::string_view get_then_key = CTBROWSER_PRIVATE_KEY("GetThen");

/// Private key caching %AsyncFromSyncIteratorPrototype%.
inline constexpr std::string_view async_from_sync_key =
    CTBROWSER_PRIVATE_KEY("AsyncFromSyncIteratorPrototype");

/// A PromiseCapability record (27.2.1.1). For %Promise% itself `resolve`
/// and `reject` stay undefined and the promise is settled directly; the
/// functions are made only when script needs them (see materialise).
struct capability {
    value promise = value::undefined();
    value resolve = value::undefined();
    value reject = value::undefined();
};

[[nodiscard]] value slot(object_object * o, std::string_view name);
[[nodiscard]] bool is_promise(value v);
[[nodiscard]] value init_promise(context & cx, value self);
[[nodiscard]] value pending_promise(context & cx);
[[nodiscard]] std::pair<value, value> resolvers_for(context & cx, value promise);
void materialise(context & cx, capability & cap);
void settle_capability(context & cx, const capability & cap, value with, bool rejected);
void deliver(context & cx, value handler_record, value argument, bool rejected);
[[nodiscard]] value job_native(context & cx, std::string_view key, native_fn fn);
[[nodiscard]] value reaction_job(context & cx);
[[nodiscard]] value thenable_job(context & cx);
void enqueue_reaction(context & cx, value record, value argument, bool rejected);
void settle(context & cx, value promise, value with, bool rejected);
[[nodiscard]] value get_then(context & cx, value resolution, bool & threw, value & thrown);
void resolve_promise(context & cx, value promise, value resolution);
void perform_then(context & cx, value promise, value on_ok, value on_err, const capability & cap);
[[nodiscard]] value intrinsic_promise(context & cx);
[[nodiscard]] bool new_capability(context & cx, value ctor, capability & out);
[[nodiscard]] bool species_constructor(context & cx, value o, value fallback, value & out);
[[nodiscard]] bool promise_resolve(context & cx, value ctor, value x, value & out);
[[nodiscard]] object_object * promise_prototype(context & cx);

} // namespace ctbrowser::script::detail::promise
