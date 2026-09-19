// Split from function.cpp: destructuring.
#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

// See iterator_open_name: the three natives an array pattern is compiled
// around. The record is an ordinary object so the collector sees it through
// the register that holds it; `done` starts true and is cleared only by a
// step that produced a value, which is exactly 7.4.8's rule that a throwing
// or malformed `next()` marks the record done and forbids a close.
namespace {
[[nodiscard]] value slot(object_object * record, const char * name) {
    const value * found = record->find(name);
    return found != nullptr ? *found : value::undefined();
}
} // namespace

// `using` / `await using` (9.13 DisposableResource, DisposeResources): the
// five natives compile/statements/using.cpp calls. The stack is a plain
// table - `__resources` an array of {value, method, async, sync_fallback}
// records, `__seeded` once the completion in flight has been read,
// `__threw` / `__error` the completion being folded - so the collector sees
// everything in it. Disposal errors fold as 9.13.4 step 1.a.iii says: a
// SuppressedError whose `error` is the new throw and whose `suppressed` is
// the completion so far.
namespace {

[[nodiscard]] value suppressed_error(context & c, value error, value suppressed) {
    const value made = c.make_error("SuppressedError", "An error was suppressed during disposal");
    auto * o = static_cast<object_object *>(made.as_heap());
    o->define("error", error, attr_builtin);
    o->define("suppressed", suppressed, attr_builtin);
    return made;
}

void fold_disposal_error(context & c, object_object * stack, value error) {
    const bool threw = context::truthy(slot(stack, "__threw"));
    stack->set("__error", threw ? suppressed_error(c, error, slot(stack, "__error")) : error);
    stack->set("__threw", value::boolean(true));
}

// The completion in flight, read once: kind 1 is a throw of `thrown`.
void seed_completion(object_object * stack, value kind, value thrown) {
    if (context::truthy(slot(stack, "__seeded"))) { return; }
    stack->set("__seeded", value::boolean(true));
    if (kind.is_number() && kind.as_number() == 1.0) {
        stack->set("__threw", value::boolean(true));
        stack->set("__error", thrown);
    }
}

// Pop the top resource and call its dispose method; answers what the
// caller should await (the method's result for an async resource, undefined
// for a sync one or after a throw), and whether there was a resource at all.
[[nodiscard]] bool dispose_top(context & c, object_object * stack, value & awaited) {
    awaited = value::undefined();
    const value list = slot(stack, "__resources");
    if (!list.is_array()) { return false; }
    auto * items = static_cast<array_object *>(list.as_heap());
    if (items->items.empty()) { return false; }
    const value record = items->items.back();
    items->items.pop_back();
    if (!record.is_object()) { return true; }
    auto * r = static_cast<object_object *>(record.as_heap());
    const value method = slot(r, "method");
    const value receiver = slot(r, "value");
    const std::size_t before = c.unwinds();
    const value result = c.call(method, {}, receiver);
    if (c.throw_pending()) {
        fold_disposal_error(c, stack, c.take_pending_throw());
        return true;
    }
    if (c.unwinds() != before || c.failed()) { return true; }
    if (context::truthy(slot(r, "async")) && !context::truthy(slot(r, "sync_fallback"))) {
        awaited = result;
    }
    return true;
}

void throw_folded(context & c, object_object * stack) {
    if (!context::truthy(slot(stack, "__threw"))) { return; }
    const value error = slot(stack, "__error");
    stack->set("__threw", value::boolean(false));
    stack->set("__error", value::undefined());
    c.throw_value(error);
}

void install_using(context & cx) {
    cx.define_native(std::string{using_stack_name}, [](context & c, std::span<value>) {
        auto * stack = detail::new_table(c);
        stack->set("__resources", c.make_array());
        stack->set("__seeded", value::boolean(false));
        stack->set("__threw", value::boolean(false));
        stack->set("__error", value::undefined());
        return value::object(stack);
    });
    // AddDisposableResource (9.13.1) with CreateDisposableResource (9.13.2)
    // and GetDisposeMethod (9.13.3): null and undefined register nothing; a
    // non-object is a TypeError; an `await using` asks for @@asyncDispose
    // first and falls back to @@dispose, whose result is then NOT awaited
    // (the fallback closure of 9.13.3 step 1.b.ii returns undefined).
    cx.define_native(std::string{using_add_name}, [](context & c, std::span<value> a) {
        if (a.size() < 2 || !a[0].is_object()) { return value::undefined(); }
        auto * stack = static_cast<object_object *>(a[0].as_heap());
        const value v = a[1];
        const bool async = a.size() > 2 && context::truthy(a[2]);
        if (v.is_nullish()) { return v; }
        if (!v.is_object()) {
            c.throw_error("TypeError",
                          "`using` needs an object, not " + std::string{context::type_of(v)});
            return value::undefined();
        }
        value method = value::undefined();
        bool sync_fallback = false;
        if (async) {
            method = c.lookup_property(v, "@@asyncDispose");
            if (c.throw_pending()) { return value::undefined(); }
            if (method.is_nullish()) {
                method = c.lookup_property(v, "@@dispose");
                if (c.throw_pending()) { return value::undefined(); }
                sync_fallback = true;
            }
        } else {
            method = c.lookup_property(v, "@@dispose");
            if (c.throw_pending()) { return value::undefined(); }
        }
        if (!method.is_callable()) {
            c.throw_error("TypeError", std::string{"the value of a `"} + (async ? "await " : "") +
                                           "using` declaration has no " +
                                           (async ? "[Symbol.asyncDispose] or " : "") +
                                           "[Symbol.dispose] method");
            return value::undefined();
        }
        auto * record = detail::new_table(c);
        record->set("value", v);
        record->set("method", method);
        record->set("async", value::boolean(async));
        record->set("sync_fallback", value::boolean(sync_fallback));
        const value list = slot(stack, "__resources");
        if (list.is_array()) {
            static_cast<array_object *>(list.as_heap())->items.push_back(value::object(record));
        }
        return v;
    });
    // DisposeResources (9.13.4) for a sync stack: every resource in reverse,
    // the completion folded, and thrown when it is a throw.
    cx.define_native(std::string{using_dispose_name}, [](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_object()) { return value::undefined(); }
        auto * stack = static_cast<object_object *>(a[0].as_heap());
        seed_completion(stack, a.size() > 1 ? a[1] : value::undefined(),
                        a.size() > 2 ? a[2] : value::undefined());
        value ignored;
        while (dispose_top(c, stack, ignored)) {
            if (c.failed()) { return value::undefined(); }
        }
        throw_folded(c, stack);
        return value::undefined();
    });
    // One step of an async stack: the next resource's result to await, or
    // the stack itself when none is left - after throwing what was folded.
    cx.define_native(std::string{using_step_name}, [](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_object()) { return value::undefined(); }
        auto * stack = static_cast<object_object *>(a[0].as_heap());
        seed_completion(stack, a.size() > 1 ? a[1] : value::undefined(),
                        a.size() > 2 ? a[2] : value::undefined());
        value awaited;
        if (dispose_top(c, stack, awaited)) { return awaited; }
        throw_folded(c, stack);
        return a[0];
    });
    cx.define_native(std::string{using_failed_name}, [](context & c, std::span<value> a) {
        if (a.size() < 2 || !a[0].is_object()) { return value::undefined(); }
        fold_disposal_error(c, static_cast<object_object *>(a[0].as_heap()), a[1]);
        return value::undefined();
    });
}

} // namespace

