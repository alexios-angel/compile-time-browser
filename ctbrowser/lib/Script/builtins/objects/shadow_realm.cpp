//===- objects/shadow_realm.cpp - ShadowRealm builtin -----------*- C++ -*-===//
//
// ShadowRealm, from the TC39 ShadowRealm proposal (stage 2.7); section numbers
// below are that proposal's. A ShadowRealm is a realm with intrinsics of its
// own (create_intrinsic_realm, builtins.cpp) that code can be evaluated in and
// imported into. Only primitives and functions cross between it and its
// creator: an object is a TypeError, and a function is wrapped, becoming a new
// function of the receiving realm whose [[Call]] carries its arguments over
// the same boundary the other way. So the shadow realm's `Array.prototype` can
// be rewritten without the page seeing it, and neither side ever holds an
// object of the other's.
//
// An instance's [[ShadowRealm]] slot holds the realm's global object, which is
// how context::realm_of finds the realm again.
//
//===----------------------------------------------------------------------===//

#include "../async/promise_reactions.hpp"
#include "../collections/iterator_internal.hpp"
#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

constexpr std::string_view shadow_realm_key = CTBROWSER_PRIVATE_KEY("ShadowRealm");

/// A TypeError belonging to `realm`: its prototype is that realm's
/// %TypeError.prototype%, whichever realm is running when it is thrown.
value type_error_in(context & c, realm_record & realm, std::string message) {
    const context::realm_scope in{c, realm};
    return c.make_error("TypeError", std::move(message));
}

void throw_type_error_in(context & c, realm_record & realm, std::string message) {
    c.throw_value(type_error_in(c, realm, std::move(message)));
}

/// ValidateShadowRealmObject (3.1.4): `self`'s [[ShadowRealm]], or null with
/// the TypeError thrown.
realm_record * shadow_realm_of(context & c, value self, const char * method) {
    if (self.is_object()) {
        if (const value * global =
                static_cast<object_object *>(self.as_heap())->find(shadow_realm_key);
            global != nullptr) {
            if (realm_record * realm = c.realm_of(*global)) { return realm; }
        }
    }
    c.throw_error("TypeError", std::string{"ShadowRealm.prototype."} + method +
                                   " requires that 'this' be a ShadowRealm");
    return nullptr;
}

bool wrap_value(context & c, realm_record & to, realm_record & from, realm_record & errors, value v,
                value & out);

/// GetFunctionRealm (ECMA-262 7.3.24) of a wrapped function's target: a
/// closure's own [[Realm]]; for a proxy, its target's (a revoked proxy is a
/// TypeError); anything else, the realm it came from. Null with the TypeError
/// thrown.
realm_record * function_realm(context & c, realm_record & caller, value target,
                              realm_record & from) {
    while (target.is_kind(heap_kind::proxy)) {
        auto * proxy = static_cast<proxy_object *>(target.as_heap());
        if (proxy->handler.is_null()) {
            throw_type_error_in(c, caller, "the wrapped function's target is a revoked proxy");
            return nullptr;
        }
        target = proxy->target;
    }
    if (target.is_kind(heap_kind::function)) {
        if (realm_record * own = static_cast<closure_object *>(target.as_heap())->realm) {
            return own;
        }
    }
    return &from;
}

/// CopyNameAndLength(F, Target) (3.1.3) with argCount 0: `length` and `name`
/// are read off the target and defined non-writable, non-enumerable and
/// configurable. False when a read threw.
bool copy_name_and_length(context & c, value wrapped, value target) {
    const detail::completion copied = detail::fenced(c, [wrapped, target](context & cc) {
        auto * fn = static_cast<native_object *>(wrapped.as_heap());
        double length = 0;
        const bool has_length = cc.has_own_property(target, "length");
        if (cc.throw_pending()) { return value::undefined(); }
        if (has_length) {
            const value held = cc.lookup_property(target, "length");
            if (cc.throw_pending()) { return value::undefined(); }
            if (held.is_number()) {
                const double n = held.as_number();
                if (n == std::numeric_limits<double>::infinity()) {
                    length = n;
                } else if (n != -std::numeric_limits<double>::infinity()) {
                    // ToIntegerOrInfinity, then max(L - argCount, 0).
                    length = std::isnan(n) ? 0.0 : std::max(std::trunc(n), 0.0) + 0.0;
                }
            }
        }
        fn->define("length", value::number(length), attr_configurable);
        const value name = cc.lookup_property(target, "name");
        if (cc.throw_pending()) { return value::undefined(); }
        fn->define("name", name.is_string() ? name : cc.string(""), attr_configurable);
        return value::undefined();
    });
    return !copied.threw;
}

