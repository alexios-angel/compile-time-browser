//===- coroutines.cpp - Closures and generators -----------------*- C++ -*-===//
//
// Closures, generators, async generators and the suspended frame that an
// `await` or a `yield` leaves behind. All functions here are members of
// `context`, declared in include/ctbrowser/script/vm.hpp.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

// --- generators -------------------------------------------------------------
//
// A generator is the same suspended frame `await` uses, with a different
// resumer: an explicit `.next()` instead of a settling promise. So there is
// no second suspension mechanism: `coroutine_object` already holds a proto,
// an ip, a register window and the frame's handlers, which is everything a
// paused function needs.
//
// Generators matter for real pages even where authors never write one:
// TypeScript compiles every `async` function into a generator driven by an
// `__awaiter` helper (Babylon.js has 622 such `function*` bodies). That
// helper needs exactly `.next(v)`, `.throw(e)` and a `{value, done}` record
// back.

/// Creates the closure for function \p function_index. Shared by the
/// interpreter's op::closure (run_loop.cpp) and by compiled (AOT) callers
/// through ct_aot_make_closure.
value context::make_closure(closure_object * enclosing, std::uint32_t function_index,
                            upvalue_source parent, value enclosing_this) {
    // The program is taken per frame, not per loop: a context can be running
    // functions from more than one program at a time, and a function index
    // means nothing outside the program it was compiled in.
    //
    // Guarded, because a compiled caller can run with no interpreted program
    // loaded (AOT-only mode), and a null `program_` dereference would be
    // undefined behaviour rather than a clean error.
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
    // The upvalue count must agree with the target, because reading the
    // caller's array past its end is worse than a fault: it would make a
    // closure that captured whatever was next in memory.
    if (parent.by_descriptor != nullptr && parent.descriptor_count != target.upvalues.size()) {
        raise("closure upvalue count disagrees with the function it names");
        return value::undefined();
    }

    auto * made = allocate<closure_object>(&target);
    made->owner = prog;
    // OrdinaryFunctionCreate step 4: the realm running now, which is the
    // enclosing function's - or, for a top level, the realm it was entered in.
    made->realm =
        enclosing != nullptr && enclosing->realm != nullptr ? enclosing->realm : &current_realm();
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
    // An arrow's `this` is decided here, where it is written, not where it is
    // called, which is what makes an arrow inside an arrow inside a method
    // still see the method's object. The caller passes the effective receiver.
    if (target.is_arrow) {
        made->captured_this = enclosing_this;
        if (enclosing != nullptr && enclosing->proto != nullptr && enclosing->proto->is_arrow) {
            made->captured_this = enclosing->captured_this;
        } else if (enclosing != nullptr && enclosing->derived_constructor && !frames_.empty() &&
                   frames_.back().closure == enclosing) {
            // Captures from one constructor invocation share its binding;
            // keeping it on the class closure would mix separate instances.
            value & cell = frames_.back().receiver_cell;
            if (cell.is_undefined()) {
                cell = value::object(allocate<cell_object>(enclosing_this));
            }
            made->captured_this = cell;
        }
        // Bare super() also needs the lexical new.target, even when no
        // explicit new.target expression made the compiler capture it.
        value new_target = frames_.empty() ? value::undefined() : frames_.back().new_target;
        if (enclosing != nullptr) {
            if (const value * held = enclosing->find(CTBROWSER_PRIVATE_KEY("NewTarget"))) {
                new_target = *held;
            }
        }
        if (!new_target.is_undefined()) {
            made->define(CTBROWSER_PRIVATE_KEY("NewTarget"), new_target, attr_none);
        }
        // The home object is lexical too: `super.m()` and `super()` inside an
        // arrow resolve against the method or constructor the arrow was
        // written in (an arrow has no [[HomeObject]] of its own, 15.3.4).
        // load_home reads the running closure's `__home`, so the enclosing
        // one's is copied when there is one.
        if (enclosing != nullptr) {
            if (const value * home = enclosing->find("__home")) { made->set("__home", *home); }
        }
    }
    return value::object(made);
}

