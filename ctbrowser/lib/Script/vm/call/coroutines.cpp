// ctbrowser.script context - closures, generators and the suspended frame an
// `await` or a `yield` leaves behind.
//
// One of five files carved out of a 1,171-line vm/call.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/vm.hpp>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

// PUT A SUSPENDED FRAME BACK AND RUN IT.
//
// The mirror of the suspension in `op::await_value`: the saved window goes on
// top of the register stack, the frame is rebuilt around it, the awaited value
// lands in the register the await was writing to, and the body carries on from
// the instruction after it.
//
// Called from a microtask, because a resumption IS a promise handler - it was
// registered on the awaited promise's own handler list, so it queues and orders
// with every other `then`.
// --- generators -------------------------------------------------------------
//
// A generator is the SAME suspended frame `await` uses, with a different
// resumer: an explicit `.next()` instead of a settling promise. That is why
// there is no second suspension mechanism here - `coroutine_object` already
// carried a proto, an ip, a register window and this frame's handlers, which
// is the whole of what a paused function is.
//
// WHAT THIS IS FOR. Babylon.js has 622 `function*` bodies and not one of them
// is an author writing a generator: TypeScript compiles every `async` function
// into a generator driven by an `__awaiter` helper, so `yield` there is what
// `await` became. That helper needs exactly `.next(v)`, `.throw(e)` and a
// `{value, done}` record back, which is what this implements.

// op::closure's BODY, VERBATIM, plus the three guards a compiled caller needs.
value context::make_closure(closure_object * enclosing, std::uint32_t function_index,
                            upvalue_source parent, value enclosing_this) {
    // PER FRAME, not per loop: a context can be running functions from more
    // than one program at a time, and a function index means nothing outside
    // the program it was compiled in.
    //
    // GUARDED, WHICH THE INLINE VERSION IS NOT. run_loop dereferences
    // *program_ unguarded because an interpreted frame cannot exist without a
    // program; Phase 19's AOT-only mode is exactly the configuration where one
    // can, and a null deref is undefined behaviour rather than a fault.
    const program * prog =
        enclosing != nullptr && enclosing->owner != nullptr ? enclosing->owner : program_;
    if (prog == nullptr) {
        raise("no program to take a function from");
        return value::undefined();
    }
    if (function_index >= prog->functions.size()) {
        raise("closure function index out of range");
        return value::undefined();
    }
    const function_proto & target = prog->functions[function_index];
    // AND THE COUNT MUST AGREE WITH THE TARGET, because reading the caller's
    // array past its end is worse than a fault: it is a closure that captured
    // whatever was next in memory.
    if (parent.by_descriptor != nullptr && parent.descriptor_count != target.upvalues.size()) {
        raise("closure upvalue count disagrees with the function it names");
        return value::undefined();
    }

    auto * made = allocate<closure_object>(&target);
    made->owner = prog;
    // Walk the descriptors the compiler resolved: each upvalue is either a cell
    // sitting in the enclosing frame's register, or one the enclosing closure
    // already holds. The second case is what carries a capture down through
    // more than one level of nesting.
    made->upvalues.reserve(target.upvalues.size());
    for (std::size_t which = 0; which < target.upvalues.size(); ++which) {
        const upvalue_desc & up = target.upvalues[which];
        if (up.from_parent_local) {
            made->upvalues.push_back(parent.at(which, up));
        } else if (enclosing != nullptr && up.index < enclosing->upvalues.size()) {
            made->upvalues.push_back(enclosing->upvalues[up.index]);
        } else {
            made->upvalues.push_back(value::undefined());
        }
    }
    // An arrow's `this` is decided HERE, where it is written, not where it is
    // called - which is what makes an arrow inside an arrow inside a method
    // still see the method's object. The caller reads the EFFECTIVE receiver.
    if (target.is_arrow) { made->captured_this = enclosing_this; }
    return value::object(made);
}

