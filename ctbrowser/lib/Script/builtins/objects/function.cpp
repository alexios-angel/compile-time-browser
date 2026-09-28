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
    // 20.2.3: Function.prototype is itself a built-in function of `length` 0
    // and `name` "", both { false, false, true } and `length` first (10.2.9's
    // order) - so a function whose own `length` or `name` was deleted reads
    // these rather than undefined.
    function_proto->define("length", value::number(0), attr_configurable);
    function_proto->define("name", cx.string(""), attr_configurable);

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
    // OrdinaryCallBindThis, 10.2.1.2: a SLOPPY function's `this` is the
    // global object for null or undefined and a wrapper for a primitive; a
    // strict function, an arrow and a built-in take the value as given. Done
    // here for the three explicit-receiver spellings; a plain `f()` is the
    // VM's own call and binds undefined, which a sloppy body then sees.
    const auto bind_this = [](context & c, value callee, value receiver) {
        if (!callee.is_kind(heap_kind::function)) { return receiver; }
        const struct function_proto * p = static_cast<closure_object *>(callee.as_heap())->proto;
        if (p == nullptr || p->is_strict || p->is_arrow) { return receiver; }
        if (receiver.is_nullish()) { return c.global_this(); }
        return detail::box_primitive(c, receiver);
    };
    method(cx, function_proto, "call", 1,
           [this_function, bind_this](context & c, std::span<value> a) {
               const value self = this_function(c, "call");
               if (!self.is_callable() || c.refuse_class_call(self)) { return value::undefined(); }
               const std::vector<value> rest(a.begin() + (a.empty() ? 0 : 1), a.end());
               return c.call(self, rest, bind_this(c, self, arg_at(a, 0)));
           });
    method(cx, function_proto, "apply", 2,
           [this_function, bind_this](context & c, std::span<value> a) {
               const value self = this_function(c, "apply");
               if (!self.is_callable() || c.refuse_class_call(self)) { return value::undefined(); }
               // A null or undefined argArray is an empty list (20.2.3.1 step 3).
               const value list = arg_at(a, 1);
               const value args = list.is_nullish()
                                      ? c.make_array()
                                      : detail::create_argument_list(c, list, 65536.0);
               if (args.is_undefined()) { return args; }
               const context::rooted keep{c, args};
               return c.call(self, static_cast<array_object *>(args.as_heap())->items,
                             bind_this(c, self, arg_at(a, 0)));
           });
    method(cx, function_proto, "bind", 1,
           [this_function, bind_this](context & c, std::span<value> a) {
               // A getter on the target's `length` or `name`, or a proxy's
               // getPrototypeOf trap, may throw - and a throw that has landed
               // is not pending (context::unwinds).
               const detail::unwind_watch watch{c};
               const value self = this_function(c, "bind");
               if (!self.is_callable()) { return value::undefined(); }
               const value receiver = bind_this(c, self, arg_at(a, 0));
               // 10.4.1.3 step 1, ? target.[[GetPrototypeOf]](): a proxy's trap
               // runs here and may throw; the bound function's [[Prototype]] is
               // the target's, not Function.prototype's.
               const value proto = detail::prototype_of(c, self);
               if (watch.threw()) { return value::undefined(); }
               // The arguments bound NOW are prepended to the ones supplied later,
               // which is what makes `f.bind(o, 1)` a partial application rather than
               // just a receiver change.
               const auto bound =
                   std::make_shared<std::vector<value>>(a.begin() + (a.empty() ? 0 : 1), a.end());
               auto * fn = c.allocate<native_object>(
                   "bound", [self, receiver, bound](context & inner, std::span<value> later) {
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
               // The [[Prototype]] taken above, which the collector does not
               // reach through proto_link - null is a value like any other, so
               // the layout stays fixed: target, this, prototype, then the
               // bound arguments.
               fn->retained.push_back(proto);
               fn->retained.insert(fn->retained.end(), bound->begin(), bound->end());
               // [[BoundTargetFunction]], for `new bound()`: context::construct
               // constructs the target with the bound arguments prepended (10.4.1.2),
               // reading them off `retained` as laid out above. A bound function is
               // a constructor exactly when its target is (10.4.1.3 step 5).
               fn->define(CTBROWSER_PRIVATE_KEY("BoundTargetFunction"), self, attr_none);
               fn->is_constructor = is_constructor(self);
               fn->proto_link = proto.is_null() ? value::undefined() : proto; // explicit
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
                   if (watch.threw()) { return value::undefined(); }
                   if (target_length.is_number()) {
                       // Step 7.b: +Infinity stays, -Infinity is 0, else
                       // ToIntegerOrInfinity less the bound count - not ToLength,
                       // which clamped an infinite length to 2^53 - 1.
                       const double n = target_length.as_number();
                       if (std::isinf(n)) {
                           left = n > 0 ? n : 0.0;
                       } else if (!std::isnan(n)) {
                           left = std::trunc(n) - static_cast<double>(bound->size());
                       }
                   }
               }
               fn->define("length", value::number(std::max(0.0, left)), attr_configurable);
               const value target_name = c.lookup_property(self, "name");
               if (watch.threw()) { return value::undefined(); }
               fn->define("name",
                          c.string("bound " + (target_name.is_string() ? c.to_string(target_name)
                                                                       : std::string{})),
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
        // 20.2.3.5 step 3: anything that is not callable is a TypeError -
        // `Function.prototype.toString.call({})` does not answer for Object.
        if (!self.is_callable()) {
            c.throw_error("TypeError",
                          "Function.prototype.toString requires that 'this' be a Function");
            return value::undefined();
        }
        // A native has no source; the NativeFunction syntax of 20.2.3.5 with
        // its `name` as the IdentifierName is what every engine answers, and
        // what test262's nativeFunctionMatcher parses. A bound function and a
        // proxy are natives here too (steps 2 and 4).
        std::string name;
        if (self.is_kind(heap_kind::native)) {
            const value own = c.lookup_property(self, "name");
            if (own.is_string()) { name = c.to_string(own); }
        }
        // A symbol-named built-in's name is "[Symbol.x]", which is not an
        // IdentifierName; the specification permits omitting it (the
        // IdentifierName is optional), so it is left out.
        if (!name.empty() && (name.front() == '[' || name.starts_with(symbol_key_mark) ||
                              name.find(' ') != std::string::npos)) {
            name.clear();
        }
        return c.string("function " + name + "() { [native code] }");
    });
    // 20.2.3.6 %Function.prototype[@@hasInstance]%, which did not exist: the
    // key was absent, so `Symbol.hasInstance in Function.prototype` was false
    // and the eleven files that ask about it could not begin.
    //
    // Its own descriptor is { false, false, false }, unlike every other method
    // on this table, and its `name` is the bracketed form 10.2.9 gives a
    // symbol-keyed method. What it DOES is OrdinaryHasInstance (7.3.22) - over
    // the public operations, because `instanceof` (context::instance_of)
    // dispatches to a constructor's OWN @@hasInstance first, and this method
    // called on such a constructor must not.
    auto * has_instance =
        cx.allocate<native_object>("[Symbol.hasInstance]", [](context & c, std::span<value> a) {
            const value ctor = c.current_this();
            value subject = arg_at(a, 0);
            if (!ctor.is_callable()) { return value::boolean(false); }
            if (ctor.is_kind(heap_kind::native)) {
                if (const value * bound =
                        static_cast<native_object *>(ctor.as_heap())
                            ->find(CTBROWSER_PRIVATE_KEY("BoundTargetFunction"))) {
                    return value::boolean(c.instance_of(subject, *bound));
                }
            }
            if (!subject.is_object_like()) { return value::boolean(false); }
            const detail::unwind_watch watch{c};
            const value wanted = c.lookup_property(ctor, "prototype");
            if (watch.threw()) { return value::undefined(); }
            if (!wanted.is_object_like()) {
                c.throw_error("TypeError", "Function has non-object prototype in instanceof "
                                           "check");
                return value::undefined();
            }
            const context::rooted keep{c, wanted};
            for (int depth = 0; depth < 4096; ++depth) {
                subject = detail::prototype_of(c, subject);
                if (watch.threw()) { return value::undefined(); }
                if (!subject.is_object_like()) { return value::boolean(false); }
                if (subject.same_value(wanted)) { return value::boolean(true); }
            }
            return value::boolean(false);
        });
    detail::install_arity(cx, has_instance, 1);
    has_instance->is_constructor = false;
    function_proto->define(CTBROWSER_SYMBOL_KEY("hasInstance"), value::object(has_instance),
                           attr_none);
    // 10.2.4 AddRestrictedFunctionProperties: `caller` and `arguments` on
    // Function.prototype are accessors whose getter and setter are
    // %ThrowTypeError% - `f.caller` on a strict function (a class, an arrow, a
    // built-in) is a TypeError. A sloppy function answers null before the
    // chain gets here (lookup_property's closure arm), as every browser does.
    const value thrower = cx.throw_type_error(); // one per realm, 10.2.4.1
    function_proto->define_accessor("caller", thrower, thrower, attr_configurable);
    function_proto->define_accessor("arguments", thrower, thrower, attr_configurable);
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
namespace {
// CreateDynamicFunction (20.2.1.1.1) for the four kinds: `Function`,
// %GeneratorFunction%, %AsyncFunction% and %AsyncGeneratorFunction% differ
// only in the keyword the source is wrapped in.
value dynamic_function(context & c, std::span<value> a, const char * keyword) {
    // `new Function(a, b, 'return a + b')` - every argument but the last
    // names a parameter, and the last is the body. `new Function()` is a
    // function that does nothing, which is what the spec says.
    std::string params;
    for (std::size_t i = 0; i + 1 < a.size(); ++i) {
        if (!params.empty()) { params += ","; }
        params += c.to_string(a[i]);
        if (c.throw_pending()) { return value::undefined(); }
    }
    const std::string body = a.empty() ? std::string{} : c.to_string(a[a.size() - 1]);
    if (c.throw_pending()) { return value::undefined(); }
    // RETURNED, not left as an expression statement: the program's value is
    // what its top level returns, and a bare expression yields nothing.
    // The newlines are the spec's own formatting, and they matter - they
    // keep a `//` comment at the end of the body from swallowing the brace.
    const std::string source =
        std::string{"return ("} + keyword + " anonymous(" + params + "\n) {\n" + body + "\n});";

    program compiled = compiler::compile(source);
    if (!compiled.ok) {
        // A SyntaxError a page can catch, because `new Function` on
        // user-supplied text is exactly where one is expected.
        c.throw_error("SyntaxError", compiled.error);
        return value::undefined();
    }
    const program & kept = c.own_program(std::move(compiled));
    const value made = c.run_nested(kept);
    // CreateDynamicFunction step 22-24 (OrdinaryFunctionCreate off
    // GetPrototypeFromConstructor(newTarget)): under `new` from a subclass -
    // `class F extends Function {}`, reached through super() - the closure's
    // [[Prototype]] is the instance's, F.prototype, and not the intrinsic.
    // The same shape as detail::adopt_subclass_prototype for an array.
    if (made.is_kind(heap_kind::function)) {
        const value self = c.current_this();
        if (detail::constructing_this(self)) {
            const value proto = static_cast<object_object *>(self.as_heap())->prototype;
            if (proto.is_object() &&
                proto.as_heap() != c.prototype(context::proto_kind::function) &&
                proto.as_heap() != c.prototype(context::proto_kind::generator_function) &&
                proto.as_heap() != c.prototype(context::proto_kind::async_function) &&
                proto.as_heap() != c.prototype(context::proto_kind::async_generator_function)) {
                static_cast<closure_object *>(made.as_heap())->proto_link = proto;
            }
        }
    }
    return made;
}
} // namespace

void install_dynamic_function(context & cx) {
    cx.define_native("Function", [](context & c, std::span<value> a) {
        return dynamic_function(c, a, "function");
    });
    // `eval(x)`, 19.2.1 - THE INDIRECT EVAL: the source runs at the global
    // scope, through the same run_nested `new Function` uses, and its
    // completion value (a trailing expression) comes back. A direct `eval(x)`
    // in a function does not come here: the compiler routes it through
    // direct_eval_name with the caller's bindings (below). A non-string comes
    // back unchanged (step 1).
    detail::global_fn(cx, "eval", 1, [](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_string()) { return a.empty() ? value::undefined() : a[0]; }
        program compiled = compiler::compile_for_eval(c.to_string(a[0]));
        if (!compiled.ok) {
            c.throw_error("SyntaxError", compiled.error);
            return value::undefined();
        }
        const program & kept = c.own_program(std::move(compiled));
        return c.run_nested(kept);
    });
    // `import.source(x)` - see import_source_name. Nothing here loads: a
    // source text module has no module source to hand out, so the answer is
    // a rejection either way, and never a throw (the promise is what the
    // caller holds).
    cx.define_native(std::string{import_source_name}, [](context & c, std::span<value> a) {
        (void)c.to_string(a.empty() ? value::undefined() : a[0]);
        if (c.failed()) { return value::undefined(); }
        if (c.throw_pending()) { return c.make_promise(c.take_pending_throw(), true); }
        return c.make_promise(
            c.make_error("SyntaxError", "a source text module has no module source to import"),
            true);
    });
    // See bind_this_name.
    cx.define_native(std::string{bind_this_name}, [](context & c, std::span<value> a) {
        if (!a.empty() && a[0].is_object_like()) { c.rebind_receiver(a[0]); }
        return value::undefined();
    });
    // See init_fields_name.
    cx.define_native(std::string{init_fields_name}, [](context & c, std::span<value> a) {
        if (a.size() < 2) { return value::undefined(); }
        // The class itself (at a base constructor's entry), or its home
        // object - C.prototype, whose own `constructor` is C (after super()).
        value klass_value = a[1];
        if (klass_value.is_object() && !klass_value.is_kind(heap_kind::function)) {
            const value * owner =
                static_cast<object_object *>(klass_value.as_heap())->find("constructor");
            klass_value = owner == nullptr ? value::undefined() : *owner;
        }
        if (!klass_value.is_kind(heap_kind::function)) { return value::undefined(); }
        auto * klass = static_cast<closure_object *>(klass_value.as_heap());
        if (value * fields = klass->find("__fields"); fields != nullptr && fields->is_callable()) {
            (void)c.call(*fields, {}, a[0]);
        }
        return value::undefined();
    });
    // See delete_ref_name.
    cx.define_native(std::string{delete_ref_name}, [](context & c, std::span<value> a) {
        const value target = a.empty() ? value::undefined() : a[0];
        const bool strict = a.size() > 2 && context::truthy(a[2]);
        if (a.size() > 3 && context::truthy(a[3])) {
            c.throw_error("ReferenceError", "Unsupported reference to 'super'");
            return value::undefined();
        }
        if (target.is_nullish()) {
            c.throw_error("TypeError", "Cannot convert " +
                                           std::string{target.is_null() ? "null" : "undefined"} +
                                           " to object");
            return value::undefined();
        }
        const std::string key = c.to_string(a.size() > 1 ? a[1] : value::undefined());
        if (c.throw_pending()) { return value::undefined(); }
        // A primitive base is ToObject'd: a string's `length` and indices are
        // not configurable, anything else on it is not there and deletes true.
        bool ok = true;
        if (target.is_string()) {
            std::uint32_t at = 0;
            const std::size_t n = static_cast<string_object *>(target.as_heap())->length();
            ok = !(key == "length" || (object_object::array_index_key(key, at) && at < n));
        } else if (target.is_object_like()) {
            ok = c.delete_own_property(target, key);
            if (c.throw_pending()) { return value::undefined(); }
        }
        if (!ok && strict) {
            c.throw_error("TypeError", "Cannot delete property '" + key + "'");
            return value::undefined();
        }
        return value::boolean(ok);
    });
    // See direct_eval_name and compiler_impl::compile_direct_eval: a direct
    // eval's source, compiled with the caller's bindings as its upvalues.
    {
        const value intrinsic = cx.global("eval");
        auto * native = cx.allocate<native_object>(
            std::string{direct_eval_name}, [intrinsic](context & c, std::span<value> a) {
                const value eval_fn = a.empty() ? value::undefined() : a[0];
                const value source = a.size() > 1 ? a[1] : value::undefined();
                // Only %eval% makes the call direct (13.3.6.1 step 6.a); any
                // other function under the name is called as one.
                if (eval_fn.bits() != intrinsic.bits()) {
                    // ...and a value that is not a function is the TypeError a
                    // call of it is (13.3.6.1 EvaluateCall step 4).
                    if (!eval_fn.is_callable()) {
                        c.throw_error("TypeError", "eval is not a function");
                        return value::undefined();
                    }
                    return c.call(eval_fn, {&source, 1});
                }
                if (!source.is_string()) { return source; }
                const value callee = a.size() > 2 ? a[2] : value::undefined();
                const value self = a.size() > 3 ? a[3] : value::undefined();
                const std::string descriptor = a.size() > 4 ? c.to_string(a[4]) : std::string{};
                auto * caller = callee.is_kind(heap_kind::function)
                                    ? static_cast<closure_object *>(callee.as_heap())
                                    : nullptr;
                std::vector<eval_binding> outer;
                std::vector<value> cells;
                // "... name [strict|sloppy] [lexical]" - see eval_binding.
                const auto bind = [&](std::string_view rest) {
                    const std::size_t space = rest.find(' ');
                    const std::string_view flags =
                        space == std::string_view::npos ? "" : rest.substr(space);
                    eval_binding & made = outer.emplace_back();
                    made.name = std::string{rest.substr(0, space)};
                    made.readonly =
                        flags.find(" strict") != std::string_view::npos   ? immutability::strict
                        : flags.find(" sloppy") != std::string_view::npos ? immutability::sloppy
                                                                          : immutability::none;
                    made.lexical = flags.find(" lexical") != std::string_view::npos;
                };
                const value new_target = a.size() > 5 ? a[5] : value::undefined();
                eval_site site;
                std::size_t next = 6;
                std::size_t at = 0;
                bool first = true;
                while (at <= descriptor.size()) {
                    const std::size_t end = std::min(descriptor.find('\n', at), descriptor.size());
                    const std::string_view line{descriptor.data() + at, end - at};
                    at = end + 1;
                    if (first) {
                        // "strict|sloppy [function] [method] [field]" - eval_site.
                        const auto says = [&](std::string_view word) {
                            return line.find(word) != std::string_view::npos;
                        };
                        site.strict = line.starts_with("strict");
                        site.in_function = says(" function");
                        site.in_method = says(" method");
                        site.in_class_field_initializer = says(" field");
                        first = false;
                        continue;
                    }
                    if (line.size() < 3) { continue; }
                    if (line[0] == 'P') {
                        const std::size_t space = line.find(' ', 2);
                        if (space == std::string_view::npos) { continue; }
                        site.private_names.emplace_back(line.substr(2, space - 2),
                                                        line.substr(space + 1));
                        continue;
                    }
                    if (line[0] == 'U') {
                        const std::size_t space = line.find(' ', 2);
                        if (space == std::string_view::npos || caller == nullptr) { continue; }
                        std::size_t index = 0;
                        for (const char ch : line.substr(2, space - 2)) {
                            index = index * 10 + static_cast<std::size_t>(ch - '0');
                        }
                        if (index >= caller->upvalues.size()) { continue; }
                        bind(line.substr(space + 1));
                        cells.push_back(caller->upvalues[index]);
                    } else {
                        const value given = next < a.size() ? a[next] : value::undefined();
                        ++next;
                        bind(line.substr(2));
                        // "V": a local nothing boxed - the eval sees its value.
                        cells.push_back(line[0] == 'L' && given.is_kind(heap_kind::cell)
                                            ? given
                                            : value::object(c.allocate<cell_object>(given)));
                    }
                }
                program compiled =
                    compiler::compile_for_direct_eval(c.to_string(source), outer, site);
                if (!compiled.ok) {
                    c.throw_error("SyntaxError", compiled.error);
                    return value::undefined();
                }
                const program & kept = c.own_program(std::move(compiled));
                // FROM GLOBAL CODE its declarations are globals, instantiated
                // as an indirect eval's are (program::eval_global).
                if (kept.eval_global && !c.eval_declaration_instantiation(kept)) {
                    return value::undefined();
                }
                for (const std::string & name : kept.hoisted_vars) {
                    if (!c.annex_b_skipped(kept, name)) { c.declare_global_var(name); }
                }
                for (const std::string & name : kept.function_names) {
                    c.declare_global_function(name, true);
                }
                auto * entry = c.allocate<closure_object>(&kept.functions[0]);
                entry->owner = &kept;
                entry->realm = caller != nullptr && caller->realm != nullptr ? caller->realm
                                                                             : &c.current_realm();
                entry->upvalues = std::move(cells);
                // `super.x` in the source is the caller's (19.2.1.1 step 7.a:
                // the eval runs with the caller's this environment, whose
                // function holds the home object) - see op::load_home.
                if (caller != nullptr) {
                    if (const value * home = caller->find("__home")) {
                        entry->set("__home", *home);
                    }
                }
                c.pass_new_target(new_target);
                return c.call(value::object(entry), {}, self);
            });
        native->retained.push_back(intrinsic);
        cx.define_global(std::string{direct_eval_name}, value::object(native));
    }
    // See param_eval_name. The intrinsic eval is remembered so a page that
    // rebinds the global `eval` gets its own function called instead.
    {
        const value intrinsic = cx.global("eval");
        auto * native = cx.allocate<native_object>(
            std::string{param_eval_name}, [intrinsic](context & c, std::span<value> a) {
                const value source = a.empty() ? value::undefined() : a[0];
                const value current = c.global("eval");
                if (current.bits() != intrinsic.bits()) {
                    if (!current.is_callable()) {
                        c.throw_error("TypeError", "eval is not a function");
                        return value::undefined();
                    }
                    return c.call(current, {&source, 1});
                }
                if (!source.is_string()) { return source; }
                // In a function's parameters - so `new.target` is allowed, as
                // it always was here.
                eval_site site;
                site.in_function = true;
                program compiled = compiler::compile_for_eval(c.to_string(source), site);
                if (!compiled.ok) {
                    c.throw_error("SyntaxError", compiled.error);
                    return value::undefined();
                }
                for (std::size_t i = 1; i < a.size(); ++i) {
                    const std::string bound = c.to_string(a[i]);
                    for (const std::string & declared : compiled.hoisted_vars) {
                        if (declared != bound) { continue; }
                        c.throw_error("SyntaxError",
                                      "Identifier '" + bound +
                                          "' has already been declared: a direct eval in a "
                                          "parameter expression may not var-declare a name of "
                                          "that scope");
                        return value::undefined();
                    }
                }
                const program & kept = c.own_program(std::move(compiled));
                return c.run_nested(kept);
            });
        native->retained.push_back(intrinsic);
        cx.define_global(std::string{param_eval_name}, value::object(native));
    }
    // `Function.prototype`, reachable from script rather than only consulted by
    // lookup. `Function.prototype.call.bind(...)` and
    // `Function.prototype.hasOwnProperty` are ordinary idioms, and this is the
    // same table lookup already walks - so a page that adds to it is seen by
    // every function, which is what a page doing that expects.
    if (object_object * table = cx.prototype(context::proto_kind::function)) {
        // { false, false, false } (20.2.2.2), as every constructor's is.
        static_cast<native_object *>(cx.global("Function").as_heap())
            ->define("prototype", value::object(table), attr_none);
        link_constructor(cx, table, "Function", 1, cx.global("Function"));
    }
    // %GeneratorFunction% (27.3), %AsyncGeneratorFunction% (27.4) and
    // %AsyncFunction% (27.7): not globals - reached through
    // `(function* () {}).constructor` - each a CreateDynamicFunction over its
    // keyword whose [[Prototype]] is %Function%, with a prototype object
    // inheriting Function.prototype that every closure of that shape has as
    // its [[Prototype]] (context::function_proto_kind). The generator kinds'
    // prototype objects carry `prototype` = %GeneratorPrototype% and the
    // reverse `constructor` link, both { false, false, true }.
    const auto intrinsic = [&](context::proto_kind kind, const char * name, const char * keyword,
                               context::proto_kind instances) {
        object_object * table = detail::new_table(cx);
        if (object_object * fn_proto = cx.prototype(context::proto_kind::function)) {
            table->prototype = value::object(fn_proto);
        }
        table->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string(name), attr_configurable);
        auto * ctor = cx.allocate<native_object>(name, [keyword](context & c, std::span<value> a) {
            return dynamic_function(c, a, keyword);
        });
        ctor->proto_link = cx.global("Function");
        ctor->define("prototype", value::object(table), attr_none);
        table->define("constructor", value::object(ctor), attr_configurable);
        ctor->define("length", value::number(1), attr_configurable);
        ctor->define("name", cx.string(name), attr_configurable);
        if (instances != context::proto_kind::count_) {
            if (object_object * proto = cx.prototype(instances)) {
                table->define("prototype", value::object(proto), attr_configurable);
                proto->define("constructor", value::object(table), attr_configurable);
            }
        }
        cx.set_prototype(kind, table);
    };
    intrinsic(context::proto_kind::generator_function, "GeneratorFunction", "function*",
              context::proto_kind::generator);
    intrinsic(context::proto_kind::async_generator_function, "AsyncGeneratorFunction",
              "async function*", context::proto_kind::async_generator);
    intrinsic(context::proto_kind::async_function, "AsyncFunction", "async function",
              context::proto_kind::count_);
    // %AbstractModuleSource% (source phase imports, 28.1.1): not a global -
    // the abstract class every module source class extends. Constructing it
    // is the TypeError of 28.1.1.1 whether or not `new` is used, and its
    // prototype's @@toStringTag getter answers a source's
    // [[ModuleSourceClassName]], undefined for any value without one. This
    // engine makes no module source yet, so nothing has the slot; the host
    // reaches the intrinsic through Function.prototype's private key
    // (ct262's $262.AbstractModuleSource).
    if (object_object * fn_proto = cx.prototype(context::proto_kind::function)) {
        object_object * table = detail::new_table(cx);
        auto * ctor =
            cx.allocate<native_object>("AbstractModuleSource", [](context & c, std::span<value>) {
                c.throw_error("TypeError", "%AbstractModuleSource% is abstract - it cannot be "
                                           "called or constructed");
                return value::undefined();
            });
        ctor->define("length", value::number(0), attr_configurable);
        ctor->define("name", cx.string("AbstractModuleSource"), attr_configurable);
        ctor->define("prototype", value::object(table), attr_none);
        table->define("constructor", value::object(ctor), attr_writable | attr_configurable);
        auto * tag = detail::method_native(
            cx, "get [Symbol.toStringTag]", [](context & c, std::span<value>) {
                const value self = c.current_this();
                if (!self.is_object()) { return value::undefined(); }
                const value * name = static_cast<object_object *>(self.as_heap())
                                         ->find(CTBROWSER_PRIVATE_KEY("ModuleSourceClassName"));
                return name == nullptr ? value::undefined() : *name;
            });
        detail::install_arity(cx, tag, 0);
        table->define_accessor(CTBROWSER_SYMBOL_KEY("toStringTag"), value::object(tag),
                               value::undefined(), attr_configurable);
        fn_proto->define(CTBROWSER_PRIVATE_KEY("AbstractModuleSource"), value::object(ctor),
                         attr_none);
    }
}