/// Creates the generator object returned by calling generator function
/// \p closure. Runs the eager parameter prologue now, if the function has one,
/// but not the body.
value context::make_generator(closure_object * closure, value receiver,
                              std::span<const value> args) {
    auto * saved = allocate<coroutine_object>();
    saved->proto = closure->proto;
    saved->ip = 0;
    saved->argc = static_cast<std::uint16_t>(args.size());
    saved->closure = closure;
    saved->realm = realm_for(closure);
    saved->receiver = receiver;
    saved->generator = true;
    // The arguments are the frame's first registers, which is how an ordinary
    // call passes them: the callee's frame starts where its arguments already
    // are. A generator has no such frame yet, so the window is built here and
    // the body runs out of it on the first `.next()`.
    saved->window.assign(args.begin(), args.end());
    saved->window.resize(closure->proto->frame_size, value::undefined());

    value out = make_object();
    // Rooted from here: the eager prologue below runs page code (a default
    // parameter can call anything, and a call can collect), while the
    // generator is reachable only from this local and `saved` only through it.
    // Regression: test262 staging/sm/expressions/destructuring-array-default-*.
    const rooted keep{*this, out};
    auto * obj = static_cast<object_object *>(out.as_heap());
    saved->async_gen = closure->proto->is_async;
    saved->self = out;
    if (object_object * table =
            prototype(saved->async_gen ? proto_kind::async_generator : proto_kind::generator)) {
        obj->prototype = value::object(table);
    }
    obj->define(generator_slot_key, value::object(saved), attr_none);
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
    // 27.5.3.x / 27.6.3.x EvaluateBody: FunctionDeclarationInstantiation
    // (the prologue above) first, then OrdinaryCreateFromConstructor
    // (functionObject, "%GeneratorPrototype%"). That is the function's own
    // `prototype` as of now when it is an object (a default parameter may
    // have reassigned it; ensure_prototype makes one inheriting the intrinsic
    // otherwise), else the intrinsic already set above.
    if (!failed_) {
        const value own = ensure_prototype(value::object(closure));
        if (own.is_object_like()) { obj->prototype = own; }
    }
    return out;
}

context::coroutine_object * context::current_generator() const noexcept {
    return frames_.empty() ? nullptr : frames_.back().generator;
}

value context::make_return_marker(value v) {
    value marker = make_object();
    static_cast<object_object *>(marker.as_heap())->set(return_marker_key, v);
    return marker;
}

bool context::is_return_marker(value v) const {
    return v.is_object() &&
           static_cast<object_object *>(v.as_heap())->find(return_marker_key) != nullptr;
}

value context::return_marker_value(value marker) const {
    const value * held = static_cast<object_object *>(marker.as_heap())->find(return_marker_key);
    return held != nullptr ? *held : value::undefined();
}