/// WrappedFunctionCreate (3.1.2): a function of `caller`'s realm, whose
/// [[Prototype]] is that realm's %Function.prototype% and whose [[Call]] (2.1)
/// wraps `this` and every argument into the target's realm, calls the target
/// there, and wraps the result back. Any throw on the way, the target's own
/// included, becomes a fresh TypeError of `caller`'s: nothing the other realm
/// threw crosses. Creation fails with a TypeError of `errors`' when the
/// target's `length` or `name` cannot be read.
bool wrapped_function_create(context & c, realm_record & caller, realm_record & from,
                             realm_record & errors, value target, value & out) {
    native_object * wrapped = detail::method_native(c, "", native_fn{});
    const value made = value::object(wrapped);
    const context::rooted keep{c, made};
    wrapped->retained.push_back(target);
    const auto & prototypes = caller.intrinsics->prototypes;
    if (object_object * fn_proto =
            prototypes[static_cast<std::size_t>(context::proto_kind::function)]) {
        wrapped->proto_link = value::object(fn_proto);
    }
    realm_record * const caller_realm = &caller;
    realm_record * const from_realm = &from;
    // `wrapped` is alive for the duration of its own call; the pointer is to
    // the object being called.
    wrapped->fn = [wrapped, caller_realm, from_realm](context & cc, std::span<value> a) {
        const value callee = wrapped->retained.front();
        realm_record * target_realm = function_realm(cc, *caller_realm, callee, *from_realm);
        if (target_realm == nullptr) { return value::undefined(); }
        // A wrapper made here is this native's own allocation, and pinned
        // for the call (native_scope); a primitive is the caller's.
        std::vector<value> args(a.size(), value::undefined());
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!wrap_value(cc, *target_realm, *caller_realm, *caller_realm, a[i], args[i])) {
                return value::undefined();
            }
        }
        value self = value::undefined();
        if (!wrap_value(cc, *target_realm, *caller_realm, *caller_realm, cc.current_this(), self)) {
            return value::undefined();
        }
        bool threw = false;
        value thrown = value::undefined();
        value result = value::undefined();
        {
            // The target runs in its realm: a closure's frame would go there
            // anyway, and a native has no frame to take it.
            const context::realm_scope there{cc, *target_realm};
            result = cc.call_fenced(callee, args, self, threw, thrown);
        }
        if (cc.failed()) { return value::undefined(); }
        if (threw) {
            throw_type_error_in(cc, *caller_realm, "a wrapped function threw");
            return value::undefined();
        }
        value back = value::undefined();
        if (!wrap_value(cc, *caller_realm, *target_realm, *caller_realm, result, back)) {
            return value::undefined();
        }
        return back;
    };
    if (!copy_name_and_length(c, made, target)) {
        if (!c.failed()) {
            throw_type_error_in(c, errors,
                                "a function could not be wrapped: reading its "
                                "`length` or `name` threw");
        }
        return false;
    }
    out = made;
    return true;
}

/// GetWrappedValue(to, value) (3.1.5), for a value that came from `from`: a
/// primitive as it is, a function wrapped as `to`'s, any other object a
/// TypeError. Every error belongs to `errors`, the realm that asked; that is
/// `to` except for a wrapped function's arguments, which are carried the other
/// way (2.1 step 3's note). False with the error thrown.
bool wrap_value(context & c, realm_record & to, realm_record & from, realm_record & errors, value v,
                value & out) {
    if (v.is_callable()) { return wrapped_function_create(c, to, from, errors, v, out); }
    if (v.is_object_like()) {
        throw_type_error_in(c, errors,
                            "a ShadowRealm can hand over only primitives and "
                            "functions");
        return false;
    }
    out = v;
    return true;
}

} // namespace

