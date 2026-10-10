//===- vm/call/construct.cpp - Object construction with new -----*- C++ -*-===//
//
// Construction: `new` through construct(), construct_spread() and
// construct_new() (the entry for ahead-of-time compiled code), `prototype`
// created on demand, and is_constructor. All functions here are members of
// `context`, declared in include/ctbrowser/script/vm.hpp; the other call
// paths live in the sibling files of vm/call/.
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

namespace {
/// The callee's name for "X is not a constructor", or "the value" when it has
/// none.
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
        // A method (`{ m() {} }`) is not a constructor either, but
        // function_proto has no flag for methods, so they are accepted here;
        // fixing that needs the compiler to record it.
        return p == nullptr || !(p->is_arrow || p->is_generator || p->is_async);
    }
    if (v.is_kind(heap_kind::proxy)) {
        const auto * p = static_cast<const proxy_object *>(v.as_heap());
        return p->constructor || is_constructor(p->target);
    }
    return false;
}

/// Returns \p fn's `prototype` property, creating it on first use. Ordinary
/// and generator functions need one (`new F() instanceof F` is the pattern
/// transpilers such as Babel's `_classCallCheck` rely on); arrows and async
/// functions get none, and undefined is returned for them and for
/// non-closures. It is made lazily because most closures are never
/// constructed and an object per closure is a real cost.
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
        // one), with no `constructor` of its own; the instances made by
        // calling the function inherit from it (make_generator).
        if (object_object * table = prototype(closure->proto->is_async ? proto_kind::async_generator
                                                                       : proto_kind::generator)) {
            static_cast<object_object *>(made.as_heap())->prototype = value::object(table);
        }
        closure->define("prototype", made, attr_writable);
        return made;
    }
    // 10.2.5 / 15.7.14: `F.prototype` is writable, non-enumerable and
    // non-configurable; `F.prototype.constructor` is writable, non-enumerable
    // and configurable, so it stays out of `Object.keys` and object spread.
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
        // A built-in constructor's `prototype` too (OrdinaryCreateFromConstructor,
        // 10.1.13). Otherwise each native would have to pick a prototype
        // itself, and a wrong pick (WeakMap choosing Map's) makes the instance
        // pass the wrong receiver checks.
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
    // The TypeError for `new` on a non-constructor, naming where the callee
    // came from; shared by op::construct and construct_new.
    throw_error("TypeError", "`new` on " + describe_callee(fn, origin, callee));
}

