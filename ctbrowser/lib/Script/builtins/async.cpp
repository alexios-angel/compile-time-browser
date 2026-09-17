// ctbrowser.script builtins - Promise.
//
// One of five files carved out of a 4,118-line builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in internal.hpp. The promise machinery itself
// - the resolving functions, the two jobs, PerformPromiseThen, the shared
// prototype - is this file's alone (JSON, which shared the file, is json.cpp
// since 2026-09-12).
//
// REWRITTEN TO 27.2 ON 2026-09-12. The first version fulfilled a promise with
// whatever `resolve` was handed - a promise, a thenable, anything - adopted a
// handler's returned promise inline, read its input list as an Array rather
// than an iterable, and knew nothing of species or subclassing; test262's
// built-ins/Promise measured 34% on it. What is here now is the
// specification's algorithm: CreateResolvingFunctions with [[AlreadyResolved]],
// NewPromiseResolveThenableJob as a SEPARATE job (so `Promise.resolve(thenable)`
// takes two ticks and a handler returning a promise takes three),
// NewPromiseCapability over any constructor, SpeciesConstructor in `then` and
// `finally`, and the four combinators through the iterator protocol with
// IteratorClose on an abrupt step. The object SHAPE is unchanged - the VM
// reads it by name - and is described below.

#include "collections/iterator_internal.hpp"
#include "internal.hpp"
#include "objects/internal.hpp" // key_value, for Promise.allKeyed

namespace ctbrowser::script::detail {

namespace {

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

constexpr std::string_view settled_slot = "__settled";
constexpr std::string_view value_slot = "__value";
constexpr std::string_view rejected_slot = "__rejected";
constexpr std::string_view handlers_slot = "__handlers";
// PRIVATE keys on Promise.prototype (value.hpp: no source text can spell one
// and OwnPropertyKeys never reports one): the %Promise% intrinsic itself, the
// two job natives, and the one native that reads `then` under a fence. They
// live on the prototype table so the collector traces them like any property.
constexpr std::string_view intrinsic_key = "@#Promise";
constexpr std::string_view reaction_job_key = "@#PromiseReactionJob";
constexpr std::string_view thenable_job_key = "@#PromiseResolveThenableJob";
constexpr std::string_view get_then_key = "@#GetThen";
constexpr std::string_view async_from_sync_key = "@#AsyncFromSyncIteratorPrototype";

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
struct capability {
    value promise = value::undefined();
    value resolve = value::undefined();
    value reject = value::undefined();
};

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

} // namespace

// See collections/iterator_internal.hpp.
void perform_promise_then(context & cx, value promise, value on_ok, value on_err) {
    if (!is_promise(promise)) { return; }
    perform_then(cx, promise, on_ok, on_err, capability{});
}

} // namespace ctbrowser::script::detail

