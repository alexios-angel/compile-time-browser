// ctbrowser.script builtins - explicit resource management's OBJECTS (ES2026
// clauses 20.5.7 SuppressedError, 27.4 DisposableStack and 27.5
// AsyncDisposableStack): the `using` and `await using` declarations that drive
// them are the compiler's.
//
// New on 2026-09-12. A stack holds its resources as [value, method, async]
// triples in a private-keyed array; DisposeResources (7.5.7) walks them in
// reverse, wrapping a second throw in a SuppressedError around the first. The
// async form is the same walk as a promise chain: every awaited step parks the
// walk on a reaction and resumes from it.

#include "iterator_internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

constexpr std::string_view stack_slot = "@#DisposableResourceStack";
constexpr std::string_view disposed_slot = "@#DisposableState";
constexpr std::string_view async_slot = "@#AsyncDisposableStack";

[[nodiscard]] value slot(object_object * o, std::string_view name) {
    const value * held = o->find(name);
    return held == nullptr ? value::undefined() : *held;
}

// RequireInternalSlot for either stack kind: the receiver's own table, or
// null with the TypeError in flight.
[[nodiscard]] object_object * stack_of(context & cx, bool async, const char * method) {
    const value self = cx.current_this();
    if (self.is_object()) {
        auto * o = static_cast<object_object *>(self.as_heap());
        if (o->find(stack_slot) != nullptr && (o->find(async_slot) != nullptr) == async) {
            return o;
        }
    }
    cx.throw_error("TypeError", std::string{method} + " called on an incompatible receiver");
    return nullptr;
}
// ...and not yet disposed: a ReferenceError otherwise (27.4.3.x step 3).
[[nodiscard]] object_object * live_stack_of(context & cx, bool async, const char * method) {
    object_object * o = stack_of(cx, async, method);
    if (o == nullptr) { return nullptr; }
    if (context::truthy(slot(o, disposed_slot))) {
        cx.throw_error("ReferenceError", std::string{method} + ": the stack is already disposed");
        return nullptr;
    }
    return o;
}

// GetDisposeMethod, 7.5.5: @@dispose for a sync hint; @@asyncDispose, then
// @@dispose wrapped so its result is NOT awaited, for an async one. False is
// a throw in flight; `out` undefined means neither was there.
[[nodiscard]] bool dispose_method(context & cx, value v, bool async, value & out) {
    out = value::undefined();
    if (async) {
        out = cx.lookup_property(v, "@@asyncDispose");
        if (cx.throw_pending()) { return false; }
        if (!out.is_nullish()) {
            if (!out.is_callable()) {
                cx.throw_error("TypeError", "[Symbol.asyncDispose] is not a function");
                return false;
            }
            return true;
        }
    }
    const value sync = cx.lookup_property(v, "@@dispose");
    if (cx.throw_pending()) { return false; }
    if (sync.is_nullish()) {
        out = value::undefined();
        return true;
    }
    if (!sync.is_callable()) {
        cx.throw_error("TypeError", "[Symbol.dispose] is not a function");
        return false;
    }
    if (!async) {
        out = sync;
        return true;
    }
    // 7.5.5 step 1.c.ii: a closure calling the sync method and answering a
    // promise of undefined - the sync method's own result is dropped.
    auto * wrapper = cx.allocate<native_object>("", [sync](context & c, std::span<value>) {
        const value promise = c.make_pending_promise();
        const context::rooted keep{c, promise};
        bool threw = false;
        value thrown = value::undefined();
        (void)c.call_fenced(sync, {}, c.current_this(), threw, thrown);
        c.settle_promise(promise, threw ? thrown : value::undefined(), threw);
        return promise;
    });
    wrapper->is_constructor = false;
    wrapper->retained.push_back(sync);
    out = value::object(wrapper);
    return true;
}

