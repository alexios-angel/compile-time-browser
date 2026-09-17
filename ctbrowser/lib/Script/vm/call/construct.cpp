// ctbrowser.script context - construction: `new` in its three spellings,
// `prototype` on demand, and class field initialisers.
//
// One of five files carved out of a 1,171-line vm/call.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

namespace {
// The callee's name for "X is not a constructor", or "the value" when it has none.
[[nodiscard]] std::string describe_new_target(value callee) {
    std::string name;
    if (callee.is_kind(heap_kind::native)) {
        name = static_cast<const native_object *>(callee.as_heap())->name;
    } else if (callee.is_kind(heap_kind::function)) {
        const function_proto * p = static_cast<const closure_object *>(callee.as_heap())->proto;
        if (p != nullptr) { name = p->display_name(); }
    }
    return name.empty() ? std::string{"the value"} : name;
}
} // namespace

bool is_constructor(value v) {
    if (v.is_kind(heap_kind::native)) {
        return static_cast<const native_object *>(v.as_heap())->is_constructor;
    }
    if (v.is_kind(heap_kind::function)) {
        const function_proto * p = static_cast<const closure_object *>(v.as_heap())->proto;
        // A method (`{ m() {} }`) is not one either, but the bytecode does not
        // record that - the compiler's, not this file's.
        return p == nullptr || !(p->is_arrow || p->is_generator || p->is_async);
    }
    if (v.is_kind(heap_kind::proxy)) {
        return is_constructor(static_cast<const proxy_object *>(v.as_heap())->target);
    }
    return false;
}

// EVERY FUNCTION HAS A `prototype`, and JavaScript relies on it far beyond
// classes: `function F() {}; new F() instanceof F` is the constructor-function
// pattern every transpiler emits, and Babel's own `_classCallCheck` guard is
// exactly that test. A class got one from the compiler; a plain function got
// nothing, so `new F()` produced an object with no prototype and `instanceof`
// was false for it.
//
// Made on demand rather than at closure creation: a program allocates far more
// functions than it constructs, and an object per closure is a real cost for
// something most of them never use.
value context::ensure_prototype(value fn) {
    if (!fn.is_kind(heap_kind::function)) { return value::undefined(); }
    auto * closure = static_cast<closure_object *>(fn.as_heap());
    if (value * existing = closure->find("prototype")) { return *existing; }
    // An arrow is not a constructor and never needs one; nor is an async
    // function (27.7.4: no `prototype` property at all).
    if (closure->proto != nullptr &&
        (closure->proto->is_arrow || (closure->proto->is_async && !closure->proto->is_generator))) {
        return value::undefined();
    }
    value made = make_object();
    if (closure->proto != nullptr && closure->proto->is_generator) {
        // 27.3.1.1 / 27.4.1.1 step 7: a generator function's `prototype` is
        // an ordinary object inheriting %GeneratorPrototype% (or the async
        // one), with NO `constructor` of its own - the instances made by
        // calling the function inherit from it (make_generator).
        if (object_object * table = prototype(closure->proto->is_async ? proto_kind::async_generator
                                                                       : proto_kind::generator)) {
            static_cast<object_object *>(made.as_heap())->prototype = value::object(table);
        }
        closure->define("prototype", made, attr_writable);
        return made;
    }
    // 10.2.5 / 15.7.14: `F.prototype` is { true, false, false } on an ordinary
    // function and `F.prototype.constructor` is { true, false, true }. Both
    // were enumerable, so `constructor` turned up in `Object.keys(C.prototype)`
    // and in a `{...instance}` spread of anything that inherited it.
    static_cast<object_object *>(made.as_heap())->define("constructor", fn, attr_builtin);
    closure->define("prototype", made, attr_writable);
    return made;
}

value context::make_instance(value callee) {
    auto * instance = allocate<object_object>();
    if (callee.is_object()) {
        if (value * proto = static_cast<object_object *>(callee.as_heap())->find("prototype")) {
            instance->prototype = *proto;
        }
    } else if (callee.is_kind(heap_kind::native)) {
        // A BUILT-IN CONSTRUCTOR'S `prototype` TOO (OrdinaryCreateFromConstructor,
        // 10.1.13). Only a plain object's was read, so every native constructor
        // received an instance with no prototype and had to pick one itself -
        // and WeakMap's fallback picked Map's table, which made
        // `new WeakMap()` a Map to every method that checks its receiver.
        if (value * proto = static_cast<native_object *>(callee.as_heap())->find("prototype")) {
            instance->prototype = *proto;
        }
    } else if (callee.is_kind(heap_kind::function)) {
        instance->prototype = ensure_prototype(callee);
    }
    return value::object(instance);
}

void context::new_callee_type_error(const function_proto & fn, std::string_view origin,
                                    value callee) {
    // CATCHABLE, and that is the whole difference from construct()'s
    // raise("attempted to construct a non-function"): a page's own
    // `try { new whatever() } catch` sees this one.
    throw_error("TypeError", "`new` on " + describe_callee(fn, origin, callee));
}