/// Runs one `.next(v)`, `.throw(e)` or `.return(v)` request on \p generator
/// and returns its `{value, done}` record. For an async generator the answer
/// may be undefined, meaning the request is settled later by resume().
value context::generator_resume(value generator, value sent, resume_mode how) {
    if (!generator.is_object()) { return iter_result(value::undefined(), true); }
    value * held = static_cast<object_object *>(generator.as_heap())->find(generator_slot_key);
    if (held == nullptr || !held->is_kind(heap_kind::coroutine)) {
        return iter_result(value::undefined(), true);
    }
    auto * saved = static_cast<coroutine_object *>(held->as_heap());

    // A generator cannot resume itself. `.next()` from inside the body would
    // push a second frame over the same register window and both would write
    // each other's locals; the spec makes it a TypeError and so does this.
    if (saved->running) {
        throw_error("TypeError", "this generator is already running");
        return iter_result(value::undefined(), true);
    }
    // An async generator's `.return(v)` awaits v first (27.6.3.8
    // AsyncGeneratorUnwrapYieldResumption at a yield, 27.6.3.9
    // AsyncGeneratorAwaitReturn when there is no yield to resume), so the
    // request stays open until that settles, and resume() decides what the
    // awaited value does: a return completion through the body's finally
    // blocks, or the request's own done record.
    if (saved->async_gen && how == resume_mode::returned) {
        const bool at_yield = saved->started && !saved->done;
        if (!at_yield) { saved->done = true; }
        // 1: resume at the yield with a return completion once v settles;
        // 2: no yield to resume, so just settle the request with v.
        saved->return_pending = at_yield ? 1 : 2;
        saved->awaiting = true;
        await_for(saved, sent);
        return value::undefined();
    }
    // `.throw(e)` at an async `yield*`: the delegate loop forwards it to the
    // inner iterator's `throw` (14.4.14 step 7.b). The frame resumes with
    // the record and the loop's own native does the call.
    if (saved->async_gen && how == resume_mode::thrown && saved->started && !saved->done &&
        saved->delegate.is_object()) {
        sent = make_resume_record("throw", sent);
        how = resume_mode::next;
    }
    // A finished generator keeps answering done forever. `.next()` past the
    // end is not an error and must not run the body again.
    //
    // An async generator never throws at its caller: `.throw(e)` on one that
    // has finished, or has not started, answers a rejected promise, which
    // settle_async_generator hands to the request. The sync form unwinds.
    if (saved->async_gen && how == resume_mode::thrown && (saved->done || !saved->started)) {
        saved->done = true;
        return make_promise(sent, true);
    }
    if (saved->done) {
        if (how == resume_mode::thrown) {
            thrown_ = sent;
            if (!unwind_to_handler()) { raise("uncaught exception from a finished generator"); }
            return iter_result(value::undefined(), true);
        }
        return iter_result(how == resume_mode::returned ? sent : value::undefined(), true);
    }
    // `.throw()` / `.return()` before the body ever ran never enter it: there is
    // no `yield` to throw at, so the generator simply finishes.
    if (!saved->started && how != resume_mode::next) {
        saved->done = true;
        if (how == resume_mode::thrown) {
            thrown_ = sent;
            if (!unwind_to_handler()) { raise("uncaught exception from a generator"); }
        }
        return iter_result(how == resume_mode::returned ? sent : value::undefined(), true);
    }
    // A `yield*` in progress (sync only): `.throw(e)` and `.return(v)` go to
    // the inner iterator first (14.4.14 steps 7.b and 7.c), and only what
    // comes back decides whether the frame resumes, stays suspended, or
    // finishes. The record is the one yield_delegate_settle_name keeps on the
    // coroutine; it is cleared as soon as the delegation is over.
    if (!saved->async_gen && how != resume_mode::next && saved->delegate.is_object()) {
        auto * rec = static_cast<object_object *>(saved->delegate.as_heap());
        const auto slot = [&](const char * name) {
            const value * found = rec->find(name);
            return found != nullptr ? *found : value::undefined();
        };
        const value iterator = slot("iterator");
        value forwarded = value::undefined();
        bool threw = false;
        // Every `?` in ECMA-262 14.4.14 step 7 throws at the yield: a getter
        // on the inner iterator or its result that throws goes to the frame's
        // own handlers, not to whoever called `.throw()`/`.return()`. A throw
        // parked by the native that resumed us (context::call) is taken back
        // here. One that already unwound past us (the interpreter called
        // directly) belongs to the caller, and this resume is over. A parked
        // throw also bumps unwinds_ (it unwound to call's fence), so `landed`
        // counts only unwinds with nothing parked.
        std::size_t unwound = unwinds_;
        const auto landed = [&] { return failed_ || (unwinds_ != unwound && !has_pending_throw_); };
        const auto took = [&] {
            if (!has_pending_throw_) { return false; }
            forwarded = take_pending_throw();
            threw = true;
            unwound = unwinds_;
            return true;
        };
        const value method =
            lookup_property(iterator, how == resume_mode::thrown ? "throw" : "return");
        if (landed()) { return value::undefined(); }
        const bool no_method = !took() && method.is_nullish();
        if (threw) {
            // GetMethod threw - handled with the rest below.
        } else if (no_method && how == resume_mode::returned) {
            // No `return`: the return completion carries on through the
            // outer generator's own finally blocks (below) - step 7.c.iii,
            // with the method read once.
            saved->delegate = value::undefined();
            rec->set("done", value::boolean(true));
        } else if (no_method) {
            // No `throw`: close the inner iterator, then a TypeError at the
            // yield - the protocol was violated by the delegate (7.b.iii).
            const value close = lookup_property(iterator, "return");
            if (landed()) { return value::undefined(); }
            if (!took() && close.is_callable()) {
                value thrown_by = value::undefined();
                bool close_threw = false;
                (void)call_fenced(close, {}, iterator, close_threw, thrown_by);
                if (close_threw) {
                    threw = true;
                    forwarded = thrown_by;
                }
            }
            if (!threw) {
                threw = true;
                forwarded =
                    make_error("TypeError", "The iterator does not provide a 'throw' method");
            }
        } else if (!method.is_callable()) {
            threw = true;
            forwarded = make_error(
                "TypeError", "iterator." +
                                 std::string{how == resume_mode::thrown ? "throw" : "return"} +
                                 " is not a function");
        } else {
            // The thrown value needs its own variable: call_fenced writes the
            // exception through its out parameter and also returns undefined,
            // so `forwarded` cannot be both.
            value thrown_by = value::undefined();
            const value answer = call_fenced(method, {&sent, 1}, iterator, threw, thrown_by);
            forwarded = threw ? thrown_by : answer;
            if (!threw && !forwarded.is_object()) {
                threw = true;
                forwarded = make_error("TypeError", "Iterator result is not an object");
            }
        }
        // IteratorComplete, then IteratorValue when done - either getter
        // may throw, and that throw is the frame's too.
        bool finished = false;
        value final = value::undefined();
        if (!threw && !(no_method && how == resume_mode::returned)) {
            finished = truthy(lookup_property(forwarded, "done"));
            if (landed()) { return value::undefined(); }
            if (!took() && finished) {
                final = lookup_property(forwarded, "value");
                if (landed()) { return value::undefined(); }
                (void)took();
            }
        }
        if (!threw && no_method && how == resume_mode::returned) {
            // Fall through to the return completion.
        } else if (threw) {
            // Thrown at the yield inside the loop - the frame resumes with it.
            saved->delegate = value::undefined();
            rec->set("done", value::boolean(true));
            sent = forwarded;
            how = resume_mode::thrown;
        } else if (finished) {
            saved->delegate = value::undefined();
            rec->set("done", value::boolean(true));
            if (how == resume_mode::returned) {
                // The delegate returned: the outer generator returns its
                // value, through its own finally blocks (below).
                sent = final;
            } else {
                // The delegate finished normally: the yield* expression takes
                // its value and the body carries on from the loop's exit.
                rec->set("value", final);
                sent = value::undefined();
                how = resume_mode::next;
            }
        } else {
            // Still going: the inner result is the answer, and the frame stays
            // where it is.
            return forwarded;
        }
    }
    // `.return(v)` at a yield (27.5.3.4 steps 6-9): a sync generator resumes
    // with a return completion, which here is a marker thrown at the yield.
    // Every `finally` between the yield and the body's end runs, a catch
    // clause passes it on (catch_filter_name), a `yield` inside a finally
    // suspends again, and the marker escaping the frame is what finishes the
    // generator with v (or with whatever a finally returned instead). An
    // async generator takes the same road from resume(), once v is awaited.
    if (how == resume_mode::returned) {
        sent = make_return_marker(sent);
        how = resume_mode::thrown;
    }

    // The fence under the frame: a throw that leaves the body lands here
    // first, so a return marker can be told from a page's own exception and
    // the exception put back on its way (see after run_loop).
    // Not for an async generator: its frame may park on an `await` and come
    // back through resume(), which knows nothing of a fence left here.
    const bool fenced = !saved->async_gen;
    if (fenced) {
        handler fence;
        fence.frame = frames_.size();
        fence.reg_top = registers_.size();
        fence.fence = true;
        handlers_.push_back(fence);
    }
    const std::size_t fence_mark = handlers_.size();

    const std::size_t base = restore_frame(saved);

    // The value passed to `.next(v)` lands in the destination register of
    // the `yield` that suspended, which is what makes `var x = yield y` see it.
    // Not on the first resume: nothing is waiting for it there, and writing it
    // would clobber the frame's first local, which on a body with parameters is
    // an argument.
    if (saved->started) { registers_[base + saved->await_reg] = sent; }
    saved->started = true;

    const std::size_t stop = frames_.size() - 1;
    saved->running = true;
    yielded_ = false;

    // What an escaping throw means once the fence has caught it: a return
    // marker finishes the generator with its value; anything else is the
    // page's exception and continues unwinding from the caller.
    const auto escaped = [&](value thrown) {
        saved->running = false;
        saved->done = true;
        saved->delegate = value::undefined();
        registers_.resize(base);
        if (is_return_marker(thrown)) { return iter_result(return_marker_value(thrown), true); }
        thrown_ = thrown;
        if (!unwind_to_handler()) { raise("uncaught " + describe_thrown(thrown_)); }
        return iter_result(value::undefined(), true);
    };
    const auto pop_fence = [&] {
        if (fenced && handlers_.size() >= fence_mark) { handlers_.resize(fence_mark - 1); }
    };
    const auto fence_took = [&] {
        if (!fenced || !fence_hit_) { return false; }
        fence_hit_ = false;
        return true;
    };

    if (how == resume_mode::thrown) {
        // Throw at the yield, so `try { yield x } catch` works across a real
        // suspension. __awaiter's rejection path relies on this.
        thrown_ = sent;
        if (!unwind_to_handler()) {
            while (frames_.size() > stop) { frames_.pop_back(); }
            registers_.resize(base);
            saved->running = false;
            saved->done = true;
            return iter_result(value::undefined(), true);
        }
        if (fence_took()) {
            const value thrown = fence_thrown_;
            fence_thrown_ = value::undefined();
            return escaped(thrown);
        }
    }

    const value produced = run_loop(stop);
    saved->running = false;

    if (fence_took()) {
        const value thrown = fence_thrown_;
        fence_thrown_ = value::undefined();
        return escaped(thrown);
    }
    pop_fence();

    if (yielded_) {
        yielded_ = false;
        // A sync `yield*` yields the inner result object as it is (14.4.14
        // step 7.a.vii: GeneratorYield(innerResult)), not a fresh record.
        if (!saved->async_gen && saved->delegate.is_object() && produced.is_object()) {
            return produced;
        }
        return iter_result(produced, false);
    }
    saved->delegate = value::undefined();
    // An async generator parked on an `await`. op::await_value lifted the
    // frame back into this same coroutine and flagged it; the request is
    // finished by resume() when the awaited promise settles. Nothing to
    // answer yet.
    if (suspended_ && saved->awaiting) {
        suspended_ = false;
        return value::undefined();
    }
    // The body returned, threw past its own handlers, or the VM failed. Any of
    // those finish the generator; a further `.next()` answers done forever.
    saved->done = true;
    registers_.resize(base);
    return iter_result(produced, true);
}