namespace ctbrowser::script::builtins_detail {

using detail::capability;
using detail::is_promise;
using detail::pending_promise;
using detail::resolvers_for;
using detail::slot;

namespace {

// THE ERROR `Promise.any` REJECTS WITH when every input rejected. context::
// make_error builds it on the AggregateError prototype install_aggregate_error
// registers; `errors` is the own data property 20.5.7.1.1 step 5 installs, in
// input order. No `message`: 27.2.4.3.1 step 8.d.iii.1 makes one with none.
[[nodiscard]] value aggregate_error(context & cx, value errors) {
    const value made = cx.make_error("AggregateError", "");
    auto * o = static_cast<object_object *>(made.as_heap());
    o->erase("message");
    o->define("errors", errors, attr_builtin);
    return made;
}

// 20.5.7 AggregateError(errors, message, options): a NativeError in shape -
// [[Prototype]] %Error%, prototype under %Error.prototype% with its own
// `name` and `message`, callable without `new` - plus the `errors` list,
// iterated (any iterable) into a fresh array. Installed here rather than in
// objects/errors.cpp because Promise.any is what needs it; skipped when a
// global of that name already exists.
void install_aggregate_error(context & cx) {
    if (cx.has_global("AggregateError")) { return; }
    object_object * proto = detail::new_table(cx);
    if (object_object * error_proto = cx.prototype(context::proto_kind::error)) {
        proto->prototype = value::object(error_proto);
    }
    proto->define("name", cx.string("AggregateError"), attr_builtin);
    proto->define("message", cx.string(""), attr_builtin);
    auto * ctor =
        cx.allocate<native_object>("AggregateError", [proto](context & c, std::span<value> a) {
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = value::object(proto); }
            std::string text;
            if (!arg_at(a, 1).is_undefined()) {
                if (!stringable_arg(c, a[1])) { return value::undefined(); }
                text = c.to_string(a[1]);
                if (c.throw_pending()) { return value::undefined(); }
                made->define("message", c.string(text), attr_builtin);
            }
            if (const value options = arg_at(a, 2); options.is_object_like()) {
                const bool has_cause = c.has_property(options, c.string("cause"));
                if (c.throw_pending()) { return value::undefined(); }
                if (has_cause) {
                    const value cause = c.lookup_property(options, "cause");
                    if (c.throw_pending()) { return value::undefined(); }
                    made->define("cause", cause, attr_builtin);
                }
            }
            made->define("@#ErrorData",
                         c.string("AggregateError" + (text.empty() ? std::string{} : ": " + text) +
                                  c.current_stack()),
                         attr_none);
            // Step 5: IteratorToList(GetIterator(errors)) - every value, into a
            // fresh array, and a non-iterable is the TypeError GetIterator throws.
            const value iterator = c.get_iterator(arg_at(a, 0));
            if (c.throw_pending() || !iterator.is_object_like()) { return value::undefined(); }
            detail::iterator_record rec;
            if (!detail::iterator_direct(c, iterator, rec)) { return value::undefined(); }
            const value list = c.make_array();
            const context::rooted keep{c, list};
            for (;;) {
                bool done = false;
                value item = value::undefined();
                if (!detail::iterator_step_value(c, rec, done, item)) { return value::undefined(); }
                if (done) { break; }
                static_cast<array_object *>(list.as_heap())->items.push_back(item);
            }
            made->define("errors", list, attr_builtin);
            return self;
        });
    link_constructor(cx, proto, "AggregateError", 2, value::object(ctor));
    ctor->define("prototype", value::object(proto), attr_none);
    if (const value error_ctor = cx.global("Error"); error_ctor.is_callable()) {
        ctor->proto_link = error_ctor;
    }
    cx.register_error_prototype("AggregateError", proto);
    cx.define_global("AggregateError", value::object(ctor));
}

enum class combine : std::uint8_t {
    all,
    all_settled,
    any,
    race,
    all_keyed,
    all_settled_keyed
};
[[nodiscard]] constexpr bool keyed(combine k) {
    return k == combine::all_keyed || k == combine::all_settled_keyed;
}
[[nodiscard]] constexpr bool settles_each(combine k) {
    return k == combine::all_settled || k == combine::all_settled_keyed;
}

// CreateKeyedPromiseCombinatorResultObject (Promise.allKeyed): a
// null-prototype object, one own data property per key, in key order.
[[nodiscard]] value keyed_result(context & cx, value keys, value values) {
    object_object * record = detail::new_table(cx);
    record->prototype = value::undefined(); // explicit null, see object_object::prototype
    const auto & names = static_cast<array_object *>(keys.as_heap())->items;
    const auto & held = static_cast<array_object *>(values.as_heap())->items;
    for (std::size_t i = 0; i < names.size() && i < held.size(); ++i) {
        record->define(cx.to_string(names[i]), held[i], attr_default);
    }
    return value::object(record);
}

// The element functions of 27.2.4.1.3 / 27.2.4.2.2-3 / 27.2.4.3.2: each
// writes its outcome into its own slot ONCE ([[AlreadyCalled]]) and the last
// to arrive settles the capability. State lives in one object every element
// function retains - a `value` in a lambda capture is not a root - and so
// does [[AlreadyCalled]]: allSettled's fulfilled and rejected functions for
// one index SHARE the record (27.2.4.2.1 step 8.k-t), so it is a per-index
// slot in the state's `called` list rather than a capture.
[[nodiscard]] value element_fn(context & cx, combine kind, value state, std::size_t index,
                               bool rejected) {
    auto * fn = cx.allocate<native_object>(
        "", [kind, state, index, rejected](context & c, std::span<value> a) {
            auto * s = static_cast<object_object *>(state.as_heap());
            auto & called = static_cast<array_object *>(slot(s, "called").as_heap())->items;
            if (index >= called.size()) { called.resize(index + 1, value::boolean(false)); }
            if (context::truthy(called[index])) { return value::undefined(); }
            called[index] = value::boolean(true);
            const value with = arg_at(a, 0);
            value entry = with;
            if (settles_each(kind)) {
                entry = c.make_object();
                auto * record = static_cast<object_object *>(entry.as_heap());
                record->set("status", c.string(rejected ? "rejected" : "fulfilled"));
                record->set(rejected ? "reason" : "value", with);
            }
            const value values = slot(s, "values");
            auto & items = static_cast<array_object *>(values.as_heap())->items;
            if (index < items.size()) { items[index] = entry; }
            const double left = slot(s, "left").as_number() - 1.0;
            s->set("left", value::number(left));
            if (left > 0) { return value::undefined(); }
            capability cap;
            cap.promise = slot(s, "promise");
            cap.resolve = slot(s, "resolve");
            cap.reject = slot(s, "reject");
            if (kind == combine::any) {
                detail::settle_capability(c, cap, aggregate_error(c, values), true);
            } else if (keyed(kind)) {
                detail::settle_capability(c, cap, keyed_result(c, slot(s, "keys"), values), false);
            } else {
                detail::settle_capability(c, cap, values, false);
            }
            return value::undefined();
        });
    fn->is_constructor = false;
    detail::install_arity(cx, fn, 1);
    fn->retained.push_back(state);
    return value::object(fn);
}

// Promise.all / allSettled / any / race, 27.2.4.1-3 and 27.2.4.5: one walk.
// The capability is made first (its TypeError propagates); everything after
// it is IfAbruptRejectPromise - run under one fence, and an abrupt step that
// did not come from the iterator itself closes the iterator before the
// capability is rejected.
[[nodiscard]] value combinator(context & cx, combine kind, std::span<value> a) {
    const value ctor = cx.current_this();
    if (!ctor.is_object_like()) {
        cx.throw_error("TypeError", "Promise combinator called on a non-object");
        return value::undefined();
    }
    capability cap;
    if (!detail::new_capability(cx, ctor, cap)) { return value::undefined(); }
    detail::materialise(cx, cap);
    const context::rooted keep_promise{cx, cap.promise};
    const context::rooted keep_resolve{cx, cap.resolve};
    const context::rooted keep_reject{cx, cap.reject};
    const value iterable = arg_at(a, 0);
    value iterator = value::undefined();
    bool close = false;
    const detail::completion run = detail::fenced(cx, [&](context & c) -> value {
        // GetPromiseResolve, 27.2.4.1.1: read once, checked once.
        const value promise_resolve = c.lookup_property(ctor, "resolve");
        if (c.throw_pending()) { return value::undefined(); }
        if (!promise_resolve.is_callable()) {
            c.throw_error("TypeError", "Promise resolve is not a function");
            return value::undefined();
        }
        // The items: an iterator's values, or (allKeyed) the own ENUMERABLE
        // properties of an object, each key's enumerability re-read as it is
        // reached.
        std::vector<std::string> own_keys;
        value next = value::undefined();
        if (keyed(kind)) {
            if (!iterable.is_object_like()) {
                c.throw_error("TypeError", "Promise.allKeyed: argument is not an object");
                return value::undefined();
            }
            own_keys = detail::own_property_names(c, iterable, detail::key_filter::all);
            if (c.throw_pending()) { return value::undefined(); }
        } else {
            iterator = c.get_iterator(iterable);
            if (c.throw_pending() || !iterator.is_object_like()) { return value::undefined(); }
            next = c.lookup_property(iterator, "next");
            if (c.throw_pending()) { return value::undefined(); }
        }
        object_object * shared = detail::new_table(c);
        const value state = value::object(shared);
        const context::rooted keep_state{c, state};
        const value values = c.make_array();
        const value keys = c.make_array();
        shared->set("values", values);
        shared->set("keys", keys);
        shared->set("called", c.make_array());
        shared->set("left", value::number(1));
        shared->set("promise", cap.promise);
        shared->set("resolve", cap.resolve);
        shared->set("reject", cap.reject);
        std::size_t key_at = 0;
        for (std::size_t index = 0;; ++index) {
            close = !keyed(kind);
            bool done = false;
            value item = value::undefined();
            if (keyed(kind)) {
                done = true;
                for (; key_at < own_keys.size(); ++key_at) {
                    context::property_descriptor found;
                    const bool present = c.own_property(iterable, own_keys[key_at], found);
                    if (c.throw_pending()) { return value::undefined(); }
                    if (!present || !found.enumerable) { continue; }
                    const value key = detail::key_value(c, own_keys[key_at]);
                    item = c.lookup_index(iterable, key);
                    if (c.throw_pending()) { return value::undefined(); }
                    static_cast<array_object *>(keys.as_heap())->items.push_back(key);
                    ++key_at;
                    done = false;
                    break;
                }
            } else {
                detail::iterator_record rec{iterator, next, false};
                if (!detail::iterator_step_value(c, rec, done, item)) {
                    close = false; // the iterator record is done: no IteratorClose
                    return value::undefined();
                }
            }
            if (done) {
                close = false;
                if (kind == combine::race) { return value::undefined(); }
                const double left = slot(shared, "left").as_number() - 1.0;
                shared->set("left", value::number(left));
                if (left <= 0) {
                    if (kind == combine::any) {
                        detail::settle_capability(c, cap, aggregate_error(c, values), true);
                    } else if (keyed(kind)) {
                        detail::settle_capability(c, cap, keyed_result(c, keys, values), false);
                    } else {
                        detail::settle_capability(c, cap, values, false);
                    }
                }
                return value::undefined();
            }
            const context::rooted keep_item{c, item};
            if (kind != combine::race) {
                static_cast<array_object *>(values.as_heap())->items.push_back(value::undefined());
            }
            const value resolve_args[1] = {item};
            const value next_promise = c.call(promise_resolve, resolve_args, ctor);
            if (c.throw_pending()) { return value::undefined(); }
            const context::rooted keep_next{c, next_promise};
            value on_ok = cap.resolve;
            value on_err = cap.reject;
            if (kind != combine::any && kind != combine::race) {
                on_ok = element_fn(c, kind, state, index, false);
            }
            if (settles_each(kind) || kind == combine::any) {
                on_err = element_fn(c, kind, state, index, true);
            }
            const context::rooted keep_ok{c, on_ok};
            const context::rooted keep_err{c, on_err};
            if (kind != combine::race) {
                shared->set("left", value::number(slot(shared, "left").as_number() + 1.0));
            }
            // Invoke(nextPromise, "then", ...): through the property, so a
            // subclass's or a thenable's own `then` is the one called.
            if (next_promise.is_nullish()) {
                c.throw_error("TypeError", "Cannot read properties of " +
                                               std::string{context::type_of(next_promise)});
                return value::undefined();
            }
            const value then = c.lookup_property(next_promise, "then");
            if (c.throw_pending()) { return value::undefined(); }
            if (!then.is_callable()) {
                c.throw_error("TypeError", "then is not a function");
                return value::undefined();
            }
            const value then_args[2] = {on_ok, on_err};
            (void)c.call(then, then_args, next_promise);
            if (c.throw_pending()) { return value::undefined(); }
        }
    });
    if (run.threw) {
        if (close) { detail::iterator_close_quietly(cx, iterator); }
        const value reason[1] = {run.result};
        (void)cx.call(cap.reject, reason);
    }
    return cap.promise;
}

// --- CreateAsyncFromSyncIterator, 27.1.6 -----------------------------------
//
// What `for await` and `yield*` in an async generator wrap a SYNC iterator in:
// `next`/`return`/`throw` each answer a promise of the record, and a promise
// in the record's `value` is awaited before the record is delivered
// (AsyncFromSyncIteratorContinuation, 27.1.6.4). The three live on ONE
// prototype table - %AsyncFromSyncIteratorPrototype%, whose own [[Prototype]]
// is %AsyncIteratorPrototype% - and read the sync iterator off `this`.

constexpr std::string_view sync_iterator_slot = "@#SyncIterator";
constexpr std::string_view sync_next_slot = "@#SyncNext";

// 27.1.6.4. `close_on_rejection` is the ES2025 addition: a rejected `value`
// closes the sync iterator before the rejection is reported.
[[nodiscard]] value continuation(context & cx, value result, const capability & cap,
                                 value sync_iterator, bool close_on_rejection) {
    const detail::completion parts = detail::fenced(cx, [&](context & c) -> value {
        const value done = c.lookup_property(result, "done");
        if (c.throw_pending()) { return value::undefined(); }
        const value item = c.lookup_property(result, "value");
        if (c.throw_pending()) { return value::undefined(); }
        const value pair = c.make_array();
        auto * list = static_cast<array_object *>(pair.as_heap());
        list->items.push_back(value::boolean(context::truthy(done)));
        list->items.push_back(item);
        return pair;
    });
    if (parts.threw) {
        detail::settle_capability(cx, cap, parts.result, true);
        return cap.promise;
    }
    const auto & pair = static_cast<array_object *>(parts.result.as_heap())->items;
    const bool done = pair[0].as_boolean();
    const value item = pair[1];
    value wrapper = value::undefined();
    {
        const detail::completion wrapped = detail::fenced(cx, [&](context & c) -> value {
            value out = value::undefined();
            (void)detail::promise_resolve(c, detail::intrinsic_promise(c), item, out);
            return out;
        });
        if (wrapped.threw) {
            if (!done && close_on_rejection) { detail::iterator_close_quietly(cx, sync_iterator); }
            detail::settle_capability(cx, cap, wrapped.result, true);
            return cap.promise;
        }
        wrapper = wrapped.result;
    }
    const context::rooted keep_wrapper{cx, wrapper};
    auto * unwrap = cx.allocate<native_object>("", [done](context & c, std::span<value> got) {
        return c.iter_result(arg_at(got, 0), done);
    });
    unwrap->is_constructor = false;
    detail::install_arity(cx, unwrap, 1);
    value on_rejected = value::undefined();
    if (!done && close_on_rejection) {
        auto * closer =
            cx.allocate<native_object>("", [sync_iterator](context & c, std::span<value> got) {
                detail::iterator_close_quietly(c, sync_iterator);
                c.throw_value(arg_at(got, 0));
                return value::undefined();
            });
        closer->is_constructor = false;
        detail::install_arity(cx, closer, 1);
        closer->retained.push_back(sync_iterator);
        on_rejected = value::object(closer);
    }
    const context::rooted keep_unwrap{cx, value::object(unwrap)};
    const context::rooted keep_closer{cx, on_rejected};
    detail::perform_then(cx, wrapper, value::object(unwrap), on_rejected, cap);
    return cap.promise;
}

// %AsyncIteratorPrototype%, 27.1.3: one method, @@asyncIterator answering
// `this`. %AsyncGeneratorPrototype% and %AsyncFromSyncIteratorPrototype% both
// inherit from it. Kept on Promise.prototype under a private key.
constexpr std::string_view async_iterator_key = "@#AsyncIteratorPrototype";
[[nodiscard]] object_object * async_iterator_prototype(context & cx) {
    object_object * promise_proto = detail::promise_prototype(cx);
    if (const value * held = promise_proto->find(async_iterator_key); held != nullptr) {
        return static_cast<object_object *>(held->as_heap());
    }
    object_object * table = detail::new_table(cx);
    promise_proto->define(async_iterator_key, value::object(table), attr_none);
    detail::method(cx, table, "@@asyncIterator", 0,
                   [](context & c, std::span<value>) { return c.current_this(); });
    return table;
}

[[nodiscard]] object_object * async_from_sync_prototype(context & cx) {
    object_object * promise_proto = detail::promise_prototype(cx);
    if (const value * held = promise_proto->find(detail::async_from_sync_key); held != nullptr) {
        return static_cast<object_object *>(held->as_heap());
    }
    object_object * table = detail::new_table(cx);
    promise_proto->define(detail::async_from_sync_key, value::object(table), attr_none);
    table->prototype = value::object(async_iterator_prototype(cx));
    // The receiver's sync iterator, or the promise rejected with a TypeError.
    const auto open = [](context & c, capability & cap, value & sync, value & next) {
        if (!detail::new_capability(c, detail::intrinsic_promise(c), cap)) { return false; }
        const value self = c.current_this();
        if (self.is_object()) {
            auto * o = static_cast<object_object *>(self.as_heap());
            sync = slot(o, sync_iterator_slot);
            next = slot(o, sync_next_slot);
            if (sync.is_object_like()) { return true; }
        }
        detail::settle_capability(
            c, cap, c.make_error("TypeError", "not an async-from-sync iterator"), true);
        return false;
    };
    // 27.1.6.2.1 next
    detail::method(cx, table, "next", 1, [open](context & c, std::span<value> a) {
        capability cap;
        value sync = value::undefined(), next = value::undefined();
        if (!open(c, cap, sync, next)) { return cap.promise; }
        const context::rooted keep{c, cap.promise};
        const bool with_value = !a.empty();
        const value sent = arg_at(a, 0);
        const detail::completion result = detail::fenced(c, [&](context & cc) -> value {
            if (!next.is_callable()) {
                cc.throw_error("TypeError", "iterator.next is not a function");
                return value::undefined();
            }
            const value args[1] = {sent};
            const value out = cc.call(
                next, with_value ? std::span<const value>{args} : std::span<const value>{}, sync);
            if (cc.throw_pending()) { return value::undefined(); }
            if (!out.is_object_like()) {
                cc.throw_error("TypeError", "iterator result is not an object");
            }
            return out;
        });
        if (result.threw) {
            detail::settle_capability(c, cap, result.result, true);
            return cap.promise;
        }
        return continuation(c, result.result, cap, sync, true);
    });
    // 27.1.6.2.2 return / 27.1.6.2.3 throw
    const auto forward = [open](const char * name) {
        return [open, name](context & c, std::span<value> a) {
            capability cap;
            value sync = value::undefined(), next = value::undefined();
            if (!open(c, cap, sync, next)) { return cap.promise; }
            const context::rooted keep{c, cap.promise};
            const bool with_value = !a.empty();
            const value sent = arg_at(a, 0);
            const bool returning = std::string_view{name} == "return";
            bool absent = false;
            const detail::completion result = detail::fenced(c, [&](context & cc) -> value {
                const value method = cc.lookup_property(sync, name);
                if (cc.throw_pending()) { return value::undefined(); }
                if (method.is_nullish()) {
                    absent = true;
                    if (!returning) {
                        // No `throw`: close the sync iterator so it can clean
                        // up - its own throw rejects - then the TypeError
                        // below (27.1.6.2.3 step 7).
                        (void)detail::iterator_close(cc, sync);
                    }
                    return value::undefined();
                }
                if (!method.is_callable()) {
                    cc.throw_error("TypeError",
                                   std::string{"iterator."} + name + " is not a function");
                    return value::undefined();
                }
                const value args[1] = {sent};
                const value out = cc.call(
                    method, with_value ? std::span<const value>{args} : std::span<const value>{},
                    sync);
                if (cc.throw_pending()) { return value::undefined(); }
                if (!out.is_object_like()) {
                    cc.throw_error("TypeError", "iterator result is not an object");
                }
                return out;
            });
            if (result.threw) {
                detail::settle_capability(c, cap, result.result, true);
                return cap.promise;
            }
            if (absent) {
                if (returning) {
                    detail::settle_capability(c, cap, c.iter_result(sent, true), false);
                } else {
                    detail::settle_capability(
                        c, cap,
                        c.make_error("TypeError", "The iterator does not provide a 'throw' method"),
                        true);
                }
                return cap.promise;
            }
            return continuation(c, result.result, cap, sync, !returning);
        };
    };
    detail::method(cx, table, "return", 1, forward("return"));
    detail::method(cx, table, "throw", 1, forward("throw"));
    return table;
}

} // namespace

