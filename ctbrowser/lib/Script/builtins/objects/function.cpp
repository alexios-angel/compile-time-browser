// ctbrowser.script builtins - Function.prototype, the dynamic Function
// constructor, and the generator prototype.
//
// One of five files carved out of a 1,429-line builtins/objects.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp; what only this
// directory shares is in internal.hpp beside this.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

// `Function.prototype`. 84 `.call(`, 78 `.apply(` and 26 `.bind(` in p5.js -
// and it cannot install one event listener without bind:
// `window.addEventListener(e, this['_on' + e].bind(this), {...})`.
void install_function(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * function_proto = new_table(cx);

    // IsCallable(this) IS STEP 1 of all three (20.2.3.1/20.2.3.3/20.2.3.2), and
    // all three answered `undefined` instead - so `({}).call()` reached through
    // Function.prototype was a silent no-op and a feature probe written as
    // `try { f.bind(o) } catch (e)` saw nothing.
    const auto this_function = [](context & c, const char * method) {
        const value self = c.current_this();
        if (self.is_callable()) { return self; }
        c.throw_error("TypeError", std::string{"Function.prototype."} + method +
                                       " called on incompatible receiver");
        return value::undefined();
    };
    method(cx, function_proto, "call", 1, [this_function](context & c, std::span<value> a) {
        const value self = this_function(c, "call");
        if (!self.is_callable()) { return value::undefined(); }
        const std::vector<value> rest(a.begin() + (a.empty() ? 0 : 1), a.end());
        return c.call(self, rest, arg_at(a, 0));
    });
    method(cx, function_proto, "apply", 2, [this_function](context & c, std::span<value> a) {
        const value self = this_function(c, "apply");
        if (!self.is_callable()) { return value::undefined(); }
        // CreateListFromArrayLike, 7.3.18, which this did not do: it read
        // `items` off a real Array and passed NO arguments for anything else,
        // so `f.apply(o, arguments)` and `f.apply(o, {length: 1, 0: x})` both
        // called `f()`. A null or undefined argArray is an empty list (step 3);
        // anything else that is not an object is a TypeError (step 2).
        const value list = arg_at(a, 1);
        std::vector<value> args;
        if (list.is_array()) {
            // A REAL ARRAY IS ITS ELEMENTS, unchanged: `items` is already the
            // list and its size is bounded by what was actually allocated, so
            // `Math.max.apply(null, twoHundredThousand)` keeps working.
            args = static_cast<array_object *>(list.as_heap())->items;
        } else if (!list.is_undefined() && !list.is_null()) {
            if (!list.is_object_like()) {
                c.throw_error("TypeError", "CreateListFromArrayLike called on non-object");
                return value::undefined();
            }
            // AN ARRAY-LIKE CLAIMS ITS LENGTH, and ToLength lets it claim
            // 2^53-1: `f.apply(o, {length: 1e15})` would ask for a petabyte of
            // arguments for a call the register file cannot make anyway. A
            // ceiling that THROWS is the honest form of that limit, the same
            // shape `max_string_length` takes for `repeat`.
            const double count = detail::array_like_length(c, list);
            if (count > 65536.0) {
                c.throw_error("RangeError", "too many arguments to Function.prototype.apply");
                return value::undefined();
            }
            args.reserve(static_cast<std::size_t>(count));
            for (double i = 0; i < count; ++i) {
                args.push_back(c.lookup_index(list, value::number(i)));
            }
        }
        const context::rooted_values keep{c, args};
        return c.call(self, args, arg_at(a, 0));
    });
    method(cx, function_proto, "bind", 1, [this_function](context & c, std::span<value> a) {
        const value self = this_function(c, "bind");
        if (!self.is_callable()) { return value::undefined(); }
        const value receiver = arg_at(a, 0);
        // The arguments bound NOW are prepended to the ones supplied later,
        // which is what makes `f.bind(o, 1)` a partial application rather than
        // just a receiver change.
        const auto bound =
            std::make_shared<std::vector<value>>(a.begin() + (a.empty() ? 0 : 1), a.end());
        auto * fn = detail::cx_native(
            c, "bound", [self, receiver, bound](context & inner, std::span<value> later) {
                std::vector<value> args = *bound;
                args.insert(args.end(), later.begin(), later.end());
                return inner.call(self, args, receiver);
            });
        // THREE VALUES IN A C++ CAPTURE, AND THE COLLECTOR COULD SEE NONE.
        //
        // A bound function is often the ONLY reference to its target: the
        // ordinary `return target.bind(null, arg)` out of a factory drops both
        // the target and the argument on the way out. The capture is not a
        // root, so a collection freed them and the next call ran `inner.call`
        // on a freed closure_object - a heap-use-after-free reproduced under
        // the asan preset, reading the target's kind byte out of poisoned
        // memory.
        //
        // A SNAPSHOT IS EXACT HERE, unlike a registry that grows: `*bound` is
        // fixed at bind time and neither it nor `self` and `receiver` can
        // change afterwards.
        fn->retained.push_back(self);
        fn->retained.push_back(receiver);
        fn->retained.insert(fn->retained.end(), bound->begin(), bound->end());
        // 20.2.3.2: a bound function's `length` is the target's less the
        // arguments already supplied, floored at zero, and its `name` is
        // "bound " prefixed to the target's - both { false, false, true }. It
        // had neither, so `f.bind(o).length` was undefined and `.name` was the
        // synthesised "bound", which is a different string from the one every
        // engine gives.
        // ...and it is the target's OWN `length`, and only when that is a
        // Number (20.2.3.2 step 7). It was read through the prototype chain, so
        // a target with no own `length` inherited Function.prototype's and a
        // `length` that was a string was coerced instead of ignored. An absent
        // or non-numeric one means zero, which is what step 6 initialises L to.
        double left = 0.0;
        if (c.has_own_property(self, "length")) {
            const value target_length = c.lookup_property(self, "length");
            if (target_length.is_number()) {
                left = to_length(target_length.as_number()) - static_cast<double>(bound->size());
            }
        }
        fn->define("length", value::number(std::max(0.0, left)), attr_configurable);
        const value target_name = c.lookup_property(self, "name");
        fn->define("name",
                   c.string("bound " +
                            (target_name.is_string() ? c.to_string(target_name) : std::string{})),
                   attr_configurable);
        return value::object(fn);
    });
    // The TODO that stood here - "return the REAL source" - is DONE, and the
    // body below is what does it: `function_proto` carries the span and
    // `program::source` keeps the bytes. `context::to_string` reaches this now
    // too, so `String(f)` and `f.toString()` are one answer rather than two.
    // What is still owed from the same two integers is `Error.stack`, which
    // would get real line numbers out of them.
    method(cx, function_proto, "toString", 0, [](context & c, std::span<value>) {
        // THE REAL SOURCE, when there is any. A closure knows which program its
        // protos came from, and the program kept the text - so this is a
        // substring, not a reconstruction, and what comes back is exactly what
        // was written.
        const value self = c.current_this();
        if (self.is_kind(heap_kind::function)) {
            auto * closure = static_cast<closure_object *>(self.as_heap());
            if (closure->owner != nullptr && closure->proto != nullptr) {
                const struct function_proto & fp = *closure->proto;
                const std::string & text = closure->owner->source;
                if (fp.source_end > fp.source_begin && fp.source_end <= text.size()) {
                    return c.string(text.substr(fp.source_begin, fp.source_end - fp.source_begin));
                }
            }
        }
        // A native has no source; saying so the way every engine does keeps a
        // caller that concatenates the result from producing something strange.
        return c.string("function () { [native code] }");
    });
    // 20.2.3.6 %Function.prototype[@@hasInstance]%, which did not exist: the
    // key was absent, so `Symbol.hasInstance in Function.prototype` was false
    // and the eleven files that ask about it could not begin.
    //
    // Its own descriptor is { false, false, false }, unlike every other method
    // on this table, and its `name` is the bracketed form 10.2.9 gives a
    // symbol-keyed method. What it DOES is OrdinaryHasInstance, which is
    // context::instance_of.
    //
    // The `instanceof` OPERATOR still does not consult it - lib/Script/vm's
    // `instance_of` is OrdinaryHasInstance directly, with no @@hasInstance
    // lookup in front - so a class defining its own does not change what
    // `instanceof` answers here. This is the standard function, not the hook;
    // the hook is a change to the opcode and is named rather than implied.
    auto * has_instance =
        detail::cx_native(cx, "[Symbol.hasInstance]", [](context & c, std::span<value> a) {
            return value::boolean(c.instance_of(arg_at(a, 0), c.current_this()));
        });
    detail::install_arity(cx, has_instance, 1);
    function_proto->define("@@hasInstance", value::object(has_instance), attr_none);
    cx.set_prototype(context::proto_kind::function, function_proto);
}