// --- async generators ---------------------------------------------------------
//
// `async function*`, 27.6. The body is the generator above with `await`
// allowed in it, so a request (`.next(v)`, `.throw(e)`, `.return(v)`) cannot
// always be answered on the spot: the body may be parked on an await from the
// previous one. So every request is a promise, joins a queue, and runs when
// the body is free. The record a sync generator returns is what the promise
// resolves with; a body that throws rejects it instead. The compiler's async
// fence turns the throw into a rejected promise on the body's return path, and
// settle_async_generator reads it back out.

/// Queues one request on async generator \p generator and returns the promise
/// that settles with its `{value, done}` record.
value context::async_generator_request(value generator, value sent, resume_mode how) {
    if (!pending_promise_factory_ || !promise_settler_) { return value::undefined(); }
    const value promise = pending_promise_factory_(*this);
    value * held = generator.is_object()
                       ? static_cast<object_object *>(generator.as_heap())->find(generator_slot_key)
                       : nullptr;
    if (held == nullptr || !held->is_kind(heap_kind::coroutine) ||
        !static_cast<coroutine_object *>(held->as_heap())->async_gen) {
        // 27.6.3.3 step 3: not an async generator. The promise rejects; the
        // caller is never thrown at.
        promise_settler_(*this, promise, make_error("TypeError", "not an async generator"), true);
        return promise;
    }
    auto * saved = static_cast<coroutine_object *>(held->as_heap());
    saved->queue.push_back({how, sent, promise});
    async_generator_drain(saved);
    return promise;
}