// Promise
void install_promise(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * proto = detail::promise_prototype(cx);
    // 27.6.1: %AsyncGeneratorPrototype% inherits from %AsyncIteratorPrototype%
    // (its table is install_generator's; only the link is set here).
    if (object_object * async_gen = cx.prototype(context::proto_kind::async_generator)) {
        async_gen->prototype = value::object(async_iterator_prototype(cx));
    }
    // What `await` and an async function's return need: a pending promise, a
    // way to settle one, and PromiseResolve(%Promise%, v). The VM can READ a
    // promise - it always could - but making and settling run this library's
    // own logic, queue included. The settler RESOLVES (27.2.1.3.2) rather
    // than fulfils, so an async body that returns a thenable adopts it.
    cx.set_promise_factory([](context & c, value v, bool rejected) {
        if (rejected) {
            const value made = pending_promise(c);
            detail::settle(c, made, v, true);
            return made;
        }
        value out = value::undefined();
        const context::rooted keep{c, v};
        if (!detail::promise_resolve(c, detail::intrinsic_promise(c), v, out)) {
            // The `constructor` read threw: a promise rejected with it is
            // the only answer a factory with no completion channel can give.
            out = pending_promise(c);
        }
        return out;
    });
    cx.set_pending_promise_factory(pending_promise);
    cx.set_promise_settler([](context & c, value promise, value with, bool rejected) {
        if (rejected) {
            detail::settle(c, promise, with, true);
        } else {
            detail::resolve_promise(c, promise, with);
        }
    });
    // GetIterator(obj, async) - see async_iterator_name. A sync iterator is
    // wrapped in an %AsyncFromSyncIteratorPrototype% object (27.1.6.1).
    cx.define_native(std::string{async_iterator_name}, [](context & c, std::span<value> a) {
        const value source = a.empty() ? value::undefined() : a[0];
        if (source.is_nullish()) {
            c.throw_error("TypeError", "the value is not async iterable");
            return value::undefined();
        }
        // 7.4.3 GetIterator(async): GetMethod(@@asyncIterator) - a value that
        // is neither undefined, null nor callable is the TypeError right
        // there, and @@iterator is asked only when the method is ABSENT.
        const value async = c.lookup_property(source, "@@asyncIterator");
        if (c.throw_pending()) { return value::undefined(); }
        if (async.is_callable()) {
            const value made = c.call(async, {}, source);
            if (c.throw_pending()) { return value::undefined(); }
            if (!made.is_object_like()) {
                c.throw_error("TypeError",
                              "Result of the Symbol.asyncIterator method is not an object");
                return value::undefined();
            }
            return made;
        }
        if (!async.is_nullish()) {
            c.throw_error("TypeError", "[Symbol.asyncIterator] is not a function");
            return value::undefined();
        }
        const value inner = c.get_iterator(source);
        if (c.throw_pending() || !inner.is_object_like()) { return value::undefined(); }
        const context::rooted keep{c, inner};
        const value next = c.lookup_property(inner, "next");
        if (c.throw_pending()) { return value::undefined(); }
        auto * wrapper = new_table(c);
        wrapper->prototype = value::object(async_from_sync_prototype(c));
        wrapper->define(sync_iterator_slot, inner, attr_none);
        wrapper->define(sync_next_slot, next, attr_none);
        return value::object(wrapper);
    });
    // `Promise.reject` under the compiler's name for it: what an async body's
    // fence returns for a throw it did not catch. See promise_reject_name.
    cx.define_native(std::string{promise_reject_name}, [](context & c, std::span<value> a) {
        const value made = pending_promise(c);
        detail::settle(c, made, arg_at(a, 0), true);
        return made;
    });
    // 27.2.3.1 Promise(executor). The executor runs IMMEDIATELY and is handed
    // resolve and reject; a promise it does not settle stays pending until
    // something later calls one of them. p5.js opens with it:
    //
    //   new Promise((resolve) => {
    //     if (document.readyState === 'complete') { resolve(); }
    //     else { window.addEventListener('load', resolve, false); }
    //   })
    //
    // Callable AND a namespace: the statics are installed on it directly with
    // detail::method, so each is { writable, enumerable: FALSE, configurable }
    // as clause 27.2.4 has them.
    auto * promise_new = cx.allocate<native_object>("Promise", [](context & c, std::span<value> a) {
        // 27.2.3.1 step 1: NewTarget undefined is a TypeError. A native has no
        // NewTarget; what it has is the instance `new` (or a subclass's
        // `super()`) made for it. "An object that is not already a promise"
        // rather than constructing_this's "an EMPTY object", because this
        // engine runs a derived class's field initialisers BEFORE the body,
        // so `class P extends Promise { x = 1 }` arrives with `x` set.
        const value self = c.current_this();
        if (!self.is_object() || is_promise(self)) {
            c.throw_error("TypeError", "Promise constructor cannot be invoked without 'new'");
            return value::undefined();
        }
        if (!arg_at(a, 0).is_callable()) {
            c.throw_error("TypeError", "Promise resolver is not a function");
            return value::undefined();
        }
        const value promise = detail::init_promise(c, self);
        const auto [resolve_fn, reject_fn] = resolvers_for(c, promise);
        const value args[2] = {resolve_fn, reject_fn};
        bool threw = false;
        value thrown = value::undefined();
        (void)c.call_fenced(a[0], args, value::undefined(), threw, thrown);
        if (threw) {
            // Step 10: an executor that throws rejects - through the reject
            // function, so an executor that resolved first keeps that.
            const value reason[1] = {thrown};
            (void)c.call(reject_fn, reason);
        }
        return promise;
    });
    proto->define(detail::intrinsic_key, value::object(promise_new), attr_none);
    // 27.2.4.7 Promise.resolve / 27.2.4.6 Promise.reject: over `this`, so a
    // subclass's static makes a subclass instance.
    install_aggregate_error(cx);
    method(cx, promise_new, "resolve", 1, [](context & c, std::span<value> a) {
        const value ctor = c.current_this();
        if (!ctor.is_object_like()) {
            c.throw_error("TypeError", "Promise.resolve called on a non-object");
            return value::undefined();
        }
        value out = value::undefined();
        if (!detail::promise_resolve(c, ctor, arg_at(a, 0), out)) { return value::undefined(); }
        return out;
    });
    method(cx, promise_new, "reject", 1, [](context & c, std::span<value> a) {
        capability cap;
        if (!detail::new_capability(c, c.current_this(), cap)) { return value::undefined(); }
        const context::rooted keep{c, cap.promise};
        detail::settle_capability(c, cap, arg_at(a, 0), true);
        return cap.promise;
    });
    // 27.2.4.9 Promise.try: the callback runs NOW, and its return or throw
    // settles the promise.
    method(cx, promise_new, "try", 1, [](context & c, std::span<value> a) {
        const value ctor = c.current_this();
        if (!ctor.is_object_like()) {
            c.throw_error("TypeError", "Promise.try called on a non-object");
            return value::undefined();
        }
        capability cap;
        if (!detail::new_capability(c, ctor, cap)) { return value::undefined(); }
        const context::rooted keep{c, cap.promise};
        bool threw = false;
        value thrown = value::undefined();
        const std::span<const value> rest =
            a.size() > 1 ? std::span<const value>{a}.subspan(1) : std::span<const value>{};
        const value produced = c.call_fenced(arg_at(a, 0), rest, value::undefined(), threw, thrown);
        detail::settle_capability(c, cap, threw ? thrown : produced, threw);
        return cap.promise;
    });
    method(cx, promise_new, "all", 1,
           [](context & c, std::span<value> a) { return combinator(c, combine::all, a); });
    method(cx, promise_new, "allSettled", 1,
           [](context & c, std::span<value> a) { return combinator(c, combine::all_settled, a); });
    method(cx, promise_new, "any", 1,
           [](context & c, std::span<value> a) { return combinator(c, combine::any, a); });
    method(cx, promise_new, "race", 1,
           [](context & c, std::span<value> a) { return combinator(c, combine::race, a); });
    // Promise.allKeyed / allSettledKeyed (the await-dictionary proposal): the
    // same walk over an object's own enumerable properties, answering a
    // null-prototype object keyed like the input.
    method(cx, promise_new, "allKeyed", 1,
           [](context & c, std::span<value> a) { return combinator(c, combine::all_keyed, a); });
    method(cx, promise_new, "allSettledKeyed", 1, [](context & c, std::span<value> a) {
        return combinator(c, combine::all_settled_keyed, a);
    });
    // 27.2.4.8 Promise.withResolvers: `new Promise(executor)` turned inside
    // out - the same promise and the same two functions, handed back as an
    // object instead of to a callback.
    method(cx, promise_new, "withResolvers", 0, [](context & c, std::span<value>) {
        capability cap;
        if (!detail::new_capability(c, c.current_this(), cap)) { return value::undefined(); }
        const context::rooted keep{c, cap.promise};
        detail::materialise(c, cap);
        const value out = c.make_object();
        auto * fields = static_cast<object_object *>(out.as_heap());
        fields->set("promise", cap.promise);
        fields->set("resolve", cap.resolve);
        fields->set("reject", cap.reject);
        return out;
    });
    // 27.2.4.10 get Promise[@@species]: `this`.
    {
        auto * species =
            detail::method_native(cx, "get [Symbol.species]",
                                  [](context & c, std::span<value>) { return c.current_this(); });
        detail::install_arity(cx, species, 0);
        promise_new->define_accessor("@@species", value::object(species), value::undefined(),
                                     attr_configurable);
    }
    detail::constant(promise_new, "prototype", value::object(proto));
    link_constructor(cx, proto, "Promise", 1, value::object(promise_new));
    cx.define_global("Promise", value::object(promise_new));
    // Iterator and the iterator helpers: after install_generator's tables
    // exist, which is any point in this function. builtins.cpp is not this
    // file's to edit; the call belongs there.
    install_iterator(cx);
    // WeakRef and FinalizationRegistry (collections/weak.cpp). ctcompile's
    // escape-cycle test pinned their ABSENCE as the divergence ND-2 until
    // 2026-09-13 (its 73d4e034 retired the three probes;
    // ctcompile/docs/native-divergences.md says so), which is what kept this
    // call commented out for a day.
    install_weak_refs(cx);
    install_disposable(cx);

    // 19.2.2 and 19.2.3, both of arity 1: ? ToNumber(number) - through
    // ToPrimitive, so a valueOf runs and a throw from it (or a Symbol, or a
    // BigInt) propagates rather than reading as NaN.
    detail::global_fn(cx, "isNaN", 1, [](context & c, std::span<value> a) {
        const value v = arg_at(a, 0);
        if (!numeric_arg(c, v)) { return value::undefined(); }
        const double n = c.to_number_value(v);
        if (c.throw_pending()) { return value::undefined(); }
        return value::boolean(std::isnan(n));
    });
    detail::global_fn(cx, "isFinite", 1, [](context & c, std::span<value> a) {
        const value v = arg_at(a, 0);
        if (!numeric_arg(c, v)) { return value::undefined(); }
        const double n = c.to_number_value(v);
        if (c.throw_pending()) { return value::undefined(); }
        return value::boolean(std::isfinite(n));
    });
    // `String` is a NAMESPACE as well as a coercion, the same way Number is.
    // `String.fromCharCode.apply(null, bytes)` is how a page turns a byte array
    // into text - 27 uses in p5.js - and it read undefined and applied it.
    {
        auto * string_ctor =
            cx.allocate<native_object>("String", [](context & c, std::span<value> a) {
                // A SYMBOL IS DESCRIBED, NOT COERCED. `String(sym)` is the one
                // conversion the specification allows on a symbol (22.1.1.1
                // step 2) and it yields "Symbol(description)". Everything else
                // here goes through `to_string`, which for a symbol returns its
                // internal KEY - that is deliberate and load-bearing, because
                // computed property access resolves `o[sym]` through the same
                // call, so it cannot be changed without separating
                // ToPropertyKey from ToString. Special-casing the explicit
                // conversion is the part that can be had cheaply.
                const value self = c.current_this();
                const bool constructing = detail::constructing_this(self);
                value made = c.string(std::string{});
                if (!a.empty() && a[0].is_kind(heap_kind::symbol) && !constructing) {
                    made =
                        c.string("Symbol(" +
                                 static_cast<symbol_object *>(a[0].as_heap())->description + ")");
                } else if (!a.empty()) {
                    made = c.string(string_arg(c, a[0]));
                    if (c.throw_pending()) { return value::undefined(); }
                }
                // 22.1.1.1 step 3: a call converts, `new` wraps - the same String
                // exotic object `Object("ab")` builds, see detail::wrap_primitive.
                return constructing ? detail::wrap_primitive(c, self, made) : made;
            });
        // detail::method, not `set`: clause 17 makes every one of these
        // { writable: true, enumerable: FALSE, configurable: true }, and `set`
        // gave them the default attributes - so `Object.keys(String)` listed
        // fromCharCode and fromCodePoint and `for (k in String)` walked them.
        const auto stat = [&](const char * name, double arity, native_fn fn) {
            detail::method(cx, string_ctor, name, arity, std::move(fn));
        };
        // UTF-8 out (core's append_utf8), because strings here are bytes: a
        // code point above 0x7F becomes its encoding rather than one char,
        // which is what makes the round trip through String.prototype work.
        stat("fromCharCode", 1, [](context & c, std::span<value> a) {
            std::string out;
            for (std::size_t i = 0; i < a.size(); ++i) {
                // ToUint16 of ToNumber (22.1.2.1): an object's valueOf runs.
                if (!numeric_arg(c, a[i])) { return value::undefined(); }
                const double n = c.to_number_value(a[i]);
                if (c.throw_pending()) { return value::undefined(); }
                append_utf8(out, context::to_uint32(value::number(n)) & 0xFFFFu);
            }
            return c.string(out);
        });
        stat("fromCodePoint", 1, [](context & c, std::span<value> a) {
            std::string out;
            for (std::size_t i = 0; i < a.size(); ++i) {
                // 22.1.2.2 step 2c: a code point must be an INTEGER in
                // [0, 0x10FFFF] and anything else is a RangeError. to_uint32
                // wrapped instead, so `String.fromCodePoint(-1)` produced the
                // encoding of 0xFFFFFFFF and `fromCodePoint(1.5)` produced one
                // for 1.
                const double code = c.to_number_value(a[i]);
                if (code != std::trunc(code) || std::isnan(code) || code < 0 || code > 0x10FFFF) {
                    c.throw_error("RangeError", "Invalid code point");
                    return c.string(std::string{});
                }
                append_utf8(out, static_cast<char32_t>(code));
            }
            return c.string(out);
        });
        // `String.raw`, 22.1.2.4. It is the tag every template-literal library
        // reaches for and it is also callable directly, which is the only way
        // this engine can reach it - the compiler still refuses a TAGGED
        // template (docs/script.md names it), so `String.raw({raw: [...]}, ...)`
        // is the whole surface. 25 of the 30 test262 files read "TypeError:
        // raw is undefined, not a function" before this.
        stat("raw", 1, [](context & c, std::span<value> a) {
            const value cooked = arg_at(a, 0);
            const value literals = c.lookup_property(cooked, "raw");
            const value raw_len = c.lookup_property(literals, "length");
            const double count = to_length(c.to_number_value(raw_len));
            std::string out;
            for (double k = 0; k < count; k += 1.0) {
                out += c.to_string(c.lookup_index(literals, value::number(k)));
                if (k + 1 == count) { break; }
                // One substitution BETWEEN each pair of literals, and running
                // out of them ends the interpolation rather than the string:
                // `String.raw({raw: ['a','b','c']}, 'x')` is "axbc".
                const std::size_t at = static_cast<std::size_t>(k) + 1;
                if (at < a.size()) { out += c.to_string(a[at]); }
            }
            return c.string(out);
        });
        if (object_object * table = cx.prototype(context::proto_kind::string)) {
            detail::constant(string_ctor, "prototype", value::object(table));
            link_constructor(cx, table, "String", 1, value::object(string_ctor));
        }
        cx.define_global("String", value::object(string_ctor));
    }
    // `Number` is installed by install_number, which gives it the statics as
    // well as the coercion. Defining it again here would replace the whole
    // thing with a bare function and silently drop Number.isFinite and its
    // siblings - which is exactly what it used to do.
    //
    // `Boolean` is the same, and it WAS being redefined here, one line under
    // that warning: install_boolean gives it a prototype and this replaced it
    // with a bare coercion, so `Object.getPrototypeOf(true) === Boolean.prototype`
    // was false and `true.toString()` found nothing.
}

} // namespace ctbrowser::script::builtins_detail