// AddDisposableResource, 7.5.3, onto the stack's list. A `method` given is
// adopt/defer's closure; otherwise it is looked up on `v`, and a null or
// undefined `v` adds nothing to a sync stack and an await-only marker to an
// async one.
[[nodiscard]] bool add_resource(context & cx, object_object * stack, value v, bool async,
                                value method) {
    if (method.is_undefined()) {
        if (v.is_nullish()) {
            if (!async) { return true; }
            v = value::undefined();
        } else {
            if (!v.is_object_like()) {
                cx.throw_error("TypeError", "a disposable resource must be an object");
                return false;
            }
            if (!dispose_method(cx, v, async, method)) { return false; }
            if (method.is_undefined()) {
                cx.throw_error("TypeError", async ? "the object is not async disposable"
                                                  : "the object is not disposable");
                return false;
            }
        }
    } else if (!method.is_callable()) {
        cx.throw_error("TypeError", "dispose method is not a function");
        return false;
    }
    const value entry = cx.make_array();
    static_cast<array_object *>(entry.as_heap())->items = {v, method, value::boolean(async)};
    static_cast<array_object *>(slot(stack, stack_slot).as_heap())->items.push_back(entry);
    return true;
}

// The SuppressedError of 7.5.7 step 2.c.iii.1: `error` is the newer throw,
// `suppressed` the one it displaced. Built through the intrinsic constructor,
// which install_disposable retains on the stack prototypes.
[[nodiscard]] value suppressed_error(context & cx, value ctor, value error, value suppressed) {
    const value args[2] = {error, suppressed};
    const value made = cx.construct(ctor, args);
    return made.is_object() ? made : error;
}

// DisposeResources, 7.5.7, SYNC: reverse order, each method called on its
// value, a throw over an earlier throw wrapped. False with the throw in
// flight - thrown at the end, once, so every resource ran first.
[[nodiscard]] bool dispose_all(context & cx, object_object * stack, value suppressed_ctor) {
    const value list = slot(stack, stack_slot);
    const std::vector<value> resources = static_cast<array_object *>(list.as_heap())->items;
    static_cast<array_object *>(list.as_heap())->items.clear();
    const context::rooted_values keep{cx, resources};
    bool threw = false;
    value thrown = value::undefined();
    for (std::size_t i = resources.size(); i-- > 0;) {
        const auto & entry = static_cast<array_object *>(resources[i].as_heap())->items;
        if (!entry[1].is_callable()) { continue; }
        bool failed = false;
        value error = value::undefined();
        const context::rooted keep_thrown{cx, thrown};
        (void)cx.call_fenced(entry[1], {}, entry[0], failed, error);
        if (!failed) { continue; }
        thrown = threw ? suppressed_error(cx, suppressed_ctor, error, thrown) : error;
        threw = true;
    }
    if (threw) { cx.throw_value(thrown); }
    return !threw;
}

// DisposeResources, 7.5.7, ASYNC: the same walk, but a step whose result
// must be awaited parks the walk on a promise reaction and the reaction
// resumes it. State lives in one object every reaction retains. The `await
// undefined` steps (2.b, 3) keep a sync-disposed resource's callbacks and
// an async one's ordered the way the specification's own microtask count
// orders them.
[[nodiscard]] value dispose_step(context & cx, value state_value);

[[nodiscard]] value resume_fn(context & cx, value state_value, bool rejected) {
    auto * fn =
        cx.allocate<native_object>("", [state_value, rejected](context & c, std::span<value> a) {
            auto * state = static_cast<object_object *>(state_value.as_heap());
            if (rejected) {
                const value error = arg_at(a, 0);
                const bool had = context::truthy(slot(state, "threw"));
                state->set("thrown", had ? suppressed_error(c, slot(state, "SuppressedError"),
                                                            error, slot(state, "thrown"))
                                         : error);
                state->set("threw", value::boolean(true));
            }
            return dispose_step(c, state_value);
        });
    fn->is_constructor = false;
    fn->retained.push_back(state_value);
    return value::object(fn);
}

// Await(v): PromiseResolve(%Promise%, v) then the two resumptions - the
// engine's own factory is PromiseResolve, and PerformPromiseThen is
// async.cpp's.
void await_then_step(context & cx, value state_value, value awaited) {
    const value wrapper = cx.make_promise(awaited, false);
    const context::rooted keep{cx, wrapper};
    const value on_ok = resume_fn(cx, state_value, false);
    const context::rooted keep_ok{cx, on_ok};
    const value on_err = resume_fn(cx, state_value, true);
    const context::rooted keep_err{cx, on_err};
    detail::perform_promise_then(cx, wrapper, on_ok, on_err);
}

