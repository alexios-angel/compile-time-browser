#include "promise_reactions.hpp"

namespace ctbrowser::script::detail {

namespace promise {

// --- promises ---------------------------------------------------------------
//
// A promise is an ordinary object carrying four own, non-enumerable slots:
// `__settled`, `__value`, `__rejected` and `__handlers`. The VM READS THEM BY
// NAME - context::is_pending_promise, wrap_in_promise, attach_resume and the
// await opcode - so the shape is fixed here and only the algorithms are this
// file's to choose. `__handlers` is the list of PromiseReaction records
// (27.2.1.1), each an object {ok, err, next, resolve, reject} - or {co}, the
// VM's own record for a suspended `await`, which `deliver` resumes instead of
// calling anything.
//
// A handler never runs the moment a promise settles: `settle` QUEUES a
// reaction job per record (context::queue_microtask) and the event loop
// drains it at the end of the turn, so `p.then(f); after();` runs `after`
// first, the way a real job queue orders them.

// PRIVATE keys on Promise.prototype (value.hpp: no source text can spell one
// and OwnPropertyKeys never reports one): the %Promise% intrinsic itself, the
// two job natives, and the one native that reads `then` under a fence. They
// live on the prototype table so the collector traces them like any property.

[[nodiscard]] object_object * promise_prototype(context & cx);

[[nodiscard]] value slot(object_object * o, std::string_view name) {
    const value * held = o->find(name);
    return held == nullptr ? value::undefined() : *held;
}

// IsPromise, 27.2.1.6: an object with the [[PromiseState]] slot.
[[nodiscard]] bool is_promise(value v) {
    return v.is_object() &&
           static_cast<object_object *>(v.as_heap())->find(settled_slot) != nullptr;
}

// The four slots, PENDING, on a fresh object - or on the instance `new` handed
// a subclass (`class P extends Promise` reaches the constructor through
// `super()`, with `this` already made on P.prototype).
[[nodiscard]] value init_promise(context & cx, value self) {
    if (!self.is_object()) { self = cx.make_object(); }
    auto * p = static_cast<object_object *>(self.as_heap());
    if (!p->prototype.is_object_like()) { p->prototype = value::object(promise_prototype(cx)); }
    p->define(value_slot, value::undefined(), attr_builtin);
    p->define(rejected_slot, value::boolean(false), attr_builtin);
    p->define(settled_slot, value::boolean(false), attr_builtin);
    p->define(handlers_slot, cx.make_array(), attr_builtin);
    return self;
}
[[nodiscard]] value pending_promise(context & cx) {
    return init_promise(cx, value::undefined());
}

// A PromiseCapability record (27.2.1.1). For %Promise% itself `resolve` and
// `reject` are left undefined and the promise is resolved directly - the
// functions exist only when something hands them to JavaScript, see
// `materialise`.

void settle(context & cx, value promise, value with, bool rejected);
void resolve_promise(context & cx, value promise, value resolution);
[[nodiscard]] value reaction_job(context & cx);
[[nodiscard]] value thenable_job(context & cx);

// CreateResolvingFunctions, 27.2.1.3. The pair shares ONE [[AlreadyResolved]]
// record, and a second pair for the same promise (NewPromiseResolveThenableJob
// makes one) has its own - which is why the flag is in a shared cell rather
// than on the promise.
//
// RETAINED, not merely captured: a `value` captured by a C++ lambda is
// invisible to the collector, and these two hold the only reference to the
// promise that outlives the call - a page keeps `resolve`, not the promise -
// so it was collected out from under them and a later resolve() settled freed
// memory, SILENTLY. `native_object::retained` is a traced list that is not a
// property, so it costs no name and is not visible to
// Object.getOwnPropertyNames(resolve).
[[nodiscard]] std::pair<value, value> resolvers_for(context & cx, value promise) {
    object_object * shared = new_table(cx);
    shared->set("promise", promise);
    shared->set("done", value::boolean(false));
    const value state = value::object(shared);
    const auto make = [&](bool rejecting) {
        auto * fn =
            cx.allocate<native_object>("", [state, rejecting](context & c, std::span<value> a) {
                auto * s = static_cast<object_object *>(state.as_heap());
                if (context::truthy(slot(s, "done"))) { return value::undefined(); }
                s->set("done", value::boolean(true));
                const value promise = slot(s, "promise");
                if (rejecting) {
                    settle(c, promise, arg_at(a, 0), true);
                } else {
                    resolve_promise(c, promise, arg_at(a, 0));
                }
                return value::undefined();
            });
        fn->is_constructor = false;
        install_arity(cx, fn, 1); // 27.2.1.3.1-2: length 1, name ""
        fn->retained.push_back(state);
        return value::object(fn);
    };
    const value resolve = make(false);
    const value reject = make(true);
    return {resolve, reject};
}

// The capability's functions, made on first need: a native capability is
// resolved directly until something has to HAND `resolve`/`reject` to
// JavaScript (`then` on a combinator's element promise, `withResolvers`).
void materialise(context & cx, capability & cap) {
    if (cap.resolve.is_callable() && cap.reject.is_callable()) { return; }
    const auto [resolve, reject] = resolvers_for(cx, cap.promise);
    cap.resolve = resolve;
    cap.reject = reject;
}

// Resolve or reject the capability with a value: through its functions when
// it has them, directly otherwise. A throw out of a subclass's `resolve` is
// left parked for the caller's fence.
void settle_capability(context & cx, const capability & cap, value with, bool rejected) {
    const value fn = rejected ? cap.reject : cap.resolve;
    if (fn.is_callable()) {
        const value args[1] = {with};
        (void)cx.call(fn, args);
        return;
    }
    if (rejected) {
        settle(cx, cap.promise, with, true);
    } else {
        resolve_promise(cx, cap.promise, with);
    }
}

// TriggerPromiseReactions' job, 27.2.2.1 NewPromiseReactionJob: run the
// handler for how the promise settled and pass its outcome to the capability.
void deliver(context & cx, value handler_record, value argument, bool rejected) {
    auto * record = static_cast<object_object *>(handler_record.as_heap());
    // A RESUMPTION IS A PROMISE HANDLER. `await` registers the suspended frame
    // on the awaited promise's own handler list, so it queues and orders with
    // every `then` rather than being a second mechanism that races them.
    if (value * waiting = record->find("co"); waiting != nullptr) {
        cx.resume(*waiting, argument, rejected);
        return;
    }
    const value handler = slot(record, rejected ? "err" : "ok");
    bool threw = rejected;
    value result = argument;
    if (handler.is_callable()) {
        // A HANDLER THAT THROWS REJECTS THE NEXT PROMISE (step 1.e): the call
        // is fenced so the throw stops here instead of unwinding to whatever
        // page `try` happens to be below the microtask - or to nothing, which
        // was an engine fault.
        const value args[1] = {argument};
        value thrown = value::undefined();
        result = cx.call_fenced(handler, args, value::undefined(), threw, thrown);
        if (threw) { result = thrown; }
    }
    capability cap;
    cap.promise = slot(record, "next");
    cap.resolve = slot(record, "resolve");
    cap.reject = slot(record, "reject");
    if (!cap.promise.is_undefined() || cap.resolve.is_callable()) {
        settle_capability(cx, cap, result, threw);
    }
}

// THE TWO JOBS. A job is a callable plus values so the collector traces it,
// so the C++ work has to be reachable through a value - which is what these
// natives are. One each per context, made on demand and kept on the prototype
// table under a private key.
[[nodiscard]] value job_native(context & cx, std::string_view key, native_fn fn) {
    object_object * table = promise_prototype(cx);
    if (const value * existing = table->find(key); existing != nullptr) { return *existing; }
    const value made = value::object(cx.allocate<native_object>(std::string{key}, std::move(fn)));
    table->define(key, made, attr_none);
    return made;
}
[[nodiscard]] value reaction_job(context & cx) {
    return job_native(cx, reaction_job_key, [](context & c, std::span<value> a) {
        if (a.size() >= 3 && a[0].is_object()) { deliver(c, a[0], a[1], context::truthy(a[2])); }
        return value::undefined();
    });
}
// 27.2.2.2 NewPromiseResolveThenableJob: (promise, thenable, then) -> a FRESH
// pair of resolving functions, `then` called with them on the thenable, and a
// throw out of it rejects - through the pair, so a thenable that resolved
// first and then threw keeps its resolution.
[[nodiscard]] value thenable_job(context & cx) {
    return job_native(cx, thenable_job_key, [](context & c, std::span<value> a) {
        if (a.size() < 3) { return value::undefined(); }
        const auto [resolve, reject] = resolvers_for(c, a[0]);
        const value args[2] = {resolve, reject};
        bool threw = false;
        value thrown = value::undefined();
        (void)c.call_fenced(a[2], args, a[1], threw, thrown);
        if (threw) {
            const value reason[1] = {thrown};
            (void)c.call(reject, reason);
        }
        return value::undefined();
    });
}

void enqueue_reaction(context & cx, value record, value argument, bool rejected) {
    cx.queue_microtask(reaction_job(cx), {record, argument, value::boolean(rejected)});
}

// FulfillPromise / RejectPromise (27.2.1.4, 27.2.1.7) and
// TriggerPromiseReactions (27.2.1.8): settle ONCE, then queue every waiting
// reaction. The once is defensive - the resolving functions already gate on
// [[AlreadyResolved]] - and it is what keeps the VM's settler hook
// (context::settle_promise) harmless on a promise something else settled.
void settle(context & cx, value promise, value with, bool rejected) {
    if (!is_promise(promise)) { return; }
    auto * p = static_cast<object_object *>(promise.as_heap());
    if (context::truthy(slot(p, settled_slot))) { return; }
    p->define(value_slot, with, attr_builtin);
    p->define(rejected_slot, value::boolean(rejected), attr_builtin);
    p->define(settled_slot, value::boolean(true), attr_builtin);
    // RejectPromise step 7: HostPromiseRejectionTracker(promise, "reject")
    // when nothing has reacted to it yet.
    if (rejected && !context::promise_is_handled(promise)) {
        cx.track_promise_rejection(promise, false);
    }
    value * handlers = p->find(handlers_slot);
    if (handlers == nullptr || !handlers->is_array()) { return; }
    // COPIED before draining: a handler may register another on this same
    // promise, and appending to the vector being walked invalidates it.
    const std::vector<value> pending = static_cast<array_object *>(handlers->as_heap())->items;
    static_cast<array_object *>(handlers->as_heap())->items.clear();
    for (const value & record : pending) { enqueue_reaction(cx, record, with, rejected); }
}

// Get(resolution, "then") AS ONE COMPLETION RECORD: the getter may throw, and
// 27.2.1.3.2 step 9 rejects with exactly what it threw. A throw a native's
// own `lookup_property` parks is rethrown at that native's return, so reading
// the property inside a native under call_fenced is how the value is had.
[[nodiscard]] value get_then(context & cx, value resolution, bool & threw, value & thrown) {
    const value getter = job_native(cx, get_then_key, [](context & c, std::span<value> a) {
        return c.lookup_property(arg_at(a, 0), "then");
    });
    const value args[1] = {resolution};
    return cx.call_fenced(getter, args, value::undefined(), threw, thrown);
}

// The promise resolve function's body, 27.2.1.3.2 steps 7-16 - what
// `resolve(x)` does once [[AlreadyResolved]] is set. A thenable is NOT
// adopted here: the job is queued and runs on its own tick.
void resolve_promise(context & cx, value promise, value resolution) {
    if (resolution.strict_equals(promise)) {
        settle(cx, promise, cx.make_error("TypeError", "Chaining cycle detected for promise"),
               true);
        return;
    }
    if (!resolution.is_object_like()) {
        settle(cx, promise, resolution, false);
        return;
    }
    bool threw = false;
    value thrown = value::undefined();
    const value then = get_then(cx, resolution, threw, thrown);
    if (threw) {
        settle(cx, promise, thrown, true);
        return;
    }
    if (!then.is_callable()) {
        settle(cx, promise, resolution, false);
        return;
    }
    cx.queue_microtask(thenable_job(cx), {promise, resolution, then});
}

// PerformPromiseThen, 27.2.5.4.1: one reaction record for both outcomes,
// appended while the promise is pending, queued at once when it is not. The
// capability may be EMPTY (an `await`, a combinator's element reaction): the
// record then has no `next` and `deliver` calls nothing after the handler.
void perform_then(context & cx, value promise, value on_ok, value on_err, const capability & cap) {
    object_object * record = new_table(cx);
    record->set("ok", on_ok.is_callable() ? on_ok : value::undefined());
    record->set("err", on_err.is_callable() ? on_err : value::undefined());
    record->set("next", cap.promise);
    record->set("resolve", cap.resolve);
    record->set("reject", cap.reject);
    auto * p = static_cast<object_object *>(promise.as_heap());
    if (!context::truthy(slot(p, settled_slot))) {
        if (value * handlers = p->find(handlers_slot);
            handlers != nullptr && handlers->is_array()) {
            static_cast<array_object *>(handlers->as_heap())
                ->items.push_back(value::object(record));
        }
        cx.mark_promise_handled(promise); // step 11
        return;
    }
    enqueue_reaction(cx, value::object(record), slot(p, value_slot),
                     context::truthy(slot(p, rejected_slot)));
    cx.mark_promise_handled(promise); // steps 9 and 11
}

[[nodiscard]] value intrinsic_promise(context & cx) {
    return slot(promise_prototype(cx), intrinsic_key);
}

// NewPromiseCapability, 27.2.1.5. %Promise% itself takes the direct form; any
// other constructor is run with a GetCapabilitiesExecutor and must hand it
// two callables exactly once. FALSE means a TypeError - or the constructor's
// own throw - is in flight.
[[nodiscard]] bool new_capability(context & cx, value ctor, capability & out) {
    if (ctor.is_heap() && ctor.strict_equals(intrinsic_promise(cx))) {
        out = capability{};
        out.promise = pending_promise(cx);
        return true;
    }
    if (!is_constructor(ctor)) {
        cx.throw_error("TypeError", "Promise capability constructor is not a constructor");
        return false;
    }
    object_object * holder = new_table(cx);
    const value state = value::object(holder);
    const context::rooted keep_state{cx, state};
    auto * executor = cx.allocate<native_object>("", [state](context & c, std::span<value> a) {
        auto * s = static_cast<object_object *>(state.as_heap());
        // 27.2.1.5.1 steps 3-4: a second call with anything defined is refused.
        if (!slot(s, "resolve").is_undefined() || !slot(s, "reject").is_undefined()) {
            c.throw_error("TypeError", "Promise executor has already been invoked");
            return value::undefined();
        }
        s->set("resolve", arg_at(a, 0));
        s->set("reject", arg_at(a, 1));
        return value::undefined();
    });
    executor->is_constructor = false;
    install_arity(cx, executor, 2);
    executor->retained.push_back(state);
    // Construct(C, « executor ») under a fence: a throw inside the
    // constructor is the caller's to see, and a `try` inside it is not.
    const value executor_value = value::object(executor);
    const completion built = fenced(cx, [ctor, executor_value](context & c) -> value {
        const value args[1] = {executor_value};
        return c.construct(ctor, args);
    });
    if (built.threw) {
        cx.throw_value(built.result);
        return false;
    }
    const value promise = built.result;
    const value resolve = slot(holder, "resolve");
    const value reject = slot(holder, "reject");
    if (!resolve.is_callable()) {
        cx.throw_error("TypeError", "Promise resolve function is not callable");
        return false;
    }
    if (!reject.is_callable()) {
        cx.throw_error("TypeError", "Promise reject function is not callable");
        return false;
    }
    out.promise = promise;
    out.resolve = resolve;
    out.reject = reject;
    return true;
}

// SpeciesConstructor, 7.3.22: `O.constructor[@@species]`, the default when
// either is absent, a TypeError when either is the wrong kind of thing.
[[nodiscard]] bool species_constructor(context & cx, value o, value fallback, value & out) {
    const value ctor = cx.lookup_property(o, "constructor");
    if (cx.throw_pending()) { return false; }
    if (ctor.is_undefined()) {
        out = fallback;
        return true;
    }
    if (!ctor.is_object_like()) {
        cx.throw_error("TypeError", "The .constructor property is not an object");
        return false;
    }
    const value species = cx.lookup_property(ctor, "@@species");
    if (cx.throw_pending()) { return false; }
    if (species.is_nullish()) {
        out = fallback;
        return true;
    }
    if (is_constructor(species)) {
        out = species;
        return true;
    }
    cx.throw_error("TypeError", "object.constructor[Symbol.species] is not a constructor");
    return false;
}

// PromiseResolve(C, x), 27.2.4.7.1: x itself when it is a promise whose
// `constructor` is C, else a new C resolved with it.
[[nodiscard]] bool promise_resolve(context & cx, value ctor, value x, value & out) {
    if (is_promise(x)) {
        const value xc = cx.lookup_property(x, "constructor");
        if (cx.throw_pending()) { return false; }
        if (xc.strict_equals(ctor)) {
            out = x;
            return true;
        }
    }
    capability cap;
    if (!new_capability(cx, ctor, cap)) { return false; }
    const context::rooted keep{cx, cap.promise};
    settle_capability(cx, cap, x, false);
    if (cx.throw_pending()) { return false; }
    out = cap.promise;
    return true;
}

// then/catch/finally, once for the program rather than three natives per
// promise. Built lazily so a context with no promise ever made pays nothing;
// the constructor links to it when install_promise runs.
[[nodiscard]] object_object * promise_prototype(context & cx) {
    if (object_object * existing = cx.prototype(context::proto_kind::promise)) { return existing; }
    object_object * table = new_table(cx);
    cx.set_prototype(context::proto_kind::promise, table);
    // 27.2.5.4 Promise.prototype.then
    method(cx, table, "then", 2, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!is_promise(self)) {
            c.throw_error("TypeError", "Promise.prototype.then called on a non-Promise");
            return value::undefined();
        }
        value ctor = value::undefined();
        if (!species_constructor(c, self, intrinsic_promise(c), ctor)) {
            return value::undefined();
        }
        capability cap;
        if (!new_capability(c, ctor, cap)) { return value::undefined(); }
        const context::rooted keep{c, cap.promise};
        perform_then(c, self, arg_at(a, 0), arg_at(a, 1), cap);
        return cap.promise;
    });
    // 27.2.5.1 Promise.prototype.catch: Invoke(this, "then", undefined, f) -
    // generic, through the property, so a subclass's `then` is the one called.
    method(cx, table, "catch", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        // GetV's ToObject: null and undefined are the TypeError right here.
        if (self.is_nullish()) {
            c.throw_error("TypeError", "Promise.prototype.catch called on null or undefined");
            return value::undefined();
        }
        const value then = c.lookup_property(self, "then");
        if (c.throw_pending()) { return value::undefined(); }
        if (!then.is_callable()) {
            c.throw_error("TypeError", "Promise.prototype.catch: then is not a function");
            return value::undefined();
        }
        const value args[2] = {value::undefined(), arg_at(a, 0)};
        return c.call(then, args, self);
    });
    // 27.2.5.3 Promise.prototype.finally: ThenFinally and CatchFinally run the
    // callback, resolve its result through the species constructor, and then
    // restore the original outcome - value or rejection - once that settles.
    method(cx, table, "finally", 1, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!self.is_object_like()) {
            c.throw_error("TypeError", "Promise.prototype.finally called on a non-object");
            return value::undefined();
        }
        value ctor = value::undefined();
        if (!species_constructor(c, self, intrinsic_promise(c), ctor)) {
            return value::undefined();
        }
        const value on_finally = arg_at(a, 0);
        value then_finally = on_finally;
        value catch_finally = on_finally;
        if (on_finally.is_callable()) {
            object_object * shared = new_table(c);
            shared->set("onFinally", on_finally);
            shared->set("C", ctor);
            const value state = value::object(shared);
            const context::rooted keep_state{c, state};
            const auto make = [&](bool rethrow) {
                auto * fn = c.allocate<native_object>(
                    "", [state, rethrow](context & cc, std::span<value> args) {
                        auto * s = static_cast<object_object *>(state.as_heap());
                        const value outcome = arg_at(args, 0);
                        const value result = cc.call(slot(s, "onFinally"), {});
                        if (cc.throw_pending()) { return value::undefined(); }
                        value wrapped = value::undefined();
                        if (!promise_resolve(cc, slot(s, "C"), result, wrapped)) {
                            return value::undefined();
                        }
                        const context::rooted keep_wrapped{cc, wrapped};
                        // valueThunk / thrower: the original outcome, restored
                        // after the callback's promise settles.
                        auto * back = cc.allocate<native_object>(
                            "", [outcome, rethrow](context & c3, std::span<value>) {
                                if (rethrow) {
                                    c3.throw_value(outcome);
                                    return value::undefined();
                                }
                                return outcome;
                            });
                        back->is_constructor = false;
                        install_arity(cc, back, 0);
                        back->retained.push_back(outcome);
                        const value then = cc.lookup_property(wrapped, "then");
                        if (cc.throw_pending()) { return value::undefined(); }
                        if (!then.is_callable()) {
                            cc.throw_error("TypeError", "then is not a function");
                            return value::undefined();
                        }
                        const value then_args[1] = {value::object(back)};
                        return cc.call(then, then_args, wrapped);
                    });
                fn->is_constructor = false;
                install_arity(c, fn, 1);
                fn->retained.push_back(state);
                return value::object(fn);
            };
            then_finally = make(false);
            catch_finally = make(true);
        }
        const context::rooted keep_a{c, then_finally};
        const context::rooted keep_b{c, catch_finally};
        const value then = c.lookup_property(self, "then");
        if (c.throw_pending()) { return value::undefined(); }
        if (!then.is_callable()) {
            c.throw_error("TypeError", "Promise.prototype.finally: then is not a function");
            return value::undefined();
        }
        const value args[2] = {then_finally, catch_finally};
        return c.call(then, args, self);
    });
    table->define("@@toStringTag", cx.string("Promise"), attr_configurable); // 27.2.5.5
    return table;
}

} // namespace promise

// See collections/iterator_internal.hpp.
void perform_promise_then(context & cx, value promise, value on_ok, value on_err) {
    using namespace promise;
    if (!is_promise(promise)) { return; }
    perform_then(cx, promise, on_ok, on_err, capability{});
}

} // namespace ctbrowser::script::detail