value context::construct(value callee, std::span<const value> args, value new_target) {
    // `new C(...)` is Construct(C, args, C): new.target is the callee unless a
    // caller with one of its own - a proxy forwarding, Reflect.construct -
    // says otherwise.
    if (new_target.is_undefined()) { new_target = callee; }
    // 10.5.13 [[Construct]]: `new proxy(...)` runs the construct trap with
    // (target, argsArray, newTarget) and its answer must be an object; with no
    // trap, Construct(target, args, newTarget). p5.js has exactly one of these
    // and it runs at the bundle's top level: `p5.renderers['p2d-p3'] = new
    // Proxy(Renderer2D, {construct(...) {...}})`.
    if (callee.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(callee.as_heap());
        bool failed = false;
        const value trap = proxy_trap(callee, "construct", &failed);
        if (failed || throw_pending()) { return value::undefined(); }
        if (trap.is_callable()) {
            const value list = make_array();
            static_cast<array_object *>(list.as_heap())->items.assign(args.begin(), args.end());
            const value trap_args[3] = {p->target, list, new_target};
            // A THROW OUT OF THE TRAP HAS LANDED by the time call returns when
            // no native is above (context::unwinds says why throw_pending
            // cannot see it), so both are asked.
            const std::size_t before = unwinds();
            const value made = call(trap, trap_args, p->handler);
            if (throw_pending() || unwinds() != before) { return value::undefined(); }
            if (!made.is_object_like()) {
                throw_error("TypeError", "proxy [[Construct]] must return an object");
                return value::undefined();
            }
            return made;
        }
        return construct(p->target, args, new_target);
    }
    if (!callee.is_callable()) {
        raise("attempted to construct a non-function");
        return value::undefined();
    }
    if (!is_constructor(callee)) {
        throw_error("TypeError", describe_new_target(callee) + " is not a constructor");
        return value::undefined();
    }
    // OrdinaryCreateFromConstructor off NEW.TARGET (10.1.13 / 10.1.14
    // GetPrototypeFromConstructor): for a plain `new` that is the callee's own
    // `prototype`; for another new.target - Reflect.construct's, a proxy's
    // forwarding, a bound function's - its `prototype` is read through [[Get]]
    // (an accessor runs and may throw, a proxy answers for its target), and
    // one that is not an object falls back to the callee's intrinsic.
    value self = make_instance(callee);
    if (!new_target.strict_equals(callee)) {
        const std::size_t before = unwinds();
        const value proto = lookup_property(new_target, "prototype");
        if (throw_pending() || unwinds() != before) { return value::undefined(); }
        if (proto.is_object_like()) {
            static_cast<object_object *>(self.as_heap())->prototype = proto;
        }
    }
    // THE INSTANCE IS IN A C++ LOCAL FOR THE REST OF THIS FUNCTION, across a
    // field-initialiser run and a constructor body - both of which run user
    // JavaScript and can collect. Nothing rooted it, and under gc_stress that
    // is a heap-use-after-free on the object `new` is building.
    const rooted keep_instance{*this, self};
    run_field_initialisers(callee, self);
    if (callee.is_kind(heap_kind::native)) {
        auto * nat = static_cast<native_object *>(callee.as_heap());
        // A BOUND FUNCTION (Function.prototype.bind, which says how `retained`
        // is laid out): [[Construct]] is the target's, with the bound
        // arguments in front and the bound `this` ignored (10.4.1.2).
        if (const value * target = nat->find("@#BoundTargetFunction");
            target != nullptr && target->is_callable() && nat->retained.size() >= 3) {
            std::vector<value> all{nat->retained.begin() + 3, nat->retained.end()};
            all.insert(all.end(), args.begin(), args.end());
            const rooted_values keep_all{*this, all};
            // 10.4.1.2 step 5: a newTarget that was the bound function itself
            // becomes the target; any other one is passed along.
            return construct(*target, all,
                             new_target.strict_equals(callee) ? value::undefined() : new_target);
        }
        std::vector<value> copy{args.begin(), args.end()};
        // Rooted for the same reason invoke() roots a native's arguments: from
        // C++ they live in the caller's span alone.
        const rooted_values keep_args{*this, copy};
        const value saved = current_this_;
        current_this_ = self;
        const value produced = [&] {
            const native_scope pinned{*this};
            return nat->fn(*this, copy);
        }();
        current_this_ = saved;
        if (rethrow_pending()) { return value::undefined(); } // see context::call
        // A native returning a primitive looks exactly like a constructor
        // that returned nothing: `new Number(5)` evaluates to the receiver,
        // which the three wrapper constructors fill through
        // detail::wrap_primitive (values.cpp, async.cpp).
        return produced.is_object_like() ? produced : self;
    }
    // `new C()` evaluates to the new object unless the body returned one of its
    // own - the single case the spec lets override it.
    //
    // `invoke` rather than `call`, so that a constructor with a COMPILED body
    // is told it is constructing. The ABI hands that decision to
    // ct_aot_return_value, and passing false would make `new C()` on a compiled
    // constructor evaluate to whatever the body happened to return.
    // AND new.target GOES WITH IT: invoke consumes pending_new_target_ into
    // the frame, as op::construct sets fresh.new_target on its own path.
    pending_new_target_ = new_target;
    const value produced = invoke(callee, args, self, /*constructing*/ true);
    return produced.is_object_like() ? produced : self;
}

