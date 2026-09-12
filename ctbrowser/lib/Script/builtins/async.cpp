// ctbrowser.script builtins - Promise.
//
// One of five files carved out of a 4,118-line builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in internal.hpp. The promise machinery itself
// - deliver, settle, settle_with, the shared prototype - is this file's alone
// (JSON, which shared the file, is json.cpp since 2026-09-12).

#include "internal.hpp"

namespace ctbrowser::script::detail {

namespace {

// --- promises ---------------------------------------------------------------
//
// A promise is an ordinary object carrying `__value`, `__rejected`, `__settled`
// and `__handlers`, with then/catch/finally on one shared prototype. A handler
// never runs the moment a promise settles: `settle` and `settle_with` QUEUE a
// delivery (enqueue_delivery -> context::queue_microtask) and the event loop
// drains it at the end of the turn, so `p.then(f); after();` runs `after`
// first, the way a real job queue orders them.

[[nodiscard]] value make_promise(context & cx, value v, bool rejected);
void settle(context & cx, value promise, value with, bool rejected);

// A PROMISE THAT HAS NOT SETTLED: what `then` hands back, what `await`
// suspends on, what `new Promise` and the combinators settle later.
[[nodiscard]] value pending_promise(context & cx) {
    const value made = make_promise(cx, value::undefined(), false);
    static_cast<object_object *>(made.as_heap())
        ->define("__settled", value::boolean(false), attr_builtin);
    return made;
}

// RESOLVE AND REJECT FOR ONE PROMISE, as `new Promise(executor)` hands them to
// the executor and `Promise.withResolvers` hands them back.
//
// RETAINED, not merely captured: a `value` captured by a C++ lambda is
// invisible to the collector, and these two hold the only reference to the
// promise that outlives the call - a page keeps `resolve`, not the promise -
// so it was collected out from under them and a later resolve() settled
// freed memory, SILENTLY, because settle() checks is_object() and a recycled
// cell is usually not one. The cost was an async function that could suspend
// exactly ONCE. `native_object::retained` is a traced list that is not a
// property, so it costs no name and is not visible to
// Object.getOwnPropertyNames(resolve), which the `__promise` property that
// first fixed this was.
[[nodiscard]] std::pair<native_object *, native_object *> resolvers_for(context & cx,
                                                                        value promise) {
    auto * resolve_fn =
        cx.allocate<native_object>("resolve", [promise](context & inner, std::span<value> args) {
            settle(inner, promise, args.empty() ? value::undefined() : args[0], false);
            return value::undefined();
        });
    auto * reject_fn =
        cx.allocate<native_object>("reject", [promise](context & inner, std::span<value> args) {
            settle(inner, promise, args.empty() ? value::undefined() : args[0], true);
            return value::undefined();
        });
    resolve_fn->retained.push_back(promise);
    reject_fn->retained.push_back(promise);
    return {resolve_fn, reject_fn};
}

// Run one registered handler and settle the promise it produced.
void deliver(context & cx, value handler_record, value settled, bool rejected) {
    auto * record = static_cast<object_object *>(handler_record.as_heap());
    value * on_ok = record->find("ok");
    value * on_err = record->find("err");
    value * next = record->find("next");
    const value handler = rejected ? (on_err == nullptr ? value::undefined() : *on_err)
                                   : (on_ok == nullptr ? value::undefined() : *on_ok);
    // A RESUMPTION IS A PROMISE HANDLER. `await` registers the suspended frame
    // on the awaited promise's own handler list, so it queues and orders with
    // every `then` rather than being a second mechanism that races them.
    if (value * waiting = record->find("co"); waiting != nullptr) {
        cx.resume(*waiting, settled, rejected);
        return;
    }
    if (next == nullptr) { return; }
    // `finally` RUNS EITHER WAY AND CHANGES NOTHING. Its callback takes no
    // argument, its return value is ignored, and the outcome - value or
    // rejection - passes straight through to the next promise. It used to call
    // its callback the moment it was registered and hand back the SAME promise,
    // so it ran before the rejection it was supposed to follow and a chain
    // after it saw the wrong link.
    // A HANDLER THAT THROWS REJECTS THE NEXT PROMISE (27.2.5.4.1 step 9.a):
    // the call is fenced so the throw stops here instead of unwinding to
    // whatever page `try` happens to be below the microtask - or to nothing,
    // which was an engine fault. `finally`'s callback throwing overrides the
    // outcome the same way.
    bool threw = false;
    value thrown = value::undefined();
    if (value * on_finally = record->find("fin"); on_finally != nullptr) {
        if (on_finally->is_callable()) {
            (void)cx.call_fenced(*on_finally, std::span<const value>{}, value::undefined(), threw,
                                 thrown);
        }
        if (threw) {
            settle(cx, *next, thrown, true);
        } else {
            settle(cx, *next, settled, rejected);
        }
        return;
    }
    if (!handler.is_callable()) {
        // No handler for how this settled: it passes straight through, so a
        // rejection survives a bare `.then(f)` and a later `.catch` sees it.
        settle(cx, *next, settled, rejected);
        return;
    }
    const value args[1] = {settled};
    const value produced = cx.call_fenced(handler, args, value::undefined(), threw, thrown);
    if (threw) {
        settle(cx, *next, thrown, true);
        return;
    }
    // A handler returning a promise ADOPTS it, which is what makes a chain of
    // `then`s that each do async work run in order rather than all at once.
    if (produced.is_object()) {
        auto * inner = static_cast<object_object *>(produced.as_heap());
        if (inner->find("__settled") != nullptr) {
            value * inner_settled = inner->find("__settled");
            value * inner_value = inner->find("__value");
            value * inner_rejected = inner->find("__rejected");
            if (context::truthy(*inner_settled)) {
                settle(cx, *next, inner_value == nullptr ? value::undefined() : *inner_value,
                       inner_rejected != nullptr && context::truthy(*inner_rejected));
            } else {
                // still pending: chain onto it
                value * handlers = inner->find("__handlers");
                if (handlers != nullptr && handlers->is_array()) {
                    object_object * record2 = new_table(cx);
                    record2->set("next", *next);
                    static_cast<array_object *>(handlers->as_heap())
                        ->items.push_back(value::object(record2));
                }
            }
            return;
        }
    }
    settle(cx, *next, produced, false);
}

// THE JOB. Delivery is queued rather than run, and a job is a callable plus
// values so the collector traces it - so the C++ work has to be reachable
// through a value, which is what this native is. One per context, made on
// demand and remembered.
[[nodiscard]] value delivery_job(context & cx) {
    static const std::string slot = "__deliverJob";
    if (const value existing = cx.global(slot); existing.is_callable()) { return existing; }
    value made =
        value::object(cx.allocate<native_object>(slot, [](context & c, std::span<value> a) {
            if (a.size() >= 3 && a[0].is_object()) {
                deliver(c, a[0], a[1], context::truthy(a[2]));
            }
            return value::undefined();
        }));
    cx.define_global(slot, made);
    return made;
}

// Queue one delivery for the end of the turn.
void enqueue_delivery(context & cx, value record, value settled, bool rejected) {
    cx.queue_microtask(delivery_job(cx), {record, settled, value::boolean(rejected)});
}

void settle(context & cx, value promise, value with, bool rejected) {
    if (!promise.is_object()) { return; }
    auto * p = static_cast<object_object *>(promise.as_heap());
    value * already = p->find("__settled");
    if (already != nullptr && context::truthy(*already)) { return; } // settle once
    p->define("__value", with, attr_builtin);
    p->define("__rejected", value::boolean(rejected), attr_builtin);
    p->define("__settled", value::boolean(true), attr_builtin);
    value * handlers = p->find("__handlers");
    if (handlers == nullptr || !handlers->is_array()) { return; }
    // COPIED before draining: a handler may register another on this same
    // promise, and appending to the vector being walked invalidates it.
    const std::vector<value> pending = static_cast<array_object *>(handlers->as_heap())->items;
    static_cast<array_object *>(handlers->as_heap())->items.clear();
    // QUEUED, not called. `p.then(f); after();` must run `after` first, and a
    // handler that runs the instant a promise settles can also reenter code
    // that is halfway through its own work.
    for (const value & record : pending) { enqueue_delivery(cx, record, with, rejected); }
}

// `then`/`catch`/`finally` all reduce to: remember what to do for each way this
// can settle, and either do it now or when it settles.
//
// `on_finally` is the third form: one callback for BOTH outcomes, with no say
// in either - its return value is ignored and the outcome passes through. It
// goes in the same record so it queues and orders like everything else, rather
// than being a special case at the call site.
value settle_with(context & cx, value on_ok, value on_err, value on_finally = value::undefined()) {
    const value self = cx.current_this();
    if (!self.is_object()) { return self; }
    auto * promise = static_cast<object_object *>(self.as_heap());

    const value next = pending_promise(cx);
    object_object * record = new_table(cx);
    record->set("ok", on_ok);
    record->set("err", on_err);
    record->set("next", next);
    if (!on_finally.is_undefined()) { record->set("fin", on_finally); }

    value * settled = promise->find("__settled");
    if (settled != nullptr && context::truthy(*settled)) {
        value * held = promise->find("__value");
        value * state = promise->find("__rejected");
        // Already settled, so there is nothing to wait FOR - but the handler
        // still runs at the end of the turn rather than here. `Promise
        // .resolve(1).then(f); after();` orders them the same way as the
        // pending case, which is the whole point of a queue.
        enqueue_delivery(cx, value::object(record), held == nullptr ? value::undefined() : *held,
                         state != nullptr && context::truthy(*state));
        return next;
    }
    value * handlers = promise->find("__handlers");
    if (handlers != nullptr && handlers->is_array()) {
        static_cast<array_object *>(handlers->as_heap())->items.push_back(value::object(record));
    }
    return next;
}

// then/catch/finally, once for the program rather than three natives per
// promise. They were already receiver-based - settle_with reads current_this -
// so nothing about them was per-instance; and having them here is what makes `p
// instanceof Promise` answerable at all, since a promise then has a prototype to
// walk. Built lazily so a context with no promise ever made pays nothing.
[[nodiscard]] object_object * promise_prototype(context & cx) {
    if (object_object * existing = cx.prototype(context::proto_kind::promise)) { return existing; }
    object_object * table = new_table(cx);
    method(cx, table, "then", [](context & c, std::span<value> a) {
        return settle_with(c, a.empty() ? value::undefined() : a[0],
                           a.size() > 1 ? a[1] : value::undefined());
    });
    method(cx, table, "catch", [](context & c, std::span<value> a) {
        return settle_with(c, value::undefined(), a.empty() ? value::undefined() : a[0]);
    });
    method(cx, table, "finally", [](context & c, std::span<value> a) {
        return settle_with(c, value::undefined(), value::undefined(), arg_at(a, 0));
    });
    table->define("@@toStringTag", cx.string("Promise"), attr_configurable); // 27.2.5.5
    cx.set_prototype(context::proto_kind::promise, table);
    return table;
}

[[nodiscard]] value make_promise(context & cx, value v, bool rejected) {
    object_object * promise = new_table(cx);
    promise->prototype = value::object(promise_prototype(cx));
    promise->define("__value", v, attr_builtin);
    promise->define("__rejected", value::boolean(rejected), attr_builtin);
    promise->define("__settled", value::boolean(true), attr_builtin);
    promise->define("__handlers", cx.make_array(), attr_builtin);
    return value::object(promise);
}

} // namespace

} // namespace ctbrowser::script::detail