// `new Function(body)` - A COMPILER AT RUN TIME.
//
// It existed and refused, because a closure holds a `const function_proto *`
// into the program it came from and nothing owned a program compiled here. Two
// things closed that: `closure_object::owner` records which program a closure's
// nested functions live in, so a frame from one program can call into another;
// and the context now OWNS the programs it compiles, so they outlive the
// closures that point into them.
//
// The body is wrapped in a function expression and returned, so the parameters
// and the body go through exactly the path a written-out function does. p5.js
// builds three of these for shader source; a bundle may build any number.
void install_dynamic_function(context & cx) {
    cx.define_native("Function", [](context & c, std::span<value> a) {
        // `new Function(a, b, 'return a + b')` - every argument but the last
        // names a parameter, and the last is the body. `new Function()` is a
        // function that does nothing, which is what the spec says.
        std::string params;
        for (std::size_t i = 0; i + 1 < a.size(); ++i) {
            if (!params.empty()) { params += ","; }
            params += c.to_string(a[i]);
        }
        const std::string body = a.empty() ? std::string{} : c.to_string(a[a.size() - 1]);
        // RETURNED, not left as an expression statement: the program's value is
        // what its top level returns, and a bare expression yields nothing.
        // The newlines are the spec's own formatting, and they matter - they
        // keep a `//` comment at the end of the body from swallowing the brace.
        const std::string source =
            "return (function anonymous(" + params + "\n) {\n" + body + "\n});";

        program compiled = compiler::compile(source);
        if (!compiled.ok) {
            // A SyntaxError a page can catch, because `new Function` on
            // user-supplied text is exactly where one is expected.
            c.throw_error("SyntaxError", compiled.error);
            return value::undefined();
        }
        const program & kept = c.own_program(std::move(compiled));
        return c.run_nested(kept);
    });
    // `eval(x)`, 19.2.1 - AS AN INDIRECT EVAL, always: the source runs at the
    // global scope, through the same run_nested `new Function` uses, and its
    // completion value (a trailing expression) comes back. A DIRECT eval that
    // sees the caller's locals needs the compiler to keep a frame's scope
    // alive by name, which this engine's register frames do not; test262's
    // eval-code/direct tests measure that gap by name. A non-string comes
    // back unchanged (step 1).
    cx.define_native("eval", [](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_string()) { return a.empty() ? value::undefined() : a[0]; }
        program compiled = compiler::compile_for_eval(c.to_string(a[0]));
        if (!compiled.ok) {
            c.throw_error("SyntaxError", compiled.error);
            return value::undefined();
        }
        const program & kept = c.own_program(std::move(compiled));
        return c.run_nested(kept);
    });
    // `Function.prototype`, reachable from script rather than only consulted by
    // lookup. `Function.prototype.call.bind(...)` and
    // `Function.prototype.hasOwnProperty` are ordinary idioms, and this is the
    // same table lookup already walks - so a page that adds to it is seen by
    // every function, which is what a page doing that expects.
    if (object_object * table = cx.prototype(context::proto_kind::function)) {
        static_cast<native_object *>(cx.global("Function").as_heap())
            ->set("prototype", value::object(table));
        link_constructor(cx, table, "Function", 1, cx.global("Function"));
    }
}

