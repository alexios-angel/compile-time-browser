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

#include "async/promise_reactions.hpp"

namespace ctbrowser::script::builtins_detail {

using detail::promise::capability;
using detail::promise::is_promise;
using detail::promise::pending_promise;
using detail::promise::resolvers_for;
using detail::promise::slot;

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
                detail::promise::settle_capability(c, cap, aggregate_error(c, values), true);
            } else if (keyed(kind)) {
                detail::promise::settle_capability(c, cap, keyed_result(c, slot(s, "keys"), values),
                                                   false);
            } else {
                detail::promise::settle_capability(c, cap, values, false);
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
    if (!detail::promise::new_capability(cx, ctor, cap)) { return value::undefined(); }
    detail::promise::materialise(cx, cap);
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
                        detail::promise::settle_capability(c, cap, aggregate_error(c, values),
                                                           true);
                    } else if (keyed(kind)) {
                        detail::promise::settle_capability(c, cap, keyed_result(c, keys, values),
                                                           false);
                    } else {
                        detail::promise::settle_capability(c, cap, values, false);
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
        detail::promise::settle_capability(cx, cap, parts.result, true);
        return cap.promise;
    }
    const auto & pair = static_cast<array_object *>(parts.result.as_heap())->items;
    const bool done = pair[0].as_boolean();
    const value item = pair[1];
    value wrapper = value::undefined();
    {
        const detail::completion wrapped = detail::fenced(cx, [&](context & c) -> value {
            value out = value::undefined();
            (void)detail::promise::promise_resolve(c, detail::promise::intrinsic_promise(c), item,
                                                   out);
            return out;
        });
        if (wrapped.threw) {
            if (!done && close_on_rejection) { detail::iterator_close_quietly(cx, sync_iterator); }
            detail::promise::settle_capability(cx, cap, wrapped.result, true);
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
    detail::promise::perform_then(cx, wrapper, value::object(unwrap), on_rejected, cap);
    return cap.promise;
}

// %AsyncIteratorPrototype%, 27.1.3: one method, @@asyncIterator answering
// `this`. %AsyncGeneratorPrototype% and %AsyncFromSyncIteratorPrototype% both
// inherit from it. Kept on Promise.prototype under a private key.
constexpr std::string_view async_iterator_key = "@#AsyncIteratorPrototype";
[[nodiscard]] object_object * async_iterator_prototype(context & cx) {
    object_object * promise_proto = detail::promise::promise_prototype(cx);
    if (const value * held = promise_proto->find(async_iterator_key); held != nullptr) {
        return static_cast<object_object *>(held->as_heap());
    }
    object_object * table = detail::new_table(cx);
    promise_proto->define(async_iterator_key, value::object(table), attr_none);
    {
        auto * self =
            detail::method_native(cx, "[Symbol.asyncIterator]",
                                  [](context & c, std::span<value>) { return c.current_this(); });
        detail::install_arity(cx, self, 0);
        table->define("@@asyncIterator", value::object(self), attr_builtin);
    }
    // %AsyncIteratorPrototype%[@@asyncDispose] (explicit resource management,
    // 27.1.3.2): a promise of undefined once the iterator's `return`, if it
    // has one, has been called and its answer awaited; a throw from either
    // step is the rejection.
    {
        auto * dispose =
            detail::method_native(cx, "[Symbol.asyncDispose]", [](context & c, std::span<value>) {
                capability cap;
                if (!detail::promise::new_capability(c, detail::promise::intrinsic_promise(c),
                                                     cap)) {
                    return value::undefined();
                }
                const context::rooted keep{c, cap.promise};
                const value self = c.current_this();
                const detail::completion got = detail::fenced(c, [&](context & cc) -> value {
                    const value back = cc.lookup_property(self, "return");
                    if (cc.throw_pending()) { return value::undefined(); }
                    if (back.is_nullish()) { return value::undefined(); }
                    if (!back.is_callable()) {
                        cc.throw_error("TypeError", "iterator.return is not a function");
                        return value::undefined();
                    }
                    const value result = cc.call(back, {}, self);
                    if (cc.throw_pending()) { return value::undefined(); }
                    value wrapped = value::undefined();
                    (void)detail::promise::promise_resolve(
                        cc, detail::promise::intrinsic_promise(cc), result, wrapped);
                    return wrapped;
                });
                if (got.threw) {
                    detail::promise::settle_capability(c, cap, got.result, true);
                    return cap.promise;
                }
                if (!got.result.is_object_like()) { // no `return`: undefined, at once
                    detail::promise::settle_capability(c, cap, value::undefined(), false);
                    return cap.promise;
                }
                auto * unwrap = c.allocate<native_object>(
                    "", [](context &, std::span<value>) { return value::undefined(); });
                unwrap->is_constructor = false;
                detail::install_arity(c, unwrap, 1);
                const context::rooted keep_unwrap{c, value::object(unwrap)};
                detail::promise::perform_then(c, got.result, value::object(unwrap),
                                              value::undefined(), cap);
                return cap.promise;
            });
        detail::install_arity(cx, dispose, 0);
        table->define("@@asyncDispose", value::object(dispose), attr_builtin);
    }
    return table;
}

[[nodiscard]] object_object * async_from_sync_prototype(context & cx) {
    object_object * promise_proto = detail::promise::promise_prototype(cx);
    if (const value * held = promise_proto->find(detail::promise::async_from_sync_key);
        held != nullptr) {
        return static_cast<object_object *>(held->as_heap());
    }
    object_object * table = detail::new_table(cx);
    promise_proto->define(detail::promise::async_from_sync_key, value::object(table), attr_none);
    table->prototype = value::object(async_iterator_prototype(cx));
    // The receiver's sync iterator, or the promise rejected with a TypeError.
    const auto open = [](context & c, capability & cap, value & sync, value & next) {
        if (!detail::promise::new_capability(c, detail::promise::intrinsic_promise(c), cap)) {
            return false;
        }
        const value self = c.current_this();
        if (self.is_object()) {
            auto * o = static_cast<object_object *>(self.as_heap());
            sync = slot(o, sync_iterator_slot);
            next = slot(o, sync_next_slot);
            if (sync.is_object_like()) { return true; }
        }
        detail::promise::settle_capability(
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
            detail::promise::settle_capability(c, cap, result.result, true);
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
                detail::promise::settle_capability(c, cap, result.result, true);
                return cap.promise;
            }
            if (absent) {
                if (returning) {
                    detail::promise::settle_capability(c, cap, c.iter_result(sent, true), false);
                } else {
                    detail::promise::settle_capability(
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
    object_object * proto = detail::promise::promise_prototype(cx);
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
            detail::promise::settle(c, made, v, true);
            return made;
        }
        value out = value::undefined();
        const context::rooted keep{c, v};
        if (!detail::promise::promise_resolve(c, detail::promise::intrinsic_promise(c), v, out)) {
            // The `constructor` read threw: a promise rejected with it is
            // the only answer a factory with no completion channel can give.
            out = pending_promise(c);
        }
        return out;
    });
    cx.set_pending_promise_factory(pending_promise);
    cx.set_promise_settler([](context & c, value promise, value with, bool rejected) {
        if (rejected) {
            detail::promise::settle(c, promise, with, true);
        } else {
            detail::promise::resolve_promise(c, promise, with);
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
        detail::promise::settle(c, made, arg_at(a, 0), true);
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
        const value promise = detail::promise::init_promise(c, self);
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
    proto->define(detail::promise::intrinsic_key, value::object(promise_new), attr_none);
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
        if (!detail::promise::promise_resolve(c, ctor, arg_at(a, 0), out)) {
            return value::undefined();
        }
        return out;
    });
    method(cx, promise_new, "reject", 1, [](context & c, std::span<value> a) {
        capability cap;
        if (!detail::promise::new_capability(c, c.current_this(), cap)) {
            return value::undefined();
        }
        const context::rooted keep{c, cap.promise};
        detail::promise::settle_capability(c, cap, arg_at(a, 0), true);
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
        if (!detail::promise::new_capability(c, ctor, cap)) { return value::undefined(); }
        const context::rooted keep{c, cap.promise};
        bool threw = false;
        value thrown = value::undefined();
        const std::span<const value> rest =
            a.size() > 1 ? std::span<const value>{a}.subspan(1) : std::span<const value>{};
        const value produced = c.call_fenced(arg_at(a, 0), rest, value::undefined(), threw, thrown);
        detail::promise::settle_capability(c, cap, threw ? thrown : produced, threw);
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
        if (!detail::promise::new_capability(c, c.current_this(), cap)) {
            return value::undefined();
        }
        const context::rooted keep{c, cap.promise};
        detail::promise::materialise(c, cap);
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