namespace ctbrowser::script::builtins_detail {

using detail::pending_promise;
using detail::resolvers_for;

namespace {

// ATTACH A REACTION THE WAY `then` ATTACHES ONE, and for the same reason it has
// to be the same way: a combinator is specified over arbitrary values, so all
// three of "already settled", "still pending" and "not a promise at all" have
// to reach the same code. `Promise.allSettled([p, 1])` must wait for `p` and
// must report the 1 as fulfilled.
//
// The reaction runs at the END OF THE TURN in every case, because that is what
// `settle` and `settle_with` do - a handler that runs the instant it is
// attached would order `Promise.allSettled([1]).then(f); after();` backwards.
void react(context & cx, value input, native_object * on_ok, native_object * on_err) {
    object_object * record = detail::new_table(cx);
    record->set("ok", value::object(on_ok));
    record->set("err", value::object(on_err));
    // `deliver` DROPS A RECORD WITH NO `next` before it calls anything: that
    // slot is the promise a `then` would have returned. Nothing reads this one,
    // and leaving it out silently loses the reaction.
    record->set("next", pending_promise(cx));

    if (input.is_object()) {
        auto * p = static_cast<object_object *>(input.as_heap());
        if (value * settled = p->find("__settled"); settled != nullptr) {
            if (context::truthy(*settled)) {
                const value * held = p->find("__value");
                const value * state = p->find("__rejected");
                detail::enqueue_delivery(cx, value::object(record),
                                         held == nullptr ? value::undefined() : *held,
                                         state != nullptr && context::truthy(*state));
                return;
            }
            if (value * handlers = p->find("__handlers");
                handlers != nullptr && handlers->is_array()) {
                static_cast<array_object *>(handlers->as_heap())
                    ->items.push_back(value::object(record));
                return;
            }
        }
    }
    // Not a promise: 27.2.4.7.1 resolves it with itself.
    detail::enqueue_delivery(cx, value::object(record), input, false);
}

// The argument, as the list of things to wait for.
//
// AN ARRAY, NOT AN ITERABLE. A general iterable needs Symbol.iterator dispatch,
// which is the gap `for..of` has here too (docs/script.md), and `Promise.all`
// has always read its argument this way. A non-array is an empty list rather
// than the TypeError the specification asks for, which is the same deviation
// and is named rather than fixed in passing.
[[nodiscard]] std::vector<value> entries_of(std::span<value> a) {
    if (!a.empty() && a[0].is_array()) {
        return static_cast<array_object *>(a[0].as_heap())->items;
    }
    return {};
}

// A COMBINATOR'S SHARED STATE, reachable rather than captured.
//
// A `value` held only by a C++ lambda is invisible to the collector - the
// comment on `new Promise`'s resolve/reject says what that cost - so the
// result promise, the results array and the counter live in one object that
// every reaction RETAINS.
[[nodiscard]] object_object * combinator_state(context & cx, value out, value results,
                                               std::size_t count) {
    object_object * state = detail::new_table(cx);
    state->set("out", out);
    state->set("results", results);
    state->set("left", value::number(static_cast<double>(count)));
    return state;
}

[[nodiscard]] double count_down(object_object * state) {
    const value * left = state->find("left");
    const double remaining = (left == nullptr ? 0.0 : left->as_number()) - 1.0;
    state->set("left", value::number(remaining));
    return remaining;
}

[[nodiscard]] value slot_of(object_object * state, const char * name) {
    const value * held = state->find(name);
    return held == nullptr ? value::undefined() : *held;
}

void put_result(object_object * state, std::size_t index, value entry) {
    const value * results = state->find("results");
    if (results == nullptr || !results->is_array()) { return; }
    auto & items = static_cast<array_object *>(results->as_heap())->items;
    if (index < items.size()) { items[index] = entry; }
}

// 27.2.4.1.2: every fulfilment writes its value into its own slot and the last
// one to arrive resolves the result with the whole array; ONE rejection settles
// it outright, and the reactions still queued after it find the result already
// settled - `settle` is settle-once, which is what makes that safe rather than
// needing a flag of its own.
[[nodiscard]] native_object * all_reaction(context & cx, value state, std::size_t index,
                                           bool rejected) {
    auto * made = cx.allocate<native_object>(
        rejected ? "rejected" : "fulfilled",
        [state, index, rejected](context & c, std::span<value> args) {
            auto * held = static_cast<object_object *>(state.as_heap());
            const value with = args.empty() ? value::undefined() : args[0];
            if (rejected) {
                detail::settle(c, slot_of(held, "out"), with, true);
                return value::undefined();
            }
            put_result(held, index, with);
            if (count_down(held) <= 0) {
                detail::settle(c, slot_of(held, "out"), slot_of(held, "results"), false);
            }
            return value::undefined();
        });
    made->retained.push_back(state);
    return made;
}

// 27.2.4.2.2: every reaction writes `{ status, value }` or
// `{ status, reason }` into its own slot, and the last one to finish resolves.
[[nodiscard]] native_object * allsettled_reaction(context & cx, value state, std::size_t index,
                                                  bool rejected) {
    auto * made = cx.allocate<native_object>(
        rejected ? "rejected" : "fulfilled",
        [state, index, rejected](context & c, std::span<value> args) {
            auto * held = static_cast<object_object *>(state.as_heap());
            const value entry = c.make_object();
            auto * record = static_cast<object_object *>(entry.as_heap());
            record->set("status", c.string(rejected ? "rejected" : "fulfilled"));
            record->set(rejected ? "reason" : "value", args.empty() ? value::undefined() : args[0]);
            put_result(held, index, entry);
            if (count_down(held) <= 0) {
                detail::settle(c, slot_of(held, "out"), slot_of(held, "results"), false);
            }
            return value::undefined();
        });
    made->retained.push_back(state);
    return made;
}

// THE ERROR `Promise.any` REJECTS WITH when every input rejected.
//
// AggregateError is a constructor this engine does not have (docs/test262.md
// names it as deliberately absent), so `make_error` falls back to
// Error.prototype plus an own `name` - which keeps `e.name === "AggregateError"`
// and `e instanceof Error` true. `errors` is the array 27.2.4.3.2 requires, in
// input order.
[[nodiscard]] value aggregate_error(context & cx, value errors) {
    const value made = cx.make_error("AggregateError", "All promises were rejected");
    static_cast<object_object *>(made.as_heap())->set("errors", errors);
    return made;
}

// 27.2.4.3.2, and it is `all` with the two outcomes swapped: the first
// FULFILMENT wins, and it takes every rejection to fail.
[[nodiscard]] native_object * any_reaction(context & cx, value state, std::size_t index,
                                           bool rejected) {
    auto * made = cx.allocate<native_object>(
        rejected ? "rejected" : "fulfilled",
        [state, index, rejected](context & c, std::span<value> args) {
            auto * held = static_cast<object_object *>(state.as_heap());
            const value with = args.empty() ? value::undefined() : args[0];
            if (!rejected) {
                detail::settle(c, slot_of(held, "out"), with, false);
                return value::undefined();
            }
            put_result(held, index, with);
            if (count_down(held) <= 0) {
                detail::settle(c, slot_of(held, "out"),
                               aggregate_error(c, slot_of(held, "results")), true);
            }
            return value::undefined();
        });
    made->retained.push_back(state);
    return made;
}

// 27.2.4.5.2: whichever settles first settles the result, however it settled.
[[nodiscard]] native_object * race_reaction(context & cx, value state, bool rejected) {
    auto * made = cx.allocate<native_object>(
        rejected ? "rejected" : "fulfilled", [state, rejected](context & c, std::span<value> args) {
            auto * held = static_cast<object_object *>(state.as_heap());
            detail::settle(c, slot_of(held, "out"), args.empty() ? value::undefined() : args[0],
                           rejected);
            return value::undefined();
        });
    made->retained.push_back(state);
    return made;
}

} // namespace

// Promise
void install_promise(context & cx) {
    using detail::method;
    using detail::new_table;
    cx.set_promise_factory(
        [](context & c, value v, bool rejected) { return detail::make_promise(c, v, rejected); });
    // What `await` needs to suspend: a promise that has not settled, and a way
    // to settle one. The VM can READ a promise - it always could - but making
    // and settling run this library's own logic, queue included.
    cx.set_pending_promise_factory(pending_promise);
    cx.set_promise_settler([](context & c, value promise, value with, bool rejected) {
        detail::settle(c, promise, with, rejected);
    });
    // GetIterator(obj, async) - see async_iterator_name. A sync iterator is
    // wrapped: its `next` answers a promise, and a promise in `value` is
    // awaited before the record is delivered (27.1.6.4 step 8-ish, the
    // AsyncFromSyncIteratorContinuation). `return`/`throw` forward the same way.
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
        if (async.is_callable()) { return c.call(async, {}, source); }
        if (!async.is_nullish()) {
            c.throw_error("TypeError", "[Symbol.asyncIterator] is not a function");
            return value::undefined();
        }
        const value sync = c.lookup_property(source, "@@iterator");
        if (c.throw_pending()) { return value::undefined(); }
        if (!sync.is_callable()) {
            c.throw_error("TypeError", "the value is not async iterable");
            return value::undefined();
        }
        const value inner = c.call(sync, {}, source);
        auto * wrapper = new_table(c);
        const auto forward = [&](const char * name) {
            auto * step = c.allocate<native_object>(name, [inner, name](context & cc,
                                                                        std::span<value> args) {
                const value out = pending_promise(cc);
                const value method = cc.lookup_property(inner, name);
                if (!method.is_callable()) {
                    // A sync iterator with no `return`/`throw`: done, or the
                    // reason rethrown - 27.1.6.2.2 step 7 / 27.1.6.2.3 step 8.
                    if (std::string_view{name} == "throw") {
                        detail::settle(cc, out, args.empty() ? value::undefined() : args[0], true);
                    } else {
                        auto * record = new_table(cc);
                        record->set("value", args.empty() ? value::undefined() : args[0]);
                        record->set("done", value::boolean(true));
                        detail::settle(cc, out, value::object(record), false);
                    }
                    return out;
                }
                const value result = cc.call(method, args, inner);
                if (cc.failed()) { return out; }
                if (!result.is_object()) {
                    detail::settle(cc, out,
                                   cc.make_error("TypeError", "iterator result is not an object"),
                                   true);
                    return out;
                }
                const value done = cc.lookup_property(result, "done");
                const value item = cc.lookup_property(result, "value");
                auto * state = new_table(cc);
                state->set("out", out);
                state->set("done", value::boolean(context::truthy(done)));
                const auto reaction = [&](bool rejected) {
                    auto * made = cc.allocate<native_object>(
                        rejected ? "rejected" : "fulfilled",
                        [state, rejected](context & c3, std::span<value> got) {
                            const value settled = got.empty() ? value::undefined() : got[0];
                            if (rejected) {
                                detail::settle(c3, slot_of(state, "out"), settled, true);
                                return value::undefined();
                            }
                            auto * record = new_table(c3);
                            record->set("value", settled);
                            record->set("done", slot_of(state, "done"));
                            detail::settle(c3, slot_of(state, "out"), value::object(record), false);
                            return value::undefined();
                        });
                    made->retained.push_back(value::object(state));
                    return made;
                };
                react(cc, item, reaction(false), reaction(true));
                return out;
            });
            step->retained.push_back(inner);
            wrapper->define(name, value::object(step), attr_builtin);
        };
        forward("next");
        forward("return");
        forward("throw");
        return value::object(wrapper);
    });
    // `Promise.reject` under the compiler's name for it: what an async body's
    // fence returns for a throw it did not catch. See promise_reject_name.
    cx.define_native(std::string{promise_reject_name}, [](context & c, std::span<value> a) {
        return detail::make_promise(c, a.empty() ? value::undefined() : a[0], true);
    });
    // `new Promise(executor)`. The executor runs IMMEDIATELY and is handed
    // resolve and reject; a promise it does not settle stays pending until
    // something later calls one of them. That is the whole of what was missing,
    // and p5.js opens with it:
    //
    //   new Promise((resolve) => {
    //     if (document.readyState === 'complete') { resolve(); }
    //     else { window.addEventListener('load', resolve, false); }
    //   })
    //
    // Callable AND a namespace: the statics are installed on it directly with
    // detail::method, so each is { writable, enumerable: FALSE, configurable }
    // as clause 27.2.4 has them (a copy through `set` made them enumerable).
    auto * promise_new = cx.allocate<native_object>("Promise", [](context & c, std::span<value> a) {
        const value promise = pending_promise(c);
        if (a.empty() || !a[0].is_callable()) { return promise; }
        const auto [resolve_fn, reject_fn] = resolvers_for(c, promise);
        const value args[2] = {value::object(resolve_fn), value::object(reject_fn)};
        (void)c.call(a[0], args);
        return promise;
    });
    method(cx, promise_new, "resolve", 1, [](context & c, std::span<value> a) {
        return detail::make_promise(c, a.empty() ? value::undefined() : a[0], false);
    });
    method(cx, promise_new, "reject", 1, [](context & c, std::span<value> a) {
        return detail::make_promise(c, a.empty() ? value::undefined() : a[0], true);
    });
    // `Promise.all` - 27.2.4.1. The first rejection wins; otherwise the result
    // is an array of the values in input order.
    //
    // IT WAITS, and it did not. This read `__value` off each entry as it walked
    // the array, which answers immediately - and wrongly - for an input that has
    // not settled: `Promise.all([d.promise]).then(f)` ran `f` with `[undefined]`
    // in the same turn and then never ran it again when `d.resolve` arrived. It
    // goes through `react` now, the same path `then` takes and the one the other
    // three combinators were rewritten onto, so "already settled", "still
    // pending" and "not a promise at all" all reach one implementation.
    //
    // WHAT THAT CHANGES FOR A PAGE, said plainly: a `Promise.all` over an input
    // that never settles no longer resolves. p5.js opens with
    // `Promise.all([waitForDocumentReady(), waitingForTranslator]).then(_globalInit)`
    // (vendor/p5/p5.js:138934); the first of those resolves at once here because
    // `document.readyState` is "complete", and the second is i18next's `init`,
    // whose backend fetches a CDN URL. If that promise never settles headless
    // then p5 never boots, where before it booted on a wrong answer - so the p5
    // ratchet is the thing to watch on this change.
    method(cx, promise_new, "all", 1, [](context & c, std::span<value> a) {
        const std::vector<value> entries = entries_of(a);
        const value out = pending_promise(c);
        const value results = c.make_array();
        static_cast<array_object *>(results.as_heap())
            ->items.assign(entries.size(), value::undefined());
        if (entries.empty()) {
            detail::settle(c, out, results, false);
            return out;
        }
        const value state = value::object(combinator_state(c, out, results, entries.size()));
        for (std::size_t i = 0; i < entries.size(); ++i) {
            react(c, entries[i], all_reaction(c, state, i, false), all_reaction(c, state, i, true));
        }
        return out;
    });
    // `Promise.allSettled` - 27.2.4.2. It never rejects: every input's outcome
    // is reported, in input order, as `{ status: "fulfilled", value }` or
    // `{ status: "rejected", reason }`.
    //
    // LIKE `all` ABOVE, this one waits: all four go through `react`, which is
    // the same path `then` takes. `all` was the last one that did not.
    method(cx, promise_new, "allSettled", 1, [](context & c, std::span<value> a) {
        const std::vector<value> entries = entries_of(a);
        const value out = pending_promise(c);
        const value results = c.make_array();
        static_cast<array_object *>(results.as_heap())
            ->items.assign(entries.size(), value::undefined());
        if (entries.empty()) {
            detail::settle(c, out, results, false);
            return out;
        }
        const value state = value::object(combinator_state(c, out, results, entries.size()));
        for (std::size_t i = 0; i < entries.size(); ++i) {
            react(c, entries[i], allsettled_reaction(c, state, i, false),
                  allsettled_reaction(c, state, i, true));
        }
        return out;
    });
    // `Promise.any` - 27.2.4.3. The first FULFILMENT wins; if every input
    // rejects it rejects with an AggregateError carrying `errors` in order.
    method(cx, promise_new, "any", 1, [](context & c, std::span<value> a) {
        const std::vector<value> entries = entries_of(a);
        const value out = pending_promise(c);
        const value errors = c.make_array();
        static_cast<array_object *>(errors.as_heap())
            ->items.assign(entries.size(), value::undefined());
        if (entries.empty()) {
            // 27.2.4.3.1 step 5: an empty list is already "all rejected".
            detail::settle(c, out, aggregate_error(c, errors), true);
            return out;
        }
        const value state = value::object(combinator_state(c, out, errors, entries.size()));
        for (std::size_t i = 0; i < entries.size(); ++i) {
            react(c, entries[i], any_reaction(c, state, i, false), any_reaction(c, state, i, true));
        }
        return out;
    });
    // `Promise.race` - 27.2.4.5. An EMPTY list stays pending forever, which is
    // the specified answer and not an oversight.
    method(cx, promise_new, "race", 1, [](context & c, std::span<value> a) {
        const std::vector<value> entries = entries_of(a);
        const value out = pending_promise(c);
        const value state = value::object(combinator_state(c, out, value::undefined(), 0));
        for (const value & entry : entries) {
            react(c, entry, race_reaction(c, state, false), race_reaction(c, state, true));
        }
        return out;
    });
    // `Promise.withResolvers` - 27.2.4.8, and the reason this file was opened:
    // WPT's `url-import-referrer-policy.html` fails on exactly this name. It is
    // `new Promise(executor)` turned inside out - the same promise and the same
    // two functions, handed back as an object instead of to a callback, so a
    // page does not have to smuggle them out of the executor's scope.
    method(cx, promise_new, "withResolvers", 0, [](context & c, std::span<value>) {
        const value promise = pending_promise(c);
        const auto [resolve_fn, reject_fn] = resolvers_for(c, promise);
        const value out = c.make_object();
        auto * fields = static_cast<object_object *>(out.as_heap());
        fields->set("promise", promise);
        fields->set("resolve", value::object(resolve_fn));
        fields->set("reject", value::object(reject_fn));
        return out;
    });
    detail::constant(promise_new, "prototype", value::object(detail::promise_prototype(cx)));
    cx.define_global("Promise", value::object(promise_new));

    cx.define_native("isNaN", [](context &, std::span<value> a) {
        return value::boolean(std::isnan(num_at(a, 0)));
    });
    cx.define_native("isFinite", [](context &, std::span<value> a) {
        return value::boolean(std::isfinite(num_at(a, 0)));
    });
    // Their own `name` and `length` (19.2.2, 19.2.3, both of arity 1).
    // define_native allocates a bare native, so `length` was absent and `name`
    // came from context::own_property's synthesised fallback - which cannot
    // refuse a write or be deleted, and which verifyProperty reports as two
    // failures at once.
    const auto slots = [&](const char * name, double arity) {
        const value fn = cx.global(name);
        if (!fn.is_kind(heap_kind::native)) { return; }
        auto * made = static_cast<native_object *>(fn.as_heap());
        made->is_constructor = false; // a global function, clause 19
        made->define("length", value::number(arity), attr_configurable);
        made->define("name", cx.string(name), attr_configurable);
    };
    slots("isNaN", 1);
    slots("isFinite", 1);
    slots("eval", 1);
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
