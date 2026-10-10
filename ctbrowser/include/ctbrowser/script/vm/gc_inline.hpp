#pragma once
//===- gc_inline.hpp - Inline GC members of context -------------*- C++ -*-===//
//
// Inline definitions of `context`'s garbage-collection and await plumbing:
// external roots, the stress mode, safepoints, threshold-driven collection
// and the helpers that suspend a coroutine on a pending promise.
//
//===----------------------------------------------------------------------===//

#include "../vm.hpp"

namespace ctbrowser::script {

inline auto context::set_external_roots(std::function<void(const root_visitor &)> enumerate)
    -> void {
    external_roots_ = std::move(enumerate);
}

inline auto context::set_weak_prune(std::function<void(const survives_fn &)> prune) -> void {
    weak_prune_ = std::move(prune);
}

/// Test mode: collect the whole heap at every safepoint. Very slow, but it is
/// the only way to exercise the rooting rules the ABI requires, since an
/// ordinary run collects only when the heap is due and only at a call
/// boundary.
inline auto context::set_gc_stress(bool on) noexcept -> void {
    gc_stress_ = on;
}

[[nodiscard]] inline auto context::gc_stress() const noexcept -> bool {
    return gc_stress_;
}

/// A point where the ABI allows a collection. Under stress it always collects;
/// otherwise only when the heap has passed `collect_if_due`'s threshold, so a
/// synchronous script that never yields is bounded the way a page on a timer
/// is (some WPT files run thousands of subtests in one top-level script).
/// The cost is one compare. It is called from `invoke`, every C++ entry into
/// JavaScript, which includes `Function.prototype.apply` and so every
/// testharness.js subtest. Interpreted JS-to-JS calls are deliberately not
/// safepoints: ctcompile/test/Analysis/Escape/Cycle.cpp checks that
/// `churn(1000)` collects exactly once under stress and returns with its 4,000
/// dead nodes unswept.
inline auto context::safepoint() -> void {
    if (gc_stress_) [[unlikely]] {
        (void)collect();
        return;
    }
    (void)collect_if_due();
}

/// How many collections have run. A test that forces GC and checks an answer
/// proves nothing on its own: the answer is the same if no collection
/// happened, which is exactly how a stress mode that silently does nothing
/// looks. Asserting this counter grew is the proof.
[[nodiscard]] inline auto context::collections() const noexcept -> std::size_t {
    return collections_;
}

/// Collects if the heap has grown enough to be worth it. Called once per tick
/// and from `safepoint()` at every call boundary, so a long-running page's
/// garbage is bounded instead of accumulating for the life of the document,
/// or of one synchronous script. The threshold doubles with the survivors, so
/// the work is amortised O(1) per allocation and the heap peaks at about twice
/// the live set.
inline auto context::collect_if_due() -> std::size_t {
    if (live_objects_ < collect_threshold_ && string_bytes_ < string_budget_) { return 0; }
    const std::size_t freed = collect();
    // The next collection waits for the heap to grow again, so a page whose
    // live set is genuinely large does not collect on every tick.
    collect_threshold_ = std::max(minimum_collect_threshold, live_objects_ * 2);
    return freed;
}

[[nodiscard]] inline auto context::live_objects() const noexcept -> std::size_t {
    return live_objects_;
}

/// Whether this value is a promise that has not settled, the one case `await`
/// cannot answer by reading. The shape is the standard library's: its settled
/// slot is present and false.
[[nodiscard]] inline auto context::is_pending_promise(value v) -> bool {
    if (!v.is_object()) { return false; }
    value * settled = static_cast<object_object *>(v.as_heap())->find(promise_settled_key);
    return settled != nullptr && !truthy(*settled);
}

/// The job that resumes an await of a settled value: called as
/// (coroutine, value, rejected), it resumes the coroutine. One native per
/// context, made on first use.
[[nodiscard]] inline auto context::await_job() -> value {
    if (await_job_.is_undefined()) {
        await_job_ =
            value::object(allocate<native_object>("await", [](context & c, std::span<value> a) {
                if (a.size() >= 3) { c.resume(a[0], a[1], truthy(a[2])); }
                return value::undefined();
            }));
    }
    return await_job_;
}

/// Asks a pending promise to resume this coroutine when it settles. The record
/// goes on the promise's own reaction list, so the resumption is queued and
/// ordered exactly like a `then`, because that is what it is.
inline auto context::attach_resume(value promise, value coroutine) -> void {
    if (!promise.is_object()) { return; }
    value * handlers = static_cast<object_object *>(promise.as_heap())->find(promise_reactions_key);
    if (handlers == nullptr || !handlers->is_array()) { return; }
    auto * record = allocate<object_object>();
    record->set("co", coroutine);
    static_cast<array_object *>(handlers->as_heap())->items.push_back(value::object(record));
    mark_promise_handled(promise); // An await is a PerformPromiseThen (27.7.5.3).
}

} // namespace ctbrowser::script
