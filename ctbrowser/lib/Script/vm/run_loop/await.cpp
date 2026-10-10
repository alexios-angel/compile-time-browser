//===- ctbrowser/lib/Script/vm/run_loop/await.cpp - The await opcode ------===//
//
// The interpreter's `await` handler, context::execute_await, called from
// run_loop.cpp's VM_CASE(await). It suspends the current frame into a
// coroutine and resumes it when the awaited value settles.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ctbrowser::script {

/// Executes `await` (instruction `in`: register b holds the awaited value,
/// register a receives the result). `vm_frame` and `base` are the run loop's
/// cached frame and register base, and are re-derived here when the register
/// file may have moved. Returns the async function's promise when the
/// suspension leaves the frame stack at or below `stop_depth` (the run loop
/// must then return it); otherwise returns nullopt and the loop continues.
std::optional<value> context::execute_await(instruction in, call_frame *& vm_frame,
                                            std::size_t & base, std::size_t stop_depth) {
    const auto reg = [&](std::uint16_t r) -> value & { return registers_[base + r]; };
    do {
        {
            // A settled promise carries its value in its private PromiseResult
            // slot (promise_result_key); anything else awaits to itself. A
            // rejected promise throws, which is what makes
            // `try { await f() } catch` work.
            const value awaited_raw = reg(in.b);
            // Every await suspends the frame (27.7.5.3 Await: PerformPromiseThen
            // on a promise resolved with the value), so the continuation is a
            // job even when the value is already settled: `await 1` runs the
            // rest of the body after the microtasks queued before it, which
            // ordering tests and MutationObserver callbacks rely on. There is
            // one stack with the event loop above it, so `await` cannot block:
            // the frame is lifted out, the caller is handed a promise, and the
            // frame comes back when the awaited value settles (from its
            // handler list when pending, from a job queued now when not).
            //
            // A script's top level is the exception: `return await x` in a
            // classic script (no closure, no caller to hand a promise to)
            // reads a settled value straight out, and for a pending promise
            // drains the microtask queue first, since the jobs that settle it
            // are the ones an async callee just queued. Draining re-enters the
            // VM, so the frame and its register window are re-derived after.
            const bool top_level = vm_frame->closure == nullptr;
            // PromiseResolve(%Promise%, value) reads a promise's constructor
            // before deciding whether to adopt it. The installed factory owns
            // that algorithm, including thenable assimilation. Its getter
            // errors must reach this await before the frame suspends, and its
            // callbacks may grow the register stack or collect temporary values.
            if (awaited_raw.is_object_like() && promise_factory_) {
                const rooted keep{*this, awaited_raw};
                const value resolved = [&] {
                    const native_scope pinned{*this};
                    return make_promise(awaited_raw, false);
                }();
                if (rethrow_pending() || failed_) { break; }
                vm_frame = &frames_.back();
                base = vm_frame->base;
                reg(in.b) = resolved;
            }
            const value awaited = reg(in.b);
            if (top_level && is_pending_promise(awaited)) {
                drain_microtasks();
                if (failed_) { break; }
                vm_frame = &frames_.back();
                base = vm_frame->base;
            }
            const bool suspends = is_pending_promise(awaited) || !top_level;
            if (suspends && pending_promise_factory_ && promise_settler_) {
                if (vm_frame->async_promise.is_undefined()) {
                    vm_frame->async_promise = pending_promise_factory_(*this);
                }
                const value promise = vm_frame->async_promise;
                // An async generator's frame is already a coroutine (the one
                // its `.next()` resumes), so the await parks that object rather
                // than making a second one the generator would never see.
                // `awaiting` keeps the request queue from resuming it until
                // the awaited promise does.
                coroutine_object * saved = vm_frame->generator;
                if (saved != nullptr) {
                    saved->awaiting = true;
                    saved->running = false;
                } else {
                    saved = allocate<coroutine_object>();
                }
                const std::uint16_t slot = vm_frame->result_reg;
                suspend_frame(saved, in.a);
                if (is_pending_promise(awaited)) {
                    attach_resume(awaited, value::object(saved));
                } else {
                    // Settled, or not a promise at all: resume in a job
                    // with the value (or throw the rejection there).
                    value with = awaited;
                    bool rejected = false;
                    if (awaited.is_object()) {
                        auto * obj = static_cast<object_object *>(awaited.as_heap());
                        if (value * state = obj->find(promise_rejected_key);
                            state != nullptr && truthy(*state)) {
                            rejected = true;
                        }
                        if (value * settled = obj->find(promise_result_key)) {
                            with = *settled;
                            // An await is a PerformPromiseThen (27.7.5.3
                            // step 3): an awaited rejection is a handled one.
                            mark_promise_handled(awaited);
                        }
                    }
                    queue_microtask(await_job(),
                                    {value::object(saved), with, value::boolean(rejected)});
                }
                suspended_ = true;
                if (frames_.size() <= stop_depth) { return promise; }
                registers_[frames_.back().base + slot] = promise;
                break;
            }
            reg(in.a) = awaited;
            if (awaited.is_object()) {
                auto * obj = static_cast<object_object *>(awaited.as_heap());
                if (obj->find(promise_settled_key) != nullptr) { mark_promise_handled(awaited); }
                if (value * state = obj->find(promise_rejected_key);
                    state != nullptr && truthy(*state)) {
                    thrown_ = obj->find(promise_result_key) != nullptr
                                  ? *obj->find(promise_result_key)
                                  : value::undefined();
                    if (!unwind_to_handler()) { raise("uncaught rejection"); }
                    break;
                }
                if (value * settled = obj->find(promise_result_key)) { reg(in.a) = *settled; }
            }
            break;
        }
    } while (0);
    return std::nullopt;
}

} // namespace ctbrowser::script
