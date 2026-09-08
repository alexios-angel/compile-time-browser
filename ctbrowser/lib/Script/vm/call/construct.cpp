// ctbrowser.script context - construction: `new` in its three spellings,
// `prototype` on demand, and class field initialisers.
//
// One of five files carved out of a 1,171-line vm/call.cpp on 2026-09-08 -
// which was itself one of four carved out of a 3,232-line vm.cpp on
// 2026-08-09. All members of `context`, declared in
// include/ctbrowser/script/vm.hpp - so they split across translation units
// with nothing to declare.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/vm.hpp>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

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
    // An arrow is not a constructor and never needs one.
    if (closure->proto != nullptr && closure->proto->is_arrow) { return value::undefined(); }
    value made = make_object();
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

value context::construct(value callee, std::span<const value> args) {
    // `new proxy(...)` runs the construct trap with (target, argsArray). p5.js
    // has exactly one of these and it runs at the bundle's top level:
    // `p5.renderers['p2d-p3'] = new Proxy(Renderer2D, {construct(...) {...}})`.
    if (callee.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(callee.as_heap());
        const value trap = proxy_trap(callee, "construct");
        if (trap.is_callable()) {
            value list = make_array();
            static_cast<array_object *>(list.as_heap())->items.assign(args.begin(), args.end());
            const value trap_args[2] = {p->target, list};
            return call(trap, trap_args, p->handler);
        }
        return construct(p->target, args);
    }
    if (!callee.is_callable()) {
        raise("attempted to construct a non-function");
        return value::undefined();
    }
    const value self = make_instance(callee);
    // THE INSTANCE IS IN A C++ LOCAL FOR THE REST OF THIS FUNCTION, across a
    // field-initialiser run and a constructor body - both of which run user
    // JavaScript and can collect. Nothing rooted it, and under gc_stress that
    // is a heap-use-after-free on the object `new` is building.
    const rooted keep_instance{*this, self};
    run_field_initialisers(callee, self);
    if (callee.is_kind(heap_kind::native)) {
        auto * nat = static_cast<native_object *>(callee.as_heap());
        std::vector<value> copy{args.begin(), args.end()};
        const value saved = current_this_;
        current_this_ = self;
        const value produced = nat->fn(*this, copy);
        current_this_ = saved;
        // A CONVERSION UNDER `new` KEEPS ITS VALUE. `new Number(5)` used to
        // evaluate to the fresh empty instance, because a native returning a
        // primitive looks exactly like a constructor that returned nothing - so
        // the 5 was thrown away and `n + 1` was "[object Object]1". Silently.
        //
        // The DEVIATION, said plainly: the spec builds a wrapper OBJECT here, so
        // `typeof new Number(5)` is "object" in a browser and "number" here.
        // Every operation on it is right, which is the opposite of what happened
        // before, and no page relies on the wrapper - every style guide in
        // existence tells you not to write this. The flag is set only on the
        // three conversions in install_globals, so a page's own constructor
        // returning a primitive still evaluates to its instance per spec.
        if (nat->find("__conversion") != nullptr) { return produced; }
        return produced.is_object_like() ? produced : self;
    }
    // `new C()` evaluates to the new object unless the body returned one of its
    // own - the single case the spec lets override it.
    //
    // `invoke` rather than `call`, so that a constructor with a COMPILED body
    // is told it is constructing. The ABI hands that decision to
    // ct_aot_return_value, and passing false would make `new C()` on a compiled
    // constructor evaluate to whatever the body happened to return.
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
    // Most-derived first, walking `C.prototype`'s own prototype back to the
    // parent's `constructor`. Depth-capped for the same reason every other
    // chain walk here is: a page can make the chain cyclic.
    std::vector<value> chain;
    value current = constructor;
    for (int depth = 0; depth < 64 && current.is_kind(heap_kind::function); ++depth) {
        chain.push_back(current);
        value * prototype = static_cast<closure_object *>(current.as_heap())->find("prototype");
        if (prototype == nullptr || !prototype->is_object()) { break; }
        const value parent_prototype =
            static_cast<object_object *>(prototype->as_heap())->prototype;
        if (!parent_prototype.is_object()) { break; }
        value * parent =
            static_cast<object_object *>(parent_prototype.as_heap())->find("constructor");
        if (parent == nullptr) { break; }
        current = *parent;
    }
    // ...then run them BASE FIRST, so a derived field that reads one the base
    // set finds it there. The spec runs a derived class's fields after its
    // super() call returns; this runs the whole chain before the constructor
    // body instead, which agrees wherever a constructor does not overwrite a
    // field it also declares.
    for (std::size_t i = chain.size(); i-- > 0;) {
        auto * klass = static_cast<closure_object *>(chain[i].as_heap());
        if (value * fields = klass->find("__fields"); fields != nullptr && fields->is_callable()) {
            call(*fields, {}, self);
        }
    }
}

} // namespace ctbrowser::script