/// Runs queued requests on \p saved until the queue is empty or the body
/// parks on an `await`.
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

/// Settles the current request's promise with \p outcome. \p raw_return means
/// \p outcome is the body's return value rather than a finished record.
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
        value * state = obj->find(promise_rejected_key);
        if (state == nullptr || !truthy(*state)) { return nullptr; }
        return obj->find(promise_result_key);
    };
    if (value * reason = rejection_of(outcome)) {
        // A return completion that ran the body's finally blocks and escaped
        // through the async fence: the request's record, not a rejection.
        if (is_return_marker(*reason)) {
            promise_settler_(*this, promise, iter_result(return_marker_value(*reason), true),
                             false);
            return;
        }
        promise_settler_(*this, promise, *reason, true);
        return;
    }
    value record = outcome;
    if (raw_return) { record = iter_result(outcome, true); }
    if (record.is_object()) {
        if (value * v = static_cast<object_object *>(record.as_heap())->find("value")) {
            if (value * reason = rejection_of(*v)) {
                // The return marker again, this time as the value of the
                // done record generator_resume made when the body finished
                // on a later request (a `yield` inside the finally the
                // return completion was crossing).
                if (is_return_marker(*reason)) {
                    promise_settler_(*this, promise,
                                     iter_result(return_marker_value(*reason), true), false);
                    return;
                }
                promise_settler_(*this, promise, *reason, true);
                return;
            }
        }
    }
    promise_settler_(*this, promise, record, false);
}