/// Installs the `ShadowRealm` constructor and its prototype methods.
void install_shadow_realm(context & cx) {
    using detail::method;

    object_object * proto = detail::new_table(cx);
    // 3.2.1 ShadowRealm ( ): only under `new`, and every instance a realm of
    // its own.
    auto * ctor = cx.allocate<native_object>("ShadowRealm", [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!detail::constructing_this(self)) {
            c.throw_error("TypeError", "Constructor ShadowRealm requires 'new'");
            return value::undefined();
        }
        const context::rooted keep{c, self};
        realm_record & made = create_intrinsic_realm(c);
        static_cast<object_object *>(self.as_heap())
            ->define(shadow_realm_key, made.global_this, attr_none);
        return self;
    });
    detail::install_arity(cx, ctor, 0);
    detail::constant(ctor, "prototype", value::object(proto));
    proto->define("constructor", value::object(ctor), attr_builtin);

    // 3.4.1 ShadowRealm.prototype.evaluate ( sourceText ): PerformShadowRealmEval
    // (3.1.1). The source runs as an indirect eval in the other realm (its own
    // lexical scope, its `var`s on that global object); a source that does not
    // parse is the caller's SyntaxError, and a throw during the evaluation is
    // a TypeError of the caller's.
    method(cx, proto, "evaluate", 1, [](context & c, std::span<value> a) {
        realm_record * const shadow = shadow_realm_of(c, c.current_this(), "evaluate");
        if (shadow == nullptr) { return value::undefined(); }
        const value source = arg_at(a, 0);
        if (!source.is_string()) {
            c.throw_error("TypeError", "ShadowRealm.prototype.evaluate: the source is not a "
                                       "string");
            return value::undefined();
        }
        realm_record & caller = c.current_realm();
        program compiled = compiler::compile_for_eval(c.to_string(source));
        if (!compiled.ok) {
            c.throw_error("SyntaxError", compiled.error);
            return value::undefined();
        }
        const program & kept = c.own_program(std::move(compiled));
        detail::completion ran;
        {
            const context::realm_scope there{c, *shadow};
            ran = detail::fenced(c, [&kept](context & cc) { return cc.run_nested(kept); });
        }
        if (c.failed()) { return value::undefined(); }
        if (ran.threw) {
            throw_type_error_in(c, caller, "the ShadowRealm's evaluation threw");
            return value::undefined();
        }
        value out = value::undefined();
        if (!wrap_value(c, caller, *shadow, caller, ran.result, out)) { return value::undefined(); }
        return out;
    });

    // 3.4.2 ShadowRealm.prototype.importValue ( specifier, exportName ):
    // ShadowRealmImportValue (3.1.6). The module is loaded and evaluated in
    // the other realm, and a promise of the caller's settles with the named
    // export wrapped, or rejects with a TypeError of the caller's: for a load
    // or an evaluation that failed (%ThrowTypeError% is the rejection
    // handler), for an export that is not there, and for one that cannot
    // cross.
    method(cx, proto, "importValue", 2, [](context & c, std::span<value> a) {
        realm_record * const shadow = shadow_realm_of(c, c.current_this(), "importValue");
        if (shadow == nullptr) { return value::undefined(); }
        const std::string specifier = c.to_string(arg_at(a, 0));
        if (c.throw_pending() || c.failed()) { return value::undefined(); }
        const value export_name = arg_at(a, 1);
        if (!export_name.is_string()) {
            c.throw_error("TypeError", "ShadowRealm.prototype.importValue: the export name is "
                                       "not a string");
            return value::undefined();
        }
        realm_record * const caller = &c.current_realm();
        detail::promise::capability cap;
        if (!detail::promise::new_capability(c, detail::promise::intrinsic_promise(c), cap)) {
            return value::undefined();
        }
        const value held[3] = {cap.promise, cap.resolve, cap.reject};
        const context::rooted_values keep_cap{c, held};
        value inner = value::undefined();
        {
            const context::realm_scope there{c, *shadow};
            inner = c.dynamic_import(c.string(specifier), std::string{});
        }
        if (c.failed()) { return value::undefined(); }
        const context::rooted keep_inner{c, inner};
        const std::string name = c.to_string(export_name);
        // ExportGetter: the namespace's own `name`, wrapped for the caller.
        native_object * on_ok = detail::method_native(
            c, "", [name, caller, shadow](context & cc, std::span<value> args) {
                const value ns = arg_at(args, 0);
                const bool has = cc.has_own_property(ns, name);
                if (cc.throw_pending()) { return value::undefined(); }
                if (!has) {
                    throw_type_error_in(cc, *caller,
                                        "the module has no export named '" + name + "'");
                    return value::undefined();
                }
                const value got = cc.lookup_property(ns, name);
                if (cc.throw_pending()) { return value::undefined(); }
                value out = value::undefined();
                if (!wrap_value(cc, *caller, *shadow, *caller, got, out)) {
                    return value::undefined();
                }
                return out;
            });
        detail::install_arity(c, on_ok, 1);
        const value on_ok_value = value::object(on_ok);
        const context::rooted keep_ok{c, on_ok_value};
        const value on_err = c.throw_type_error();
        detail::promise::perform_then(c, inner, on_ok_value, on_err, cap);
        return cap.promise;
    });

    // 3.4.3 ShadowRealm.prototype [ @@toStringTag ]: non-writable,
    // non-enumerable, configurable.
    proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("ShadowRealm"), attr_configurable);
    cx.define_global("ShadowRealm", value::object(ctor));
}

} // namespace ctbrowser::script::builtins_detail