void install_destructuring_iteration(context & cx) {
    cx.define_native(std::string{iterator_open_name}, [](context & c, std::span<value> a) {
        const value iterator = c.get_iterator(a.empty() ? value::undefined() : a[0]);
        auto * record = detail::new_table(c);
        record->set("iterator", iterator);
        record->set("next", iterator.is_object() ? c.lookup_property(iterator, "next")
                                                 : value::undefined());
        record->set("done", value::boolean(!iterator.is_object()));
        return value::object(record);
    });
    cx.define_native(std::string{for_of_open_name}, [](context & c, std::span<value> a) {
        const value v = a.empty() ? value::undefined() : a[0];
        // See for_of_open_name: the fast set is what iterable_values walks
        // without the protocol, plus the array-like leniency the index loop
        // has always had (ctcompile's escape oracle pins an inherited `0`
        // getter firing under `for (x of {length: 1})`).
        bool fast = v.is_array() || v.is_string() || v.is_kind(heap_kind::proxy);
        if (!fast && v.is_object()) {
            auto * obj = static_cast<object_object *>(v.as_heap());
            fast = obj->find("__entries") != nullptr || obj->find("__items") != nullptr;
            if (!fast && !c.lookup_property(v, "@@iterator").is_callable()) {
                const value * length = obj->find("length");
                fast = length != nullptr && length->is_number();
            }
        }
        if (fast) { return value::undefined(); }
        const value iterator = c.get_iterator(v);
        auto * record = detail::new_table(c);
        record->set("iterator", iterator);
        record->set("next", iterator.is_object() ? c.lookup_property(iterator, "next")
                                                 : value::undefined());
        record->set("done", value::boolean(!iterator.is_object()));
        return value::object(record);
    });
    cx.define_native(std::string{iterator_next_name}, [](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_object()) { return value::undefined(); }
        auto * record = static_cast<object_object *>(a[0].as_heap());
        if (context::truthy(slot(record, "done"))) { return value::undefined(); }
        record->set("done", value::boolean(true));
        bool done = true;
        const value item = c.iterator_step(slot(record, "iterator"), slot(record, "next"), done);
        if (!done && !c.throw_pending()) { record->set("done", value::boolean(false)); }
        return item;
    });
    cx.define_native(std::string{array_holes_name}, [](context &, std::span<value> a) {
        if (a.empty() || !a[0].is_array()) { return value::undefined(); }
        auto * arr = static_cast<array_object *>(a[0].as_heap());
        for (std::size_t i = 1; i < a.size(); ++i) {
            if (!a[i].is_number()) { continue; }
            const auto at = static_cast<std::uint32_t>(a[i].as_number());
            if (at < arr->items.size()) { arr->set_element_attrs(at, array_object::elem_hole); }
        }
        return value::undefined();
    });
    // See import_defer_name.
    cx.define_native(std::string{import_defer_name}, [](context & c, std::span<value> a) {
        return c.deferred_module_namespace_for(a.empty() ? std::string{} : c.to_string(a[0]));
    });
    cx.define_native(std::string{catch_filter_name}, [](context & c, std::span<value> a) {
        if (!a.empty() && c.is_return_marker(a[0])) { c.throw_value(a[0]); }
        return value::undefined();
    });
    cx.define_native(std::string{require_object_name}, [](context & c, std::span<value> a) {
        const value v = a.empty() ? value::undefined() : a[0];
        if (v.is_nullish()) {
            c.throw_error("TypeError", "Cannot destructure '" + c.to_string(v) + "' as it is " +
                                           std::string{context::type_of(v)} + ".");
        }
        return value::undefined();
    });
    install_using(cx);
    cx.define_native(std::string{define_own_name}, [](context & c, std::span<value> a) {
        if (a.size() < 3 || !a[0].is_object_like()) { return value::undefined(); }
        context::property_descriptor wanted;
        wanted.has_value = wanted.has_writable = wanted.has_enumerable = true;
        wanted.has_configurable = true;
        wanted.held = a[2];
        wanted.writable = wanted.configurable = true;
        wanted.enumerable = a.size() > 3 && context::truthy(a[3]);
        if (!c.define_own_property(a[0], c.to_string(a[1]), wanted)) {
            c.throw_error("TypeError", "Cannot redefine property: " + c.to_string(a[1]));
        }
        return value::undefined();
    });
    // See class_heritage_name.
    cx.define_native(std::string{class_heritage_name}, [](context & c, std::span<value> a) {
        if (a.size() < 3 || !a[0].is_kind(heap_kind::function)) { return value::undefined(); }
        auto * ctor = static_cast<closure_object *>(a[0].as_heap());
        const value parent = a[1];
        if (parent.is_null()) {
            // `extends null`: the prototype has no [[Prototype]], the
            // constructor is still an ordinary function object.
            c.set_prototype(a[2], value::undefined());
            return value::undefined();
        }
        if (!is_constructor(parent)) {
            c.throw_error("TypeError", "Class extends value " + c.to_string(parent) +
                                           " is not a constructor or null");
            return value::undefined();
        }
        const value proto_parent = c.lookup_property(parent, "prototype");
        if (c.throw_pending()) { return value::undefined(); }
        if (!proto_parent.is_object_like() && !proto_parent.is_null()) {
            c.throw_error("TypeError",
                          "Class extends value does not have valid prototype property " +
                              c.to_string(proto_parent));
            return value::undefined();
        }
        c.set_prototype(a[2], proto_parent.is_null() ? value::undefined() : proto_parent);
        ctor->proto_link = parent;
        return value::undefined();
    });
    // See super_get_name.
    cx.define_native(std::string{super_get_name}, [](context & c, std::span<value> a) {
        if (a.size() < 3) { return value::undefined(); }
        if (a[0].is_nullish()) {
            c.throw_error("TypeError", "Cannot read properties of " +
                                           std::string{a[0].is_null() ? "null" : "undefined"} +
                                           " (reading '" + c.to_string(a[1]) + "')");
            return value::undefined();
        }
        const std::string key = c.to_string(a[1]);
        if (c.throw_pending()) { return value::undefined(); }
        return c.get_with_receiver(a[0], key, a[2]);
    });
    // See private_add_name.
    cx.define_native(std::string{private_add_name}, [](context & c, std::span<value> a) {
        if (a.size() < 2 || !a[0].is_object_like() || a[0].is_kind(heap_kind::proxy)) {
            c.throw_error("TypeError", "a private element can only be added to an object");
            return value::undefined();
        }
        const std::string key = c.to_string(a[1]);
        const std::size_t colon = key.find(':');
        const std::string shown =
            key.substr(1, colon == std::string::npos ? key.size() : colon - 1);
        if (c.has_own_property(a[0], key)) {
            c.throw_error("TypeError",
                          shown.size() > 1
                              ? "Cannot initialize " + shown + " twice on the same object"
                              : "Cannot initialize private methods of a class twice on "
                                "the same object");
            return value::undefined();
        }
        if (!c.is_extensible(a[0])) {
            c.throw_error("TypeError", "Cannot define private elements on a non-extensible object");
            return value::undefined();
        }
        context::property_descriptor wanted;
        wanted.has_value = wanted.has_writable = wanted.has_enumerable = true;
        wanted.has_configurable = true;
        wanted.held = a.size() > 2 ? a[2] : value::undefined();
        wanted.writable = wanted.configurable = true;
        wanted.enumerable = false;
        if (!c.define_own_property(a[0], key, wanted)) {
            c.throw_error("TypeError", "Cannot define private element " + shown);
        }
        return value::undefined();
    });
    // See strict_assign_check_name.
    cx.define_native(std::string{strict_assign_check_name}, [](context & c, std::span<value> a) {
        const std::string name = a.empty() ? std::string{} : c.to_string(a[0]);
        if (c.has_global(name)) { return value::undefined(); }
        const value global = c.global_this();
        if (global.is_heap() && c.has_property(global, c.string(name))) {
            return value::undefined();
        }
        c.throw_error("ReferenceError", name + " is not defined");
        return value::undefined();
    });
    // See template_object_name. The cache is a plain object RETAINED by the
    // native (native_object::retained is traced), so the collector sees every
    // array in it; the lambda holds the raw pointer that retention keeps alive.
    {
        object_object * cache = detail::new_table(cx);
        auto * native = cx.allocate<native_object>(
            std::string{template_object_name}, [cache](context & c, std::span<value> a) {
                if (a.size() < 3 || !a[1].is_array() || !a[2].is_array()) {
                    return value::undefined();
                }
                const std::string key = c.to_string(a[0]);
                if (value * cached = cache->find(key)) { return *cached; }
                // 13.2.8.4 steps 10-14: `raw` frozen on the cooked array, both
                // frozen, and the site remembers the result.
                auto * cooked = static_cast<array_object *>(a[1].as_heap());
                auto * raw = static_cast<array_object *>(a[2].as_heap());
                for (array_object * arr : {raw, cooked}) {
                    arr->extensible = false;
                    arr->elements_writable = false;
                    arr->elements_configurable = false;
                    arr->length_writable = false;
                }
                cooked->named_table().define("raw", a[2], attr_none);
                cache->set(key, a[1]);
                return a[1];
            });
        native->retained.push_back(value::object(cache));
        cx.define_global(std::string{template_object_name}, value::object(native));
    }
    cx.define_native(std::string{define_accessor_name}, [](context & c, std::span<value> a) {
        // A CLASS IS A CLOSURE: `static get [k]()` defines on the constructor,
        // which is_object() (heap_kind::object exactly) does not admit - so
        // every static computed accessor was silently dropped.
        if (a.size() < 4 || !(a[0].is_object() || a[0].is_kind(heap_kind::function))) {
            return value::undefined();
        }
        // ToPropertyKey (7.1.19) of the computed key: a symbol keeps its key,
        // anything else goes through ToPrimitive-then-ToString, whose throw
        // is the throw of the class definition.
        const std::string key = c.to_string(a[1]);
        if (c.throw_pending()) { return value::undefined(); }
        c.define_accessor(a[0], key, a[2], a[3]);
        return value::undefined();
    });
    // See yield_delegate_open_name.
    cx.define_native(std::string{yield_delegate_open_name}, [](context & c, std::span<value> a) {
        const value source = a.empty() ? value::undefined() : a[0];
        const bool async = a.size() > 1 && context::truthy(a[1]);
        value iterator = value::undefined();
        if (async) {
            // 7.4.3 GetIterator(obj, async): GetMethod(@@asyncIterator), the
            // sync method only when that is absent, and a method's result
            // that is not an object is the TypeError - before any `next()`.
            if (source.is_nullish()) {
                c.throw_error("TypeError", "the value is not async iterable");
                return value::undefined();
            }
            const value method = c.lookup_property(source, "@@asyncIterator");
            if (c.throw_pending()) { return value::undefined(); }
            if (!method.is_nullish()) {
                if (!method.is_callable()) {
                    c.throw_error("TypeError", "[Symbol.asyncIterator] is not a function");
                    return value::undefined();
                }
                iterator = c.call(method, {}, source);
                if (c.throw_pending()) { return value::undefined(); }
                if (!iterator.is_object()) {
                    c.throw_error("TypeError",
                                  "Result of the Symbol.asyncIterator method is not an object");
                    return value::undefined();
                }
            } else {
                const value sync = c.lookup_property(source, "@@iterator");
                if (c.throw_pending()) { return value::undefined(); }
                if (!sync.is_callable()) {
                    c.throw_error("TypeError", "the value is not async iterable");
                    return value::undefined();
                }
                const value inner = c.call(sync, {}, source);
                if (c.throw_pending()) { return value::undefined(); }
                if (!inner.is_object()) {
                    c.throw_error("TypeError",
                                  "Result of the Symbol.iterator method is not an object");
                    return value::undefined();
                }
                // CreateAsyncFromSyncIterator (27.1.6.1), through the shared
                // native: it is handed an iterable whose @@iterator answers
                // the sync iterator already made.
                auto * iterable = detail::new_table(c);
                auto * answer = c.allocate<native_object>(
                    "[Symbol.iterator]", [inner](context &, std::span<value>) { return inner; });
                answer->retained.push_back(inner);
                iterable->set("@@iterator", value::object(answer));
                const value wrapped = value::object(iterable);
                iterator = c.call(c.global(std::string{async_iterator_name}), {&wrapped, 1});
            }
        } else {
            iterator = c.get_iterator(source);
        }
        auto * record = detail::new_table(c);
        record->set("iterator", iterator);
        record->set("next", iterator.is_object() ? c.lookup_property(iterator, "next")
                                                 : value::undefined());
        record->set("done", value::boolean(!iterator.is_object()));
        record->set("value", value::undefined());
        return value::object(record);
    });
    cx.define_native(std::string{yield_delegate_call_name}, [](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_object()) { return value::undefined(); }
        auto * record = static_cast<object_object *>(a[0].as_heap());
        if (context::truthy(slot(record, "done"))) { return value::undefined(); }
        const value next = slot(record, "next");
        if (!next.is_callable()) {
            record->set("done", value::boolean(true));
            c.throw_error("TypeError", "iterator.next is not a function");
            return value::undefined();
        }
        const value sent = a.size() > 1 ? a[1] : value::undefined();
        // AN ASYNC GENERATOR'S `.throw(e)` / `.return(v)` ARRIVING AT THE
        // `yield*` (see resume_record_key): the inner iterator's own method
        // gets it (14.4.14 steps 7.b and 7.c). No `throw`: the inner
        // iterator is closed and the protocol violation is a TypeError at
        // the yield. No `return`: the generator returns v itself - the
        // marker every finally on the way out sees.
        if (sent.is_object()) {
            auto * table = static_cast<object_object *>(sent.as_heap());
            if (const value * how = table->find(resume_record_key); how != nullptr) {
                const std::string method_name = c.to_string(*how);
                const value * held = table->find(resume_record_value_key);
                const value v = held != nullptr ? *held : value::undefined();
                const value iterator = slot(record, "iterator");
                const value method = c.lookup_property(iterator, method_name);
                if (c.throw_pending()) { return value::undefined(); }
                if (method.is_nullish()) {
                    record->set("done", value::boolean(true));
                    if (method_name == "return") {
                        c.throw_value(c.make_return_marker(v));
                        return value::undefined();
                    }
                    if (const value close = c.lookup_property(iterator, "return");
                        close.is_callable()) {
                        (void)c.call(close, {}, iterator);
                        if (c.throw_pending()) { return value::undefined(); }
                    }
                    c.throw_error("TypeError", "The iterator does not provide a 'throw' method");
                    return value::undefined();
                }
                if (!method.is_callable()) {
                    record->set("done", value::boolean(true));
                    c.throw_error("TypeError", "iterator." + method_name + " is not a function");
                    return value::undefined();
                }
                record->set("resume", *how);
                return c.call(method, {&v, 1}, iterator);
            }
        }
        return c.call(next, {&sent, 1}, slot(record, "iterator"));
    });
    cx.define_native(std::string{yield_delegate_settle_name}, [](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_object()) { return value::undefined(); }
        auto * record = static_cast<object_object *>(a[0].as_heap());
        context::coroutine_object * co = c.current_generator();
        if (context::truthy(slot(record, "done"))) {
            if (co != nullptr) { co->delegate = value::undefined(); }
            return value::undefined();
        }
        const value result = a.size() > 1 ? a[1] : value::undefined();
        if (!result.is_object()) {
            record->set("done", value::boolean(true));
            if (co != nullptr) { co->delegate = value::undefined(); }
            c.throw_error("TypeError", "Iterator result is not an object");
            return value::undefined();
        }
        const value forwarded = slot(record, "resume");
        record->set("resume", value::undefined());
        if (context::truthy(c.lookup_property(result, "done"))) {
            record->set("done", value::boolean(true));
            record->set("value", c.lookup_property(result, "value"));
            if (co != nullptr) { co->delegate = value::undefined(); }
            // The inner iterator answered a forwarded `return` with done:
            // the generator returns that value (14.4.14 step 7.c.viii),
            // through its finally blocks.
            if (forwarded.is_string() && c.to_string(forwarded) == "return") {
                c.throw_value(c.make_return_marker(slot(record, "value")));
            }
            return value::undefined();
        }
        if (co != nullptr) { co->delegate = a[0]; }
        return result;
    });
    cx.define_native(std::string{iterator_close_name}, [](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_object()) { return value::undefined(); }
        auto * record = static_cast<object_object *>(a[0].as_heap());
        if (context::truthy(slot(record, "done"))) { return value::undefined(); }
        record->set("done", value::boolean(true));
        const bool suppress = a.size() > 1 && context::truthy(a[1]);
        const value iterator = slot(record, "iterator");
        // 7.4.10 IteratorClose: GetMethod(iterator, "return") - undefined and
        // null mean nothing to do - then Call; with a throw already in flight
        // the original wins over anything `return()` does.
        const value back = c.lookup_property(iterator, "return");
        if (back.is_nullish() || (suppress && c.throw_pending())) { return value::undefined(); }
        if (!back.is_callable()) {
            if (!suppress) { c.throw_error("TypeError", "iterator.return is not a function"); }
            return value::undefined();
        }
        if (suppress) {
            bool threw = false;
            value thrown = value::undefined();
            (void)c.call_fenced(back, {}, iterator, threw, thrown);
            return value::undefined();
        }
        const value result = c.call(back, {}, iterator);
        if (!c.throw_pending() && !result.is_object()) {
            c.throw_error("TypeError", "iterator.return() did not return an object");
        }
        return value::undefined();
    });
}
} // namespace ctbrowser::script::builtins_detail