[[nodiscard]] value dispose_step(context & cx, value state_value) {
    auto * state = static_cast<object_object *>(state_value.as_heap());
    const value list = slot(state, "stack");
    auto * resources = static_cast<array_object *>(list.as_heap());
    for (;;) {
        const double at = slot(state, "index").as_number();
        if (at < 0) {
            if (context::truthy(slot(state, "needsAwait")) &&
                !context::truthy(slot(state, "hasAwaited"))) {
                state->set("needsAwait", value::boolean(false));
                state->set("hasAwaited", value::boolean(true));
                await_then_step(cx, state_value, value::undefined());
                return value::undefined();
            }
            resources->items.clear();
            const value promise = slot(state, "promise");
            const bool threw = context::truthy(slot(state, "threw"));
            cx.settle_promise(promise, threw ? slot(state, "thrown") : value::undefined(), threw);
            return value::undefined();
        }
        const auto & entry =
            static_cast<array_object *>(resources->items[static_cast<std::size_t>(at)].as_heap())
                ->items;
        const bool async = context::truthy(entry[2]);
        if (!async && context::truthy(slot(state, "needsAwait")) &&
            !context::truthy(slot(state, "hasAwaited"))) {
            // 2.b: one `await undefined` before a sync resource that follows
            // an un-awaited one; the resource itself is taken up after it.
            state->set("needsAwait", value::boolean(false));
            state->set("hasAwaited", value::boolean(true));
            await_then_step(cx, state_value, value::undefined());
            return value::undefined();
        }
        state->set("index", value::number(at - 1));
        if (!entry[1].is_callable()) {
            state->set("needsAwait", value::boolean(true)); // 2.d: a null `await using`
            continue;
        }
        bool failed = false;
        value error = value::undefined();
        const value result = cx.call_fenced(entry[1], {}, entry[0], failed, error);
        if (failed) {
            const bool had = context::truthy(slot(state, "threw"));
            state->set("thrown", had ? suppressed_error(cx, slot(state, "SuppressedError"), error,
                                                        slot(state, "thrown"))
                                     : error);
            state->set("threw", value::boolean(true));
            continue;
        }
        if (async) {
            state->set("hasAwaited", value::boolean(true));
            await_then_step(cx, state_value, result);
            return value::undefined();
        }
    }
}

} // namespace