// See class_defined_name: the class's own members, made non-enumerable.
void install_class_defined(context & cx) {
    cx.define_native(std::string{class_defined_name}, [](context &, std::span<value> a) {
        if (a.empty() || !a[0].is_kind(heap_kind::function)) { return value::undefined(); }
        auto * ctor = static_cast<closure_object *>(a[0].as_heap());
        ctor->class_constructor = true;
        ctor->defining_class = false; // see class_heritage_name
        for (std::size_t i = 0; i < ctor->props.size(); ++i) {
            const std::string & key = ctor->props[i].first;
            ctor->set_attrs(key, static_cast<std::uint8_t>(ctor->attrs_of(key) & ~attr_enumerable));
        }
        for (accessor_entry & entry : ctor->accessors.entries) {
            entry.attrs = static_cast<std::uint8_t>(entry.attrs & ~attr_enumerable);
        }
        value * proto = ctor->find("prototype");
        if (proto == nullptr || !proto->is_object()) { return value::undefined(); }
        // MakeConstructor(F, false, proto) (15.7.14 step 16): a class's
        // `prototype` is not writable, where a function's is.
        ctor->set_attrs("prototype", attr_none);
        auto * table = static_cast<object_object *>(proto->as_heap());
        table->defining_class = false;
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
            // 27.5.3.2 GeneratorValidate: a receiver that is not a generator
            // object is a TypeError, not a finished iteration.
            const value self = c.current_this();
            const value * held = self.is_object()
                                     ? static_cast<object_object *>(self.as_heap())->find("__co")
                                     : nullptr;
            if (held == nullptr || !held->is_kind(heap_kind::coroutine)) {
                c.throw_error("TypeError", "the receiver is not a generator object");
                return value::undefined();
            }
            return c.generator_resume(self, arg_at(a, 0), how);
        };
    };
    detail::method(cx, table, "next", 1, driver(context::resume_mode::next));
    detail::method(cx, table, "throw", 1, driver(context::resume_mode::thrown));
    detail::method(cx, table, "return", 1, driver(context::resume_mode::returned));
    // A GENERATOR IS ITS OWN ITERATOR, which is what `for (x of gen())` needs
    // and what makes `[...gen()]` work.
    detail::method(cx, table, CTBROWSER_SYMBOL_KEY("iterator"), 0,
                   [](context & c, std::span<value>) { return c.current_this(); });
    table->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("Generator"),
                  attr_configurable); // 27.5.1.5
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
    detail::method(cx, async_table, CTBROWSER_SYMBOL_KEY("asyncIterator"), 0,
                   [](context & c, std::span<value>) { return c.current_this(); });
    async_table->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("AsyncGenerator"),
                        attr_configurable);
    cx.set_prototype(context::proto_kind::async_generator, async_table);
}

} // namespace ctbrowser::script::builtins_detail