/// Moves the top frame (its register window and handlers) into \p saved and
/// pops it. \p await_reg is where the resumed value will be written.
void context::suspend_frame(coroutine_object * saved, std::uint16_t await_reg) {
    const call_frame & frame = frames_.back();
    const std::size_t base = frame.base;
    saved->proto = frame.proto;
    saved->ip = frame.ip;
    saved->await_reg = await_reg;
    saved->argc = frame.argc;
    saved->closure = frame.closure;
    saved->realm = frame.realm;
    saved->receiver = frame.receiver;
    saved->constructing = frame.constructing;
    saved->promise = frame.async_promise;
    saved->window.assign(registers_.begin() + static_cast<std::ptrdiff_t>(base), registers_.end());
    saved->handlers.clear();
    for (std::size_t i = frame.handler_base; i < handlers_.size(); ++i) {
        handler moved = handlers_[i];
        moved.reg_top -= base;
        saved->handlers.push_back(moved);
    }
    handlers_.resize(frame.handler_base);
    frames_.pop_back();
    // Do not cut below the caller's own window. An interpreted callee's frame
    // starts inside its caller's (op::call: base + a + 1), so truncating at
    // this frame's base would drop live caller registers (e.g. `f(1); g(2);`
    // with both async). Keep frame_size + 8, as the call opcode reserved; a
    // native between the two frames has no window here.
    std::size_t keep = base;
    if (!frames_.empty() && frames_.back().proto != nullptr) {
        const call_frame & caller = frames_.back();
        keep = std::max(keep, caller.base + caller.proto->frame_size + 8u);
    }
    if (registers_.size() > keep) { registers_.resize(keep); }
}