// See class_defined_name: the class's own members, made non-enumerable.
void install_class_defined(context & cx) {
    cx.define_native(std::string{class_defined_name}, [](context &, std::span<value> a) {
        if (a.empty() || !a[0].is_kind(heap_kind::function)) { return value::undefined(); }
        auto * ctor = static_cast<closure_object *>(a[0].as_heap());
        for (std::size_t i = 0; i < ctor->props.size(); ++i) {
            const std::string & key = ctor->props[i].first;
            ctor->set_attrs(key, static_cast<std::uint8_t>(ctor->attrs_of(key) & ~attr_enumerable));
        }
        for (accessor_entry & entry : ctor->accessors.entries) {
            entry.attrs = static_cast<std::uint8_t>(entry.attrs & ~attr_enumerable);
        }
        value * proto = ctor->find("prototype");
        if (proto == nullptr || !proto->is_object()) { return value::undefined(); }
        auto * table = static_cast<object_object *>(proto->as_heap());
        std::vector<std::pair<std::string, std::uint8_t>> entries;
        table->each_own_entry(
            [&](const std::string & key, std::uint8_t attrs) { entries.emplace_back(key, attrs); });
        for (const auto & [key, attrs] : entries) {
            table->set_attrs(key, static_cast<std::uint8_t>(attrs & ~attr_enumerable));
        }
        for (accessor_entry & entry : table->accessors.entries) {
            entry.attrs = static_cast<std::uint8_t>(entry.attrs & ~attr_enumerable);
        }
        return value::undefined();
    });
}