value context::make_generator(closure_object * closure, value receiver,
                              std::span<const value> args) {
    auto * saved = allocate<coroutine_object>();
    saved->proto = closure->proto;
    saved->ip = 0;
    saved->argc = static_cast<std::uint16_t>(args.size());
    saved->closure = closure;
    saved->receiver = receiver;
    saved->generator = true;
    // THE ARGUMENTS ARE THE FRAME'S FIRST REGISTERS, which is how an ordinary
    // call passes them - the callee's frame starts where its arguments already
    // are. A generator has no such frame yet, so the window is built here and
    // the body runs out of it on the first `.next()`.
    saved->window.assign(args.begin(), args.end());
    saved->window.resize(closure->proto->frame_size, value::undefined());

    value out = make_object();
    auto * obj = static_cast<object_object *>(out.as_heap());
    saved->async_gen = closure->proto->is_async;
    saved->self = out;
    if (object_object * table =
            prototype(saved->async_gen ? proto_kind::async_generator : proto_kind::generator)) {
        obj->prototype = value::object(table);
    }
    obj->set("__co", value::object(saved));
    // See function_proto::eager_prologue: the parameters run now, up to the
    // compiler's own yield. `started` is put back so `.throw()`/`.return()`
    // on a generator that has not been resumed still never enter the body
    // (27.5.3.3 step 6-8: suspendedStart), and the first `.next(v)` keeps
    // discarding v. A prologue that threw has already unwound to the caller's
    // handler and left the generator done.
    if (closure->proto->eager_prologue) {
        (void)generator_resume(out, value::undefined(), resume_mode::next);
        saved->started = false;
    }
    return out;
}

value context::generator_resume(value generator, value sent, resume_mode how) {
    const auto record = [&](value v, bool done) {
        value out = make_object();
        auto * obj = static_cast<object_object *>(out.as_heap());
        obj->set("value", v);
        obj->set("done", value::boolean(done));
        return out;
    };
    if (!generator.is_object()) { return record(value::undefined(), true); }
    value * held = static_cast<object_object *>(generator.as_heap())->find("__co");
    if (held == nullptr || !held->is_kind(heap_kind::coroutine)) {
        return record(value::undefined(), true);
    }
    auto * saved = static_cast<coroutine_object *>(held->as_heap());

    // A GENERATOR CANNOT RESUME ITSELF. `.next()` from inside the body would
    // push a second frame over the same register window and both would write
    // each other's locals; the spec makes it a TypeError and so does this.
    if (saved->running) {
        throw_error("TypeError", "this generator is already running");
        return record(value::undefined(), true);
    }
    // A FINISHED GENERATOR KEEPS ANSWERING, for ever. `.next()` past the end is
    // not an error and must not run the body again.
    //
    // AN ASYNC GENERATOR NEVER THROWS AT ITS CALLER: `.throw(e)` on one that
    // has finished, or has not started, answers a REJECTED promise, which
    // settle_async_generator hands to the request. The sync form unwinds.
    if (saved->async_gen && how == resume_mode::thrown && (saved->done || !saved->started)) {
        saved->done = true;
        return make_promise(sent, true);
    }
    if (saved->done) {
        if (how == resume_mode::thrown) {
            thrown_ = sent;
            if (!unwind_to_handler()) { raise("uncaught exception from a finished generator"); }
            return record(value::undefined(), true);
        }
        return record(how == resume_mode::returned ? sent : value::undefined(), true);
    }
    // `.throw()` / `.return()` BEFORE THE BODY EVER RAN never enter it: there is
    // no `yield` to throw at, so the generator simply finishes.
    if (!saved->started && how != resume_mode::next) {
        saved->done = true;
        if (how == resume_mode::thrown) {
            thrown_ = sent;
            if (!unwind_to_handler()) { raise("uncaught exception from a generator"); }
        }
        return record(how == resume_mode::returned ? sent : value::undefined(), true);
    }
    // `.return(v)` at a yield finishes the generator without running any more of
    // it. Running the rest would be wrong - `return` means stop - and the
    // `finally` blocks the spec would run on the way out need the unwinder,
    // which is a bigger change than this corpus asks for. Recorded rather than
    // silent: docs/script.md says so by name.
    if (how == resume_mode::returned) {
        saved->done = true;
        return record(sent, true);
    }

    const std::size_t base = registers_.size();
    registers_.insert(registers_.end(), saved->window.begin(), saved->window.end());
    // Slack above the window, for the same reason a call reserves it: an
    // expression allocates scratch registers past the frame's declared size.
    registers_.resize(registers_.size() + 8u, value::undefined());

    call_frame frame;
    frame.proto = saved->proto;
    frame.ip = saved->ip;
    frame.base = base;
    frame.result_reg = 0;
    frame.argc = saved->argc;
    frame.closure = saved->closure;
    frame.receiver = saved->receiver;
    frame.handler_base = handlers_.size();
    frame.generator = saved;
    // An async generator's frame settles the REQUEST'S promise, so an await
    // inside it parks on that one rather than minting another.
    if (saved->async_gen) { frame.async_promise = saved->promise; }
    frames_.push_back(frame);
    const std::size_t index = frames_.size() - 1;
    for (handler restored : saved->handlers) {
        restored.frame = index;
        restored.reg_top += base; // relative while saved; absolute again here
        handlers_.push_back(restored);
    }
    saved->handlers.clear();

    // WHERE THE VALUE PASSED TO `.next(v)` LANDS: the destination register of
    // the `yield` that suspended, which is what makes `var x = yield y` see it.
    // NOT on the first resume - nothing is waiting for it there, and writing it
    // would clobber the frame's first local, which on a body with parameters is
    // an argument.
    if (saved->started) { registers_[base + saved->await_reg] = sent; }
    saved->started = true;

    const std::size_t stop = frames_.size() - 1;
    saved->running = true;
    yielded_ = false;

    if (how == resume_mode::thrown) {
        // THROW AT THE YIELD, so `try { yield x } catch` works across a real
        // suspension. __awaiter's rejection path is exactly this.
        thrown_ = sent;
        if (!unwind_to_handler()) {
            while (frames_.size() > stop) { frames_.pop_back(); }
            registers_.resize(base);
            saved->running = false;
            saved->done = true;
            return record(value::undefined(), true);
        }
    }

    const value produced = run_loop(stop);
    saved->running = false;

    if (yielded_) {
        yielded_ = false;
        return record(produced, false);
    }
    // AN ASYNC GENERATOR PARKED ON AN `await`. op::await_value lifted the
    // frame back into this same coroutine and flagged it; the request is
    // finished by resume() when the awaited promise settles. Nothing to
    // answer yet.
    if (suspended_ && saved->awaiting) {
        suspended_ = false;
        return value::undefined();
    }
    // The body returned, threw past its own handlers, or the VM failed. Any of
    // those finish the generator; a further `.next()` answers done for ever.
    saved->done = true;
    registers_.resize(base);
    return record(produced, true);
}