/// Pushes \p saved's register window and handlers back as a new top frame and
/// returns the frame's register base.
std::size_t context::restore_frame(coroutine_object * saved) {
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
    frame.realm = saved->realm != nullptr ? saved->realm : realm_for(saved->closure);
    frame.receiver = saved->receiver;
    frame.constructing = saved->constructing;
    frame.handler_base = handlers_.size();
    // A generator's frame keeps its coroutine, and an async generator's
    // settles the request's promise, so an await inside it parks on that
    // one rather than minting another; a sync generator's is undefined.
    frame.generator = saved->generator ? saved : nullptr;
    frame.async_promise = saved->promise;
    frames_.push_back(frame);
    const std::size_t index = frames_.size() - 1;
    for (handler restored : saved->handlers) {
        restored.frame = index;
        restored.reg_top += base; // relative while saved; absolute again here
        handlers_.push_back(restored);
    }
    saved->handlers.clear();
    return base;
}

value context::make_resume_record(std::string_view how, value v) {
    value record = make_object();
    auto * table = static_cast<object_object *>(record.as_heap());
    table->set(resume_record_key, string(std::string{how}));
    table->set(resume_record_value_key, v);
    return record;
}

/// Arranges for \p saved to resume when \p v settles (Await, 27.7.5.3): a
/// pending promise gets a resume handler, anything else queues the resume as a
/// microtask.
void context::await_for(coroutine_object * saved, value v) {
    if (!pending_promise_factory_ || !promise_settler_) { return; }
    const value coroutine = value::object(saved);
    const rooted keep{*this, v};
    value awaited = v;
    bool threw = false;
    value thrown = value::undefined();
    const auto wrap = [&] {
        awaited = pending_promise_factory_(*this);
        promise_settler_(*this, awaited, v, false);
    };
    if (v.is_object() &&
        static_cast<object_object *>(v.as_heap())->find(promise_settled_key) != nullptr) {
        // PromiseResolve(%Promise%, v), 27.2.4.7.1: a promise is awaited as
        // itself when its `constructor` is %Promise%, and resolved into a fresh
        // one otherwise. A throwing `constructor` getter is the await's
        // rejection, so it is read under a fence.
        handler fence;
        fence.frame = frames_.size();
        fence.reg_top = registers_.size();
        fence.fence = true;
        handlers_.push_back(fence);
        const std::size_t mark = handlers_.size();
        const value ctor = lookup_property(v, "constructor");
        if (fence_hit_) {
            fence_hit_ = false;
            threw = true;
            thrown = fence_thrown_;
            fence_thrown_ = value::undefined();
        } else {
            if (handlers_.size() >= mark) { handlers_.resize(mark - 1); }
            // %Promise% is the private slot the library left on its
            // prototype (builtins/async.cpp, intrinsic_key), not the global
            // a page may have replaced.
            object_object * table = prototype(proto_kind::promise);
            const value * intrinsic =
                table != nullptr ? table->find(std::string_view{CTBROWSER_PRIVATE_KEY("Promise")})
                                 : nullptr;
            if (intrinsic == nullptr || !ctor.strict_equals(*intrinsic)) { wrap(); }
        }
    } else if (v.is_object_like()) {
        wrap();
    }
    if (threw) {
        queue_microtask(await_job(), {coroutine, thrown, value::boolean(true)});
        return;
    }
    if (is_pending_promise(awaited)) {
        attach_resume(awaited, coroutine);
        return;
    }
    value with = awaited;
    bool rejected = false;
    if (awaited.is_object()) {
        auto * obj = static_cast<object_object *>(awaited.as_heap());
        if (value * state = obj->find(promise_rejected_key); state != nullptr && truthy(*state)) {
            rejected = true;
        }
        if (value * settled = obj->find(promise_result_key)) {
            with = *settled;
            mark_promise_handled(awaited);
        }
    }
    queue_microtask(await_job(), {coroutine, with, value::boolean(rejected)});
}

