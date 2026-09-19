#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ctbrowser::script {

std::optional<value> context::execute_await(instruction in, call_frame *& vm_frame,
                                            std::size_t & base, std::size_t stop_depth) {
    const auto reg = [&](std::uint16_t r) -> value & { return registers_[base + r]; };
    do {
        {
            // A settled promise carries its value in `__value`; anything else
            // awaits to itself. A REJECTED promise throws, which is what makes
            // `try { await f() } catch` work.
            const value awaited_raw = reg(in.b);
            // EVERY AWAIT SUSPENDS THE FRAME (27.7.5.3 Await: PerformPromiseThen
            // on a promise resolved with the value, so the continuation is
            // a job even when the value is already settled - `await 1`
            // runs the rest of the body after the microtasks queued before
            // it, which is what every ordering test and every
            // MutationObserver callback relies on). There is one stack and
            // the event loop is above it, so `await` cannot block: the frame
            // is lifted out, the caller is handed a promise, and the frame
            // comes back when the awaited one settles - from its handler
            // list when it is pending, from a job queued now when it is
            // not. A SCRIPT'S TOP LEVEL is the exception it always was:
            // `return await x` in a classic script (no closure, no caller
            // to hand a promise to) reads a settled value straight out -
            // and when the value is a PENDING promise it runs the queue
            // first, since the jobs that settle it are the ones an async
            // callee just queued. Draining re-enters the VM, so the
            // frame and its window are re-derived afterwards.
            const bool top_level = vm_frame->closure == nullptr;
            // 27.7.5.3 step 2, PromiseResolve(%Promise%, value): an object
            // that is not a promise is resolved INTO one - which is where a
            // thenable's `then` is called (NewPromiseResolveThenableJob), so
            // `await { then(_, reject) { reject(e) } }` throws e. A promise
            // is awaited as itself and a primitive keeps the fast path below.
            if (awaited_raw.is_object_like() && pending_promise_factory_ && promise_settler_ &&
                !(awaited_raw.is_object() &&
                  static_cast<object_object *>(awaited_raw.as_heap())->find("__settled") !=
                      nullptr)) {
                // The object stays rooted through the register until the
                // wrapper is in it; the wrapper is rooted by the register
                // from then on, and resolving allocates the thenable job.
                const rooted keep{*this, awaited_raw};
                reg(in.b) = pending_promise_factory_(*this);
                promise_settler_(*this, reg(in.b), awaited_raw, false);
                if (failed_) { break; }
                vm_frame = &frames_.back();
                base = vm_frame->base;
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
                // AN ASYNC GENERATOR'S FRAME IS ALREADY A COROUTINE - the one
                // its `.next()` resumes - so the await parks THAT object rather
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
                        if (value * state = obj->find("__rejected");
                            state != nullptr && truthy(*state)) {
                            rejected = true;
                        }
                        if (value * settled = obj->find("__value")) {
                            with = *settled;
                            // An await IS a PerformPromiseThen (27.7.5.3
                            // step 3): a rejection awaited is a handled one.
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
                if (obj->find("__settled") != nullptr) { mark_promise_handled(awaited); }
                if (value * state = obj->find("__rejected"); state != nullptr && truthy(*state)) {
                    thrown_ = obj->find("__value") != nullptr ? *obj->find("__value")
                                                              : value::undefined();
                    if (!unwind_to_handler()) { raise("uncaught rejection"); }
                    break;
                }
                if (value * settled = obj->find("__value")) { reg(in.a) = *settled; }
            }
            break;
        }
    } while (0);
    return std::nullopt;
}

} // namespace ctbrowser::script