// --- async generators ---------------------------------------------------------
//
// `async function*`, 27.6. The body is the generator above with `await`
// allowed in it, so a request - `.next(v)`, `.throw(e)`, `.return(v)` - cannot
// always be answered on the spot: the body may be parked on an await from the
// previous one. So every request is a promise, joins a queue, and runs when
// the body is free. The record a sync generator returns is what the promise
// resolves with; a body that throws rejects it instead - the compiler's async
// fence turns the throw into a rejected promise on the body's return path, and
// settle_async_generator reads it back out.

value context::async_generator_request(value generator, value sent, resume_mode how) {
    if (!pending_promise_factory_ || !promise_settler_) { return value::undefined(); }
    const value promise = pending_promise_factory_(*this);
    value * held = generator.is_object()
                       ? static_cast<object_object *>(generator.as_heap())->find("__co")
                       : nullptr;
    if (held == nullptr || !held->is_kind(heap_kind::coroutine) ||
        !static_cast<coroutine_object *>(held->as_heap())->async_gen) {
        // 27.6.3.3 step 3: not an async generator - the promise rejects, the
        // caller is never thrown at.
        promise_settler_(*this, promise, make_error("TypeError", "not an async generator"), true);
        return promise;
    }
    auto * saved = static_cast<coroutine_object *>(held->as_heap());
    saved->queue.push_back({how, sent, promise});
    async_generator_drain(saved);
    return promise;
}

void context::async_generator_drain(coroutine_object * saved) {
    while (!saved->queue.empty() && !saved->running && !saved->awaiting && !failed_) {
        const coroutine_object::async_request request = saved->queue.front();
        saved->queue.erase(saved->queue.begin());
        // Popped, so the request's values are in a C++ local and nowhere else
        // until the body has them; `promise` is what this run settles.
        const rooted keep_sent{*this, request.sent};
        const rooted keep_promise{*this, request.promise};
        saved->promise = request.promise;
        const value answer = generator_resume(saved->self, request.sent, request.how);
        if (saved->awaiting) { return; } // resume() settles this one
        settle_async_generator(saved, answer, /*raw_return*/ false);
    }
}

void context::settle_async_generator(coroutine_object * saved, value outcome, bool raw_return) {
    if (!promise_settler_) { return; }
    const value promise = saved->promise;
    saved->promise = value::undefined();
    // A settled, rejected promise is the body's throw: the async fence caught
    // it and returned `Promise.reject(e)`, which generator_resume reports as
    // the record's value (or, from `.throw()` on a finished generator, as the
    // whole answer).
    const auto rejection_of = [](value v) -> value * {
        if (!v.is_object()) { return nullptr; }
        auto * obj = static_cast<object_object *>(v.as_heap());
        value * state = obj->find("__rejected");
        if (state == nullptr || !truthy(*state)) { return nullptr; }
        return obj->find("__value");
    };
    if (value * reason = rejection_of(outcome)) {
        promise_settler_(*this, promise, *reason, true);
        return;
    }
    value record = outcome;
    if (raw_return) {
        record = make_object();
        auto * obj = static_cast<object_object *>(record.as_heap());
        obj->set("value", outcome);
        obj->set("done", value::boolean(true));
    }
    if (record.is_object()) {
        if (value * v = static_cast<object_object *>(record.as_heap())->find("value")) {
            if (value * reason = rejection_of(*v)) {
                promise_settler_(*this, promise, *reason, true);
                return;
            }
        }
    }
    promise_settler_(*this, promise, record, false);
}

