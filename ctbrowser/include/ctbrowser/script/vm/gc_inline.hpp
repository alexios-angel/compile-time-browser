#pragma once

#include "../vm.hpp"

namespace ctbrowser::script {

inline auto context::set_external_roots(std::function<void(const root_visitor &)> enumerate)
    -> void {
    external_roots_ = std::move(enumerate);
}

// COLLECT AT EVERY SAFEPOINT, FOR TESTS.
//
// Until 2026-09-12 the only thing that collected in an ordinary run was
// `collect_if_due`, once per tick, from the browser's frame loop - so a
// collection NEVER happened while script was running, and a rooting bug
// was unreachable. `safepoint()` now collects when the heap is due, but
// only at a call boundary. This TEST MODE collects the whole heap at every
// safepoint, which is enormously slow, and is the only way the rooting
// discipline the ABI demands can be exercised.
inline auto context::set_gc_stress(bool on) noexcept -> void {
    gc_stress_ = on;
}

[[nodiscard]] inline auto context::gc_stress() const noexcept -> bool {
    return gc_stress_;
}

// A point where the ABI says a collection may happen. Under stress it
// always does; otherwise only when the heap has grown past the threshold
// `collect_if_due` keeps - the tick's rule, applied inside a turn, so a
// synchronous script that never yields is bounded the way a page on a
// timer is. Before this it was stress-only, and seven WPT reflection
// files - thousands of subtests in ONE top-level script - grew until a
// 4 GB address-space cap killed the process. One compare on the path that
// calls it: `invoke`, every C++ entry into JavaScript - which is what
// `Function.prototype.apply` is, and what testharness.js runs every
// subtest through. An interpreted JS-to-JS call is deliberately NOT one:
// ctcompile/test/EscapeCycle.cpp pins that `churn(1000)` collects exactly
// once under stress and returns with its 4,000 dead nodes unswept.
inline auto context::safepoint() -> void {
    if (gc_stress_) [[unlikely]] {
        (void)collect();
        return;
    }
    (void)collect_if_due();
}

// HOW MANY COLLECTIONS HAVE RUN. A test that forces GC and asserts an
// answer proves nothing on its own - the answer is the same if no
// collection happened at all, which is exactly how a stress mode that
// silently does nothing looks.
[[nodiscard]] inline auto context::collections() const noexcept -> std::size_t {
    return collections_;
}

// Collect if the heap has grown enough to be worth it. Called once per
// tick, and from `safepoint()` at every call boundary, so a long-running
// page's garbage is bounded instead of accumulating for the life of the
// document - or of one synchronous script. The threshold doubles with the
// survivors, so the work is amortised O(1) per allocation and the heap
// peaks at about twice the live set.
inline auto context::collect_if_due() -> std::size_t {
    if (live_objects_ < collect_threshold_) { return 0; }
    const std::size_t freed = collect();
    // The next collection waits for the heap to grow again, so a page whose
    // live set is genuinely large does not collect on every tick.
    collect_threshold_ = std::max(minimum_collect_threshold, live_objects_ * 2);
    return freed;
}

[[nodiscard]] inline auto context::live_objects() const noexcept -> std::size_t {
    return live_objects_;
}

// Whether this value is a promise that has NOT settled - the one case
// `await` cannot answer by reading. The shape is the standard library's:
// `__settled` present and false.
[[nodiscard]] inline auto context::is_pending_promise(value v) -> bool {
    if (!v.is_object()) { return false; }
    value * settled = static_cast<object_object *>(v.as_heap())->find("__settled");
    return settled != nullptr && !truthy(*settled);
}

// THE JOB THAT RESUMES AN AWAIT OF A SETTLED VALUE: (coroutine, value,
// rejected) -> resume. One native per context, made on first use.
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

// Ask a pending promise to put this coroutine back when it settles. The
// record goes on the promise's own handler list, so a resumption is queued
// and ordered exactly like a `then` - because that is what it is.
inline auto context::attach_resume(value promise, value coroutine) -> void {
    if (!promise.is_object()) { return; }
    value * handlers = static_cast<object_object *>(promise.as_heap())->find("__handlers");
    if (handlers == nullptr || !handlers->is_array()) { return; }
    auto * record = allocate<object_object>();
    record->set("co", coroutine);
    static_cast<array_object *>(handlers->as_heap())->items.push_back(value::object(record));
    mark_promise_handled(promise); // an await is a PerformPromiseThen (27.7.5.3)
}

} // namespace ctbrowser::script