void install_disposable(context & cx) {
    using detail::method;
    using detail::new_table;

    // --- SuppressedError, 20.5.7 -------------------------------------------
    // A NativeError in shape: [[Prototype]] %Error%, prototype under
    // %Error.prototype% with its own `name` and `message`, and three
    // arguments - (error, suppressed, message) - installed as own
    // non-enumerable properties, `message` only when given.
    object_object * suppressed_proto = new_table(cx);
    if (object_object * error_proto = cx.prototype(context::proto_kind::error)) {
        suppressed_proto->prototype = value::object(error_proto);
    }
    suppressed_proto->define("name", cx.string("SuppressedError"), attr_builtin);
    suppressed_proto->define("message", cx.string(""), attr_builtin);
    auto * suppressed_ctor = cx.allocate<native_object>(
        "SuppressedError", [suppressed_proto](context & c, std::span<value> a) {
            value self = c.current_this();
            if (!detail::constructing_this(self)) { self = c.make_object(); }
            auto * made = static_cast<object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = value::object(suppressed_proto); }
            if (!arg_at(a, 2).is_undefined()) {
                // ToString: a symbol's refusal LANDS, an object's toString parks.
                if (!stringable_arg(c, a[2])) { return value::undefined(); }
                const std::string text = c.to_string(a[2]);
                if (c.throw_pending()) { return value::undefined(); }
                made->define("message", c.string(text), attr_builtin);
            }
            made->define("error", arg_at(a, 0), attr_builtin);
            made->define("suppressed", arg_at(a, 1), attr_builtin);
            made->define(
                "@#ErrorData",
                c.string("SuppressedError" +
                         (arg_at(a, 2).is_undefined() ? std::string{} : ": " + c.to_string(a[2])) +
                         c.current_stack()),
                attr_none);
            return self;
        });
    link_constructor(cx, suppressed_proto, "SuppressedError", 3, value::object(suppressed_ctor));
    suppressed_ctor->define("prototype", value::object(suppressed_proto), attr_none);
    if (const value error_ctor = cx.global("Error"); error_ctor.is_callable()) {
        suppressed_ctor->proto_link = error_ctor;
    }
    cx.register_error_prototype("SuppressedError", suppressed_proto);
    cx.define_global("SuppressedError", value::object(suppressed_ctor));
    const value suppressed_value = value::object(suppressed_ctor);

    // --- DisposableStack, 27.4, and AsyncDisposableStack, 27.5 ------------
    // One installer for both: the two differ in the hint their `use` looks a
    // method up with, and in `dispose` being a promise-returning
    // `disposeAsync`.
    const auto define_stack = [&](bool async) {
        const char * name = async ? "AsyncDisposableStack" : "DisposableStack";
        object_object * proto = new_table(cx);
        proto->define("@#SuppressedError", suppressed_value, attr_none);
        auto * ctor =
            cx.allocate<native_object>(name, [proto, async](context & c, std::span<value>) {
                const value self = c.current_this();
                if (!detail::constructing_this(self)) {
                    c.throw_error("TypeError",
                                  std::string{"Constructor "} +
                                      (async ? "AsyncDisposableStack" : "DisposableStack") +
                                      " requires 'new'");
                    return value::undefined();
                }
                auto * made = static_cast<object_object *>(self.as_heap());
                if (!made->prototype.is_object()) { made->prototype = value::object(proto); }
                made->define(stack_slot, c.make_array(), attr_none);
                made->define(disposed_slot, value::boolean(false), attr_none);
                if (async) { made->define(async_slot, value::boolean(true), attr_none); }
                return self;
            });
        const std::string prefix = std::string{name} + ".prototype.";
        // 27.4.3.3 get disposed
        {
            const std::string label = prefix + "disposed";
            auto * getter = detail::method_native(
                cx, "get disposed", [async, label](context & c, std::span<value>) {
                    object_object * o = stack_of(c, async, label.c_str());
                    if (o == nullptr) { return value::undefined(); }
                    return value::boolean(context::truthy(slot(o, disposed_slot)));
                });
            detail::install_arity(cx, getter, 0);
            proto->define_accessor("disposed", value::object(getter), value::undefined(),
                                   attr_configurable);
        }
        // 27.4.3.6 use(value)
        method(cx, proto, "use", 1, [async, prefix](context & c, std::span<value> a) {
            const std::string label = prefix + "use";
            object_object * o = live_stack_of(c, async, label.c_str());
            if (o == nullptr) { return value::undefined(); }
            if (!add_resource(c, o, arg_at(a, 0), async, value::undefined())) {
                return value::undefined();
            }
            return arg_at(a, 0);
        });
        // 27.4.3.1 adopt(value, onDispose): a closure calling onDispose with
        // the value, registered under an undefined resource value.
        method(cx, proto, "adopt", 2, [async, prefix](context & c, std::span<value> a) {
            const std::string label = prefix + "adopt";
            object_object * o = live_stack_of(c, async, label.c_str());
            if (o == nullptr) { return value::undefined(); }
            const value on_dispose = arg_at(a, 1);
            if (!on_dispose.is_callable()) {
                c.throw_error("TypeError", label + ": onDispose is not a function");
                return value::undefined();
            }
            const value held = arg_at(a, 0);
            auto * closure =
                c.allocate<native_object>("", [held, on_dispose](context & cc, std::span<value>) {
                    const value args[1] = {held};
                    return cc.call(on_dispose, args);
                });
            closure->is_constructor = false;
            closure->retained.push_back(held);
            closure->retained.push_back(on_dispose);
            if (!add_resource(c, o, value::undefined(), async, value::object(closure))) {
                return value::undefined();
            }
            return held;
        });
        // 27.4.3.2 defer(onDispose)
        method(cx, proto, "defer", 1, [async, prefix](context & c, std::span<value> a) {
            const std::string label = prefix + "defer";
            object_object * o = live_stack_of(c, async, label.c_str());
            if (o == nullptr) { return value::undefined(); }
            if (!arg_at(a, 0).is_callable()) {
                c.throw_error("TypeError", label + ": onDispose is not a function");
                return value::undefined();
            }
            (void)add_resource(c, o, value::undefined(), async, a[0]);
            return value::undefined();
        });
        // 27.4.3.5 move(): the resources go to a fresh stack on the same
        // prototype and this one is left disposed and empty.
        method(cx, proto, "move", 0, [async, prefix, proto](context & c, std::span<value>) {
            const std::string label = prefix + "move";
            object_object * o = live_stack_of(c, async, label.c_str());
            if (o == nullptr) { return value::undefined(); }
            object_object * fresh = new_table(c);
            fresh->prototype = value::object(proto);
            fresh->define(stack_slot, slot(o, stack_slot), attr_none);
            fresh->define(disposed_slot, value::boolean(false), attr_none);
            if (async) { fresh->define(async_slot, value::boolean(true), attr_none); }
            o->define(stack_slot, c.make_array(), attr_none);
            o->define(disposed_slot, value::boolean(true), attr_none);
            return value::object(fresh);
        });
        if (!async) {
            // 27.4.3.4 dispose(), also installed as @@dispose (27.4.3.7).
            auto * dispose = detail::method_native(
                cx, "dispose", [prefix, suppressed_value](context & c, std::span<value>) {
                    const std::string label = prefix + "dispose";
                    object_object * o = stack_of(c, false, label.c_str());
                    if (o == nullptr) { return value::undefined(); }
                    if (context::truthy(slot(o, disposed_slot))) { return value::undefined(); }
                    o->define(disposed_slot, value::boolean(true), attr_none);
                    (void)dispose_all(c, o, suppressed_value);
                    return value::undefined();
                });
            detail::install_arity(cx, dispose, 0);
            proto->define("dispose", value::object(dispose), attr_builtin);
            proto->define("@@dispose", value::object(dispose), attr_builtin);
        } else {
            // 27.5.3.3 disposeAsync(), also @@asyncDispose (27.5.3.8): a
            // promise, rejected with the receiver's TypeError rather than
            // throwing it.
            auto * dispose = detail::method_native(
                cx, "disposeAsync", [prefix, suppressed_value](context & c, std::span<value>) {
                    const value promise = c.make_pending_promise();
                    const context::rooted keep{c, promise};
                    const value self = c.current_this();
                    object_object * o = nullptr;
                    if (self.is_object()) {
                        auto * table = static_cast<object_object *>(self.as_heap());
                        if (table->find(stack_slot) != nullptr &&
                            table->find(async_slot) != nullptr) {
                            o = table;
                        }
                    }
                    if (o == nullptr) {
                        c.settle_promise(promise,
                                         c.make_error("TypeError", prefix +
                                                                       "disposeAsync called on "
                                                                       "an incompatible receiver"),
                                         true);
                        return promise;
                    }
                    if (context::truthy(slot(o, disposed_slot))) {
                        c.settle_promise(promise, value::undefined(), false);
                        return promise;
                    }
                    o->define(disposed_slot, value::boolean(true), attr_none);
                    object_object * state = new_table(c);
                    state->set("stack", slot(o, stack_slot));
                    state->set("index",
                               value::number(static_cast<double>(static_cast<array_object *>(
                                                                     slot(o, stack_slot).as_heap())
                                                                     ->items.size()) -
                                             1.0));
                    state->set("needsAwait", value::boolean(false));
                    state->set("hasAwaited", value::boolean(false));
                    state->set("threw", value::boolean(false));
                    state->set("thrown", value::undefined());
                    state->set("promise", promise);
                    state->set("SuppressedError", suppressed_value);
                    // The stack keeps a fresh, empty list from here (7.5.7
                    // step 5 happens at the end; nothing can observe it
                    // earlier because the stack is already disposed).
                    const context::rooted keep_state{c, value::object(state)};
                    (void)dispose_step(c, value::object(state));
                    return promise;
                });
            detail::install_arity(cx, dispose, 0);
            proto->define("disposeAsync", value::object(dispose), attr_builtin);
            proto->define("@@asyncDispose", value::object(dispose), attr_builtin);
        }
        proto->define("@@toStringTag", cx.string(name), attr_configurable);
        detail::constant(ctor, "prototype", value::object(proto));
        link_constructor(cx, proto, name, 0, value::object(ctor));
        cx.define_global(name, value::object(ctor));
    };
    define_stack(false);
    define_stack(true);
}

} // namespace ctbrowser::script::builtins_detail