/// Puts a suspended frame back on the stack and runs it. This mirrors the
/// suspension in `op::await_value`: the saved window goes back on top of the
/// register stack, the awaited value \p with lands in the register the await
/// writes to, and the body continues after it. Called from a microtask,
/// because a resumption is a promise handler and must be ordered with every
/// other `then`.
void context::resume(value coroutine, value with, bool rejected) {
    if (!coroutine.is_kind(heap_kind::coroutine) || failed_) { return; }
    auto * saved = static_cast<coroutine_object *>(coroutine.as_heap());

    if (saved->generator && saved->return_pending != 0) {
        // The awaited `.return(v)` value has arrived. A finished generator's
        // request settles with it as its done record (or with the rejection).
        // One parked at a `yield*` hands it to the delegate loop as a record
        // for the inner iterator. One at a plain yield resumes with a return
        // completion (the marker thrown at the yield, as the sync form does)
        // or with the rejection thrown there.
        const std::uint8_t pending = saved->return_pending;
        saved->return_pending = 0;
        saved->awaiting = false;
        if (pending == 2) {
            const value promise = saved->promise;
            saved->promise = value::undefined();
            promise_settler_(*this, promise, rejected ? with : iter_result(with, true), rejected);
            async_generator_drain(saved);
            return;
        }
        if (saved->delegate.is_object()) {
            with = make_resume_record(rejected ? "throw" : "return", with);
            rejected = false;
        } else if (!rejected) {
            with = make_return_marker(with);
            rejected = true;
        }
    }

    const std::size_t base = restore_frame(saved);
    // An async generator comes back as a generator frame, so its next `yield`
    // finds the coroutine to park in.
    if (saved->generator) {
        saved->awaiting = false;
        saved->running = true;
    }

    registers_[base + saved->await_reg] = with;
    const std::size_t stop = frames_.size() - 1;
    suspended_ = false;

    // A rejected await throws at the await, so `try { await p } catch` works
    // across a real suspension and not only across a settled one.
    if (rejected) {
        thrown_ = with;
        if (!unwind_to_handler()) {
            // Nothing in this frame catches it: the function's own promise
            // rejects, which is what an async function does with an exception
            // it does not handle. The frame is already gone (unwind_to_handler
            // pops what it cannot satisfy), so there is nothing left to run.
            while (frames_.size() > stop) { frames_.pop_back(); }
            registers_.resize(base);
            thrown_ = value::undefined();
            if (saved->generator) {
                saved->running = false;
                saved->done = true;
                if (is_return_marker(with)) {
                    promise_settler_(*this, saved->promise,
                                     iter_result(return_marker_value(with), true), false);
                } else {
                    promise_settler_(*this, saved->promise, with, true);
                }
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
        // The async generator's request finishes here: the body yielded (the
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
            const value record = iter_result(returned, false);
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
        return; // It awaited again; its promise settles on a later resume.
    }
    if (failed_) { return; }
    // The body finished. What it returns is already a settled promise, because
    // an async function's last act is `wrap_promise`, so its outcome is
    // adopted rather than wrapped a second time.
    value outcome = returned;
    bool failed_outcome = false;
    if (returned.is_object()) {
        auto * obj = static_cast<object_object *>(returned.as_heap());
        // Only a settled promise is unwrapped here. A pending one also has
        // `__settled` (false); it goes to the settler below, whose resolve
        // adopts it, so `async () => { await x; return p; }` waits for `p`.
        if (const value * settled = obj->find(promise_settled_key);
            settled != nullptr && truthy(*settled)) {
            value * held = obj->find(promise_result_key);
            value * state = obj->find(promise_rejected_key);
            outcome = held == nullptr ? value::undefined() : *held;
            failed_outcome = state != nullptr && truthy(*state);
            // Mark it handled, as the `then` of a PromiseResolveThenableJob
            // would (PerformPromiseThen step 9); otherwise the fence's
            // `Promise.reject(e)`, consumed here, is reported as unhandled.
            mark_promise_handled(returned);
        }
    }
    promise_settler_(*this, saved->promise, outcome, failed_outcome);
}

} // namespace ctbrowser::script
