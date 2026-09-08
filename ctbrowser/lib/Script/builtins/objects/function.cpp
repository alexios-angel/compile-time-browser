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
}

} // namespace ctbrowser::script::builtins_detail
