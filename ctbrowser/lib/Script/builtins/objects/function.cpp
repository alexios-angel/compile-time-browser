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
               if (!self.is_callable()) { return value::undefined(); }
               const std::vector<value> rest(a.begin() + (a.empty() ? 0 : 1), a.end());
               return c.call(self, rest, bind_this(c, self, arg_at(a, 0)));
           });
    method(cx, function_proto, "apply", 2,
           [this_function, bind_this](context & c, std::span<value> a) {
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
                       c.throw_error("RangeError",
                                     "too many arguments to Function.prototype.apply");
                       return value::undefined();
                   }
                   args.reserve(static_cast<std::size_t>(count));
                   for (double i = 0; i < count; ++i) {
                       args.push_back(c.lookup_index(list, value::number(i)));
                   }
               }
               const context::rooted_values keep{c, args};
               return c.call(self, args, bind_this(c, self, arg_at(a, 0)));
           });
    method(cx, function_proto, "bind", 1,
           [this_function, bind_this](context & c, std::span<value> a) {
               const value self = this_function(c, "bind");
               if (!self.is_callable()) { return value::undefined(); }
               const value receiver = bind_this(c, self, arg_at(a, 0));
               // 10.4.1.3 step 1, ? target.[[GetPrototypeOf]](): a proxy's trap
               // runs here and may throw; the bound function's [[Prototype]] is
               // the target's, not Function.prototype's.
               const value proto = detail::prototype_of(c, self);
               if (c.throw_pending()) { return value::undefined(); }
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
               fn->define("@#BoundTargetFunction", self, attr_none);
               fn->is_constructor = is_constructor(self);
               fn->proto_link = proto;
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
                   if (c.throw_pending()) { return value::undefined(); }
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
               if (c.throw_pending()) { return value::undefined(); }
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
        if (!name.empty() && (name.front() == '[' || name.starts_with("@@") ||
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
    // symbol-keyed method. What it DOES is OrdinaryHasInstance, which is
    // context::instance_of.
    //
    // The `instanceof` OPERATOR still does not consult it - lib/Script/vm's
    // `instance_of` is OrdinaryHasInstance directly, with no @@hasInstance
    // lookup in front - so a class defining its own does not change what
    // `instanceof` answers here. This is the standard function, not the hook;
    // the hook is a change to the opcode and is named rather than implied.
    auto * has_instance =
        cx.allocate<native_object>("[Symbol.hasInstance]", [](context & c, std::span<value> a) {
            return value::boolean(c.instance_of(arg_at(a, 0), c.current_this()));
        });
    detail::install_arity(cx, has_instance, 1);
    has_instance->is_constructor = false;
    function_proto->define("@@hasInstance", value::object(has_instance), attr_none);
    // 10.2.4 AddRestrictedFunctionProperties: `caller` and `arguments` on
    // Function.prototype are accessors whose getter and setter are
    // %ThrowTypeError% - `f.caller` on a strict function (a class, an arrow, a
    // built-in) is a TypeError. A sloppy function answers null before the
    // chain gets here (lookup_property's closure arm), as every browser does.
    const value thrower = detail::accessor_fn(cx, "", [](context & c, std::span<value>) {
        c.throw_error("TypeError", "'caller', 'callee', and 'arguments' properties may not be "
                                   "accessed on strict mode functions or the arguments objects "
                                   "for calls to them");
        return value::undefined();
    });
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
    return c.run_nested(kept);
}
} // namespace

void install_dynamic_function(context & cx) {
    cx.define_native("Function", [](context & c, std::span<value> a) {
        return dynamic_function(c, a, "function");
    });
    // `eval(x)`, 19.2.1 - AS AN INDIRECT EVAL, always: the source runs at the
    // global scope, through the same run_nested `new Function` uses, and its
    // completion value (a trailing expression) comes back. A DIRECT eval that
    // sees the caller's locals needs the compiler to keep a frame's scope
    // alive by name, which this engine's register frames do not; test262's
    // eval-code/direct tests measure that gap by name. A non-string comes
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
        if (a.size() < 2 || !a[1].is_kind(heap_kind::function)) { return value::undefined(); }
        auto * klass = static_cast<closure_object *>(a[1].as_heap());
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
            const std::size_t n = static_cast<string_object *>(target.as_heap())->text.size();
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
    // See param_eval_name. The intrinsic eval is remembered so a page that
    // rebinds the global `eval` gets its own function called instead.
    {
        const value intrinsic = cx.global("eval");
        auto * native = cx.allocate<native_object>(
            std::string{param_eval_name}, [intrinsic](context & c, std::span<value> a) {
                const value source = a.empty() ? value::undefined() : a[0];
                const value current = c.global("eval");
                if (current.bits() != intrinsic.bits()) { return c.call(current, {&source, 1}); }
                if (!source.is_string()) { return source; }
                program compiled = compiler::compile_for_eval(c.to_string(source));
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
        static_cast<native_object *>(cx.global("Function").as_heap())
            ->set("prototype", value::object(table));
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
        table->define("@@toStringTag", cx.string(name), attr_configurable);
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
    detail::method(cx, table, "@@iterator", 0,
                   [](context & c, std::span<value>) { return c.current_this(); });
    table->define("@@toStringTag", cx.string("Generator"), attr_configurable); // 27.5.1.5
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
    async_table->define("@@toStringTag", cx.string("AsyncGenerator"), attr_configurable);
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