// `.next(v)`, `.throw(e)`, `.return(v)` - the iterator protocol, for every
// generator object at once. On a prototype for the same reason a promise's
// then/catch/finally are: three natives for the whole program rather than
// three per generator, and two generator objects then compare alike.
//
// Installed EAGERLY, unlike the promise table, because the object is built by
// context::make_generator over in the VM - which cannot reach into builtins to
// construct a table lazily.
void install_generator(context & cx) {
    object_object * table = detail::new_table(cx);
    const auto driver = [](context::resume_mode how) {
        return [how](context & c, std::span<value> a) {
            return c.generator_resume(c.current_this(), arg_at(a, 0), how);
        };
    };
    detail::method(cx, table, "next", 1, driver(context::resume_mode::next));
    detail::method(cx, table, "throw", 1, driver(context::resume_mode::thrown));
    detail::method(cx, table, "return", 1, driver(context::resume_mode::returned));
    // A GENERATOR IS ITS OWN ITERATOR, which is what `for (x of gen())` needs
    // and what makes `[...gen()]` work.
    detail::method(cx, table, "@@iterator", 0,
                   [](context & c, std::span<value>) { return c.current_this(); });
    cx.set_prototype(context::proto_kind::generator, table);

    // %AsyncGeneratorPrototype%, 27.6.1: the same three, each answering a
    // PROMISE of the record and queued behind the body - see
    // context::async_generator_request. An async generator is its own async
    // iterator.
    object_object * async_table = detail::new_table(cx);
    const auto async_driver = [](context::resume_mode how) {
        return [how](context & c, std::span<value> a) {
            return c.async_generator_request(c.current_this(), arg_at(a, 0), how);
        };
    };
    detail::method(cx, async_table, "next", 1, async_driver(context::resume_mode::next));
    detail::method(cx, async_table, "throw", 1, async_driver(context::resume_mode::thrown));
    detail::method(cx, async_table, "return", 1, async_driver(context::resume_mode::returned));
    detail::method(cx, async_table, "@@asyncIterator", 0,
                   [](context & c, std::span<value>) { return c.current_this(); });
    cx.set_prototype(context::proto_kind::async_generator, async_table);
}

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
    cx.define_native(std::string{declare_vars_name}, [](context & c, std::span<value> a) {
        for (const value name : a) {
            if (!name.is_string()) { continue; }
            const std::string text = c.to_string(name);
            if (!c.has_global(text)) { c.define_global(text, value::undefined()); }
        }
        return value::undefined();
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
    cx.define_native(std::string{define_accessor_name}, [](context & c, std::span<value> a) {
        if (a.size() < 4 || !a[0].is_object()) { return value::undefined(); }
        c.define_accessor(a[0], c.to_string(a[1]), a[2], a[3]);
        return value::undefined();
    });
    // See yield_delegate_open_name.
    cx.define_native(std::string{yield_delegate_open_name}, [](context & c, std::span<value> a) {
        const value source = a.empty() ? value::undefined() : a[0];
        const bool async = a.size() > 1 && context::truthy(a[1]);
        const value iterator =
            async ? c.call(c.global(std::string{async_iterator_name}), {&source, 1})
                  : c.get_iterator(source);
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
        if (context::truthy(c.lookup_property(result, "done"))) {
            record->set("done", value::boolean(true));
            record->set("value", c.lookup_property(result, "value"));
            if (co != nullptr) { co->delegate = value::undefined(); }
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