value context::construct_spread(value callee, value arg_array) {
    // NOT construct_new. op::construct_apply calls context::construct
    // WHOLESALE - no acceptance test of its own, no pending_new_target_
    // handling - and its row is emphatic that the divergence from `new C(args)`
    // is the INTERPRETER's and is reproduced rather than repaired: a spread
    // `new` reports new.target undefined where a plain one reports C.
    return construct(callee, spread_arguments(arg_array));
}

// `new callee(...)` AS THE OPCODE MEANS IT, which is not what construct() above
// means. Three differences, and a page can see all three.
//
// (1) THE ACCEPTANCE TEST AND ITS TIER. construct() tests !is_callable() FIRST
// and raise()s, which no try/catch can see; the opcode allocates, runs the field
// initialisers and only THEN throws a catchable TypeError. Delegating wholesale
// turns `try { new obj() } catch` into an engine fault.
//
// (2) NEW.TARGET. The opcode writes `fresh.new_target = callee` into the frame
// it pushes; construct() reaches a frame only through invoke(), which takes
// new.target from pending_new_target_ - undefined. SAVED AND RESTORED rather
// than set and cleared, because the opcode never CONSUMES the flag.
//
// (3) THE ORDER. The instance exists and the field initialisers have run BEFORE
// the is-a-function test, so a throw from here has already allocated an object
// with a prototype installed. Only the collector can see that, and it is the
// ordering the ABI row insists on.
//
// WHAT IT DOES NOT REPRODUCE, said rather than left to be found: a GENERATOR
// callee. invoke() answers one with make_generator; the opcode has no such test
// and runs the body as an ordinary frame, where the first `yield` raises. This
// matches construct() and `new C(...args)` instead.

value context::construct_new(value callee, std::span<const value> args,
                             const function_proto & from) {
    // A PROXY AND A NATIVE GO THE LONG WAY ROUND, exactly as the opcode sends
    // them: the construct trap and the conversion flag both live in construct(),
    // and duplicating either is what let the two disagree.
    if (callee.is_kind(heap_kind::proxy) || callee.is_kind(heap_kind::native)) {
        return construct(callee, args);
    }
    if (callee.is_kind(heap_kind::function) && !is_constructor(callee)) {
        throw_error("TypeError", describe_new_target(callee) + " is not a constructor");
        return value::undefined();
    }
    const value self = make_instance(callee);
    // ROOTED FOR THE REASON BOTH OTHER SPELLINGS ROOT IT: the instance is in a
    // C++ local while the field initialisers run user JavaScript.
    const rooted keep{*this, self};
    run_field_initialisers(callee, self);

    if (!callee.is_kind(heap_kind::function)) {
        new_callee_type_error(from, {}, callee);
        return value::undefined();
    }
    // GUARDED ON program_ ALONE, and the conjunct a first draft had was wrong:
    // invoke() bails on `program_ == nullptr` BEFORE it reaches
    // enter_compiled, so a compiled body with an aot_entry and no program would
    // have slipped past a test for both and returned a bare instance whose
    // constructor never ran.
    if (program_ == nullptr) {
        raise("no program to construct in");
        return value::undefined();
    }
    // A DEVIATION IN THE SAFE DIRECTION, not a reproduction: op::construct's
    // compiled arm calls enter_compiled with no failed_ test, so it DOES enter
    // a constructor body after a raise in a field initialiser.
    if (failed_) { return value::undefined(); }

    const value saved = pending_new_target_;
    pending_new_target_ = callee;
    const value produced = invoke(callee, args, self, /*constructing*/ true);
    pending_new_target_ = saved;
    // A CONSTRUCTOR RETURNING A PRIMITIVE EVALUATES TO ITS RECEIVER.
    return produced.is_object_like() ? produced : self;
}

void context::run_field_initialisers(value constructor, value self) {
    // NOTHING, SINCE 2026-09-12 - kept for its callers. Instance fields are the
    // COMPILER's: a base class constructor runs its own `__fields` first thing
    // in its body (10.2.2 [[Construct]] step 6.b), which a subclass's
    // `super()` reaches as well as `new` does, and a derived class runs them
    // after its `super()` returns on the object that call bound as `this`
    // (`__ctbrowser_init_fields`, emitted after every super call). Until
    // then the whole chain ran here before the body, so a base constructor's
    // `Object.preventExtensions(this)` or returned object never met the
    // derived fields, and a parent reached through super() ran its fields
    // on the wrong object or twice.
    (void)constructor;
    (void)self;
}

} // namespace ctbrowser::script