value context::construct(value callee, std::span<const value> args, value new_target) {
    // A Proxy only has [[Construct]] when its target does. Reject before
    // reading its trap, and use the catchable TypeError for every value.
    if (!is_constructor(callee)) {
        throw_error("TypeError", describe_new_target(callee) + " is not a constructor");
        return value::undefined();
    }
    // `new C(...)` is Construct(C, args, C): new.target is the callee unless a
    // caller with one of its own - a proxy forwarding, Reflect.construct -
    // says otherwise.
    if (new_target.is_undefined()) { new_target = callee; }
    // 10.5.13 [[Construct]]: `new proxy(...)` runs the construct trap with
    // (target, argsArray, newTarget) and its answer must be an object; with no
    // trap, Construct(target, args, newTarget).
    if (callee.is_kind(heap_kind::proxy)) {
        auto * p = static_cast<proxy_object *>(callee.as_heap());
        bool failed = false;
        const value trap = proxy_trap(callee, "construct", &failed);
        if (failed || throw_pending()) { return value::undefined(); }
        if (trap.is_callable()) {
            const value list = make_array();
            static_cast<array_object *>(list.as_heap())->items.assign(args.begin(), args.end());
            const value trap_args[3] = {p->target, list, new_target};
            // With no native frame above, a throw from the trap has already
            // unwound by the time call() returns and throw_pending() cannot
            // see it (see context::unwinds), so check both.
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
    // A proxy's prototype getter can revoke it, dropping the only other root
    // of the target constructor while this call is still using it.
    const rooted keep_callee{*this, callee};
    if (callee.is_kind(heap_kind::native)) {
        auto * nat = static_cast<native_object *>(callee.as_heap());
        // Bound [[Construct]] forwards before reading any prototype (10.4.1.2).
        if (const value * target = nat->find(CTBROWSER_PRIVATE_KEY("BoundTargetFunction"));
            target != nullptr && nat->retained.size() >= 3) {
            std::vector<value> all{nat->retained.begin() + 3, nat->retained.end()};
            all.insert(all.end(), args.begin(), args.end());
            const rooted_values keep_all{*this, all};
            return construct(*target, all,
                             new_target.strict_equals(callee) ? value::undefined() : new_target);
        }
    }
    // OrdinaryCreateFromConstructor from new.target (10.1.13 / 10.1.14
    // GetPrototypeFromConstructor): for a plain `new` that is the callee's own
    // `prototype`; for another new.target (Reflect.construct's, a proxy's
    // forwarding, a bound function's) its `prototype` is read through [[Get]]
    // (an accessor runs and may throw, a proxy answers for its target), and
    // one that is not an object falls back to the callee's intrinsic.
    value self = make_instance(callee);
    // The new.target `prototype` getter and the constructor body can both
    // trigger a collection.
    const rooted keep_instance{*this, self};
    if (!new_target.strict_equals(callee)) {
        const std::size_t before = unwinds();
        const value proto = lookup_property(new_target, "prototype");
        if (throw_pending() || unwinds() != before) { return value::undefined(); }
        if (proto.is_object_like()) {
            static_cast<object_object *>(self.as_heap())->prototype = proto;
        } else if (!callee.is_kind(heap_kind::function) ||
                   !static_cast<closure_object *>(callee.as_heap())->derived_constructor) {
            // GetFunctionRealm (7.3.24) unwraps bound functions and proxies
            // without invoking traps; a revoked proxy throws at this step.
            // There is only one realm, so the walk exists only to raise that
            // TypeError; the instance keeps the prototype make_instance gave
            // it, and `target` is otherwise unused.
            // ponytail: super() still calls its base on the existing receiver;
            // move derived prototype lookup there when it uses Construct.
            value target = new_target;
            for (;;) {
                if (target.is_kind(heap_kind::proxy)) {
                    const auto * proxy = static_cast<const proxy_object *>(target.as_heap());
                    if (proxy->handler.is_null()) {
                        throw_error("TypeError", "Cannot get the realm of a revoked proxy");
                        return value::undefined();
                    }
                    target = proxy->target;
                } else if (target.is_kind(heap_kind::native)) {
                    const value * bound = static_cast<native_object *>(target.as_heap())
                                              ->find(CTBROWSER_PRIVATE_KEY("BoundTargetFunction"));
                    if (bound == nullptr) { break; }
                    target = *bound;
                } else {
                    break;
                }
            }
        }
    }
    run_field_initialisers(callee, self);
    if (callee.is_kind(heap_kind::native)) {
        auto * nat = static_cast<native_object *>(callee.as_heap());
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
        if (rethrow_pending()) { return value::undefined(); } // See context::call.
        // A native returning a primitive looks exactly like a constructor
        // that returned nothing: `new Number(5)` evaluates to the receiver,
        // which the three wrapper constructors fill through
        // detail::wrap_primitive (values.cpp, async.cpp).
        return produced.is_object_like() ? produced : self;
    }
    // `new C()` evaluates to the new object unless the body returned one of its
    // own, the single case the spec lets override it.
    //
    // `invoke` rather than `call`, so a constructor with a compiled body knows
    // it is constructing: ct_aot_return_value picks the result from that
    // flag, and without it `new C()` would evaluate to whatever the body
    // returned. invoke also moves pending_new_target_ into the new frame, as
    // op::construct does with fresh.new_target.
    pending_new_target_ = new_target;
    const value produced = invoke(callee, args, self, /*constructing*/ true);
    return produced.is_object_like() ? produced : self;
}

value context::construct_spread(value callee, value arg_array) {
    // op::construct_apply shares generic dispatch with the AOT spread helper.
    return construct(callee, spread_arguments(arg_array));
}

/// `new callee(args)` for ahead-of-time compiled code that uses the VM's boxed
/// values: returns the result instead of writing a register. Closures follow
/// op::construct (pending_new_target_ saved and restored, TypeErrors naming
/// the call site in \p from); natives and proxies go through construct().
value context::construct_new(value callee, std::span<const value> args,
                             const function_proto & from) {
    // Send proxies and natives through construct(), as op::construct does, so
    // the construct trap and the native constructing path exist in one place.
    if (callee.is_kind(heap_kind::proxy) || callee.is_kind(heap_kind::native)) {
        return construct(callee, args);
    }
    if (callee.is_kind(heap_kind::function) && !is_constructor(callee)) {
        throw_error("TypeError", describe_new_target(callee) + " is not a constructor");
        return value::undefined();
    }
    const value self = make_instance(callee);
    // Rooted, as construct() roots it: the instance lives only in a C++ local
    // while the constructor body runs JavaScript that can trigger a
    // collection.
    const rooted keep{*this, self};
    run_field_initialisers(callee, self);

    if (!callee.is_kind(heap_kind::function)) {
        new_callee_type_error(from, {}, callee);
        return value::undefined();
    }
    // Check program_ alone: invoke() returns early when program_ is null,
    // before it reaches enter_compiled, so even a compiled body with an
    // aot_entry would not run and `new` would return an instance whose
    // constructor never ran.
    if (program_ == nullptr) {
        raise("no program to construct in");
        return value::undefined();
    }
    // Stop if an earlier step raised. op::construct's compiled path has no
    // failed_ check here, so this deliberately differs from it, in the safer
    // direction.
    if (failed_) { return value::undefined(); }

    const value saved = pending_new_target_;
    pending_new_target_ = callee;
    const value produced = invoke(callee, args, self, /*constructing*/ true);
    pending_new_target_ = saved;
    // A constructor returning a primitive evaluates to its receiver.
    return produced.is_object_like() ? produced : self;
}

void context::run_field_initialisers(value constructor, value self) {
    // Does nothing; kept because op::construct, construct() and
    // construct_new() still call it. Instance fields are initialised by compiled code instead: a
    // base-class constructor runs its own `__fields` at the start of its body
    // (10.2.2 [[Construct]] step 6.b), and a derived class runs
    // `__ctbrowser_init_fields` on `this` after each `super()` returns.
    // Running them here, before the body, would hide derived fields from a
    // base constructor's `Object.preventExtensions(this)` or returned object.
    (void)constructor;
    (void)self;
}

} // namespace ctbrowser::script