void context::resume(value coroutine, value with, bool rejected) {
    if (!coroutine.is_kind(heap_kind::coroutine) || failed_) { return; }
    auto * saved = static_cast<coroutine_object *>(coroutine.as_heap());

    const std::size_t base = registers_.size();
    registers_.insert(registers_.end(), saved->window.begin(), saved->window.end());
    // Slack above the window, for the same reason a call reserves it: an
    // expression allocates scratch registers past the frame's declared size.
    registers_.resize(registers_.size() + 8u, value::undefined());

    call_frame frame;
    frame.proto = saved->proto;
    frame.ip = saved->ip;
    frame.base = base;
    frame.result_reg = 0;
    frame.argc = saved->argc;
    frame.closure = saved->closure;
    frame.receiver = saved->receiver;
    frame.constructing = saved->constructing;
    frame.handler_base = handlers_.size();
    frame.async_promise = saved->promise;
    // An async generator comes back as a GENERATOR frame, so its next `yield`
    // finds the coroutine to park in.
    if (saved->generator) {
        frame.generator = saved;
        saved->awaiting = false;
        saved->running = true;
    }
    frames_.push_back(frame);
    const std::size_t index = frames_.size() - 1;
    for (handler restored : saved->handlers) {
        restored.frame = index;
        restored.reg_top += base; // relative while saved; absolute again here
        handlers_.push_back(restored);
    }

    registers_[base + saved->await_reg] = with;
    const std::size_t stop = frames_.size() - 1;
    suspended_ = false;

    // A REJECTED await THROWS AT THE AWAIT, so `try { await p } catch` works
    // across a real suspension and not only across a settled one.
    if (rejected) {
        thrown_ = with;
        if (!unwind_to_handler()) {
            // Nothing in this frame catches it: the function's own promise
            // rejects, which is what an async function does with an exception
            // it does not handle. The frame is already gone - unwind_to_handler
            // pops what it cannot satisfy - so there is nothing left to run.
            while (frames_.size() > stop) { frames_.pop_back(); }
            registers_.resize(base);
            thrown_ = value::undefined();
            if (saved->generator) {
                saved->running = false;
                saved->done = true;
                promise_settler_(*this, saved->promise, with, true);
                saved->promise = value::undefined();
                async_generator_drain(saved);
                return;
            }
            promise_settler_(*this, saved->promise, with, true);
            drain_microtasks();
            return;
        }
    }

    const value returned = run_loop(stop);
    if (saved->generator) {
        // THE ASYNC GENERATOR'S REQUEST, finished here: the body yielded (the
        // record), returned or threw (done), or parked on another await
        // (nothing yet). Then the queue moves on.
        saved->running = false;
        if (suspended_) {
            suspended_ = false;
            return;
        }
        if (failed_) { return; }
        if (yielded_) {
            yielded_ = false;
            value record = make_object();
            auto * obj = static_cast<object_object *>(record.as_heap());
            obj->set("value", returned);
            obj->set("done", value::boolean(false));
            settle_async_generator(saved, record, /*raw_return*/ false);
        } else {
            saved->done = true;
            settle_async_generator(saved, returned, /*raw_return*/ true);
        }
        async_generator_drain(saved);
        return;
    }
    if (suspended_) {
        suspended_ = false;
        return; // it awaited again; its promise settles on some later resume
    }
    if (failed_) { return; }
    // The body finished. What it returns is already a settled promise, because
    // an async function's last act is `wrap_promise` - so its outcome is
    // ADOPTED rather than wrapped a second time.
    value outcome = returned;
    bool failed_outcome = false;
    if (returned.is_object()) {
        auto * obj = static_cast<object_object *>(returned.as_heap());
        if (obj->find("__settled") != nullptr) {
            value * held = obj->find("__value");
            value * state = obj->find("__rejected");
            outcome = held == nullptr ? value::undefined() : *held;
            failed_outcome = state != nullptr && truthy(*state);
        }
    }
    promise_settler_(*this, saved->promise, outcome, failed_outcome);
}

} // namespace ctbrowser::script
