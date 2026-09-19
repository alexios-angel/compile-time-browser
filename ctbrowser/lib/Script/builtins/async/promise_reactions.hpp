#pragma once

#include "../collections/iterator_internal.hpp"
#include "../internal.hpp"
#include "../objects/internal.hpp"

namespace ctbrowser::script::detail::promise {

inline constexpr std::string_view settled_slot = "__settled";

inline constexpr std::string_view value_slot = "__value";

inline constexpr std::string_view rejected_slot = "__rejected";

inline constexpr std::string_view handlers_slot = "__handlers";

inline constexpr std::string_view intrinsic_key = "@#Promise";

inline constexpr std::string_view reaction_job_key = "@#PromiseReactionJob";

inline constexpr std::string_view thenable_job_key = "@#PromiseResolveThenableJob";

inline constexpr std::string_view get_then_key = "@#GetThen";

inline constexpr std::string_view async_from_sync_key = "@#AsyncFromSyncIteratorPrototype";

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
