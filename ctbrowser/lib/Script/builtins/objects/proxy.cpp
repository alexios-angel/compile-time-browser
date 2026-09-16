// ctbrowser.script builtins - Proxy and Reflect.
//
// One of five files carved out of a 1,429-line builtins/objects.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp; what only this
// directory shares is in internal.hpp beside this.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

using detail::key_filter;
using detail::own_property_names;
using detail::prototype_of;
using detail::valid_descriptor;

// `Proxy` and `Reflect`. p5.js has three proxies, and one of them runs at the
// bundle's top level - `p5.renderers['p2d-p3'] = new Proxy(Renderer2D,
// {construct(...) {...}})` - so the bundle could not finish loading without it.
//
// Three traps: `get`, `has` and `construct`, which are the three p5 uses. A
// trap that is not implemented is not silently skipped - the operation falls
// through to the target, which is what an absent trap means anyway. The traps
// that ARE missing (set, deleteProperty, ownKeys, apply and the rest) behave
// the same way, so a page using one gets the target's behaviour rather than a
// wrong answer; that is a real gap and it is named here rather than discovered.
void install_proxy(context & cx) {
    using detail::method;
    using detail::new_table;

    // ProxyCreate, 10.5.14: both halves must be objects. A revoked proxy has
    // null in both slots (28.2.2.1.1), and every operation on one is a
    // TypeError - context::proxy_trap answers no trap for a null handler and
    // the fall-through then refuses the null target.
    const auto proxy_create = [](context & c, std::span<value> a) {
        if (!arg_at(a, 0).is_object_like() || !arg_at(a, 1).is_object_like()) {
            c.throw_error("TypeError",
                          "Cannot create proxy with a non-object as target or handler");
            return value::undefined();
        }
        return value::object(c.allocate<proxy_object>(a[0], a[1]));
    };
    auto * proxy_ctor =
        cx.allocate<native_object>("Proxy", [proxy_create](context & c, std::span<value> a) {
            // 28.2.1.1 step 1: `Proxy(...)` without `new` is a TypeError.
            if (!detail::constructing_this(c.current_this())) {
                c.throw_error("TypeError", "Constructor Proxy requires 'new'");
                return value::undefined();
            }
            return proxy_create(c, a);
        });
    proxy_ctor->define("length", value::number(2), attr_configurable);
    proxy_ctor->define("name", cx.string("Proxy"), attr_configurable);
    // 28.2.2.1 Proxy.revocable: { proxy, revoke }, and `revoke` reaches its
    // proxy through its OWN property table rather than a captured value,
    // because a value captured by a native lambda is not a collector root
    // (docs/script.md).
    method(cx, proxy_ctor, "revocable", 2, [proxy_create](context & c, std::span<value> a) {
        const value proxy = proxy_create(c, a);
        if (!proxy.is_kind(heap_kind::proxy)) { return value::undefined(); }
        const context::rooted keep{c, proxy};
        native_object * revoke = c.allocate<native_object>("", native_fn{});
        revoke->is_constructor = false;
        revoke->fn = [revoke](context &, std::span<value>) {
            // `revoke` is alive for the duration of its own call; the pointer
            // is to the object being called.
            if (value * held = revoke->find(primitive_slot_key); held != nullptr) {
                if (held->is_kind(heap_kind::proxy)) {
                    auto * p = static_cast<proxy_object *>(held->as_heap());
                    p->target = value::null();
                    p->handler = value::null();
                }
                *held = value::null();
            }
            return value::undefined();
        };
        detail::install_arity(c, revoke, 0);
        revoke->define(primitive_slot_key, proxy, attr_none);
        object_object * out = detail::new_table(c);
        out->set("proxy", proxy);
        out->set("revoke", value::object(revoke));
        return value::object(out);
    });
    cx.define_global("Proxy", value::object(proxy_ctor));

    // Reflect is the un-trapped operation a handler calls to do the default
    // thing - `Reflect.get(t, k)` inside a `get` trap is how a proxy adds
    // behaviour instead of replacing it.
    object_object * reflect = new_table(cx);
    // 28.1: every Reflect function REFUSES a non-object target with a
    // TypeError - the one place the namespace is stricter than Object - and
    // coerces its key with ToPropertyKey (a symbol passes through as its
    // key, see symbol_object).
    const auto target_of = [](context & c, std::span<value> a, const char * name) {
        if (arg_at(a, 0).is_object_like()) { return true; }
        c.throw_error("TypeError", std::string{"Reflect."} + name + " called on non-object");
        return false;
    };
    // ToPropertyKey, 7.1.19: ToPrimitive(hint String) then the key - an
    // object's `toString` runs, and can throw.
    const auto key_of = [](context & c, value raw, std::string & out) {
        value primitive = raw;
        if (raw.is_object_like()) {
            if (!c.to_primitive_hint(raw, "string", primitive)) { return false; }
        }
        out = c.to_string(primitive);
        return !c.throw_pending();
    };
    // 28.1.6 Reflect.get, WITH ITS RECEIVER: an accessor found on the chain is
    // called on `receiver`, which is the whole reason the third argument
    // exists. A proxy target answers through its trap (context::lookup_index),
    // which takes no receiver; so does the two-argument form.
    method(cx, reflect, "get", 2, [target_of, key_of](context & c, std::span<value> a) {
        if (!target_of(c, a, "get")) { return value::undefined(); }
        std::string key;
        if (!key_of(c, arg_at(a, 1), key)) { return value::undefined(); }
        if (a.size() < 3 || a[0].is_kind(heap_kind::proxy)) {
            return c.lookup_index(a[0], c.string(key));
        }
        const value receiver = a[2];
        value walk = a[0];
        for (int hops = 0; hops < 10000 && walk.is_object_like(); ++hops) {
            if (walk.is_kind(heap_kind::proxy)) { return c.lookup_index(walk, c.string(key)); }
            context::property_descriptor found;
            if (c.own_property(walk, key, found)) {
                if (!found.is_accessor()) { return found.held; }
                if (!found.getter.is_callable()) { return value::undefined(); }
                return c.call(found.getter, {}, receiver);
            }
            if (c.throw_pending()) { return value::undefined(); }
            walk = prototype_of(c, walk);
        }
        return value::undefined();
    });
    // 28.1.13 Reflect.set: OrdinarySet (10.1.9.2) with a receiver, answering
    // FALSE where a plain assignment is silently refused - a non-writable
    // slot, an accessor with no setter, a non-extensible receiver, a receiver
    // that is not an object at all.
    method(cx, reflect, "set", 3, [target_of, key_of](context & c, std::span<value> a) {
        if (!target_of(c, a, "set")) { return value::undefined(); }
        std::string key;
        if (!key_of(c, arg_at(a, 1), key)) { return value::undefined(); }
        const value v = arg_at(a, 2);
        const value receiver = a.size() > 3 ? a[3] : a[0];
        value walk = a[0];
        context::property_descriptor own;
        bool found = false;
        for (int hops = 0; hops < 10000 && walk.is_object_like(); ++hops) {
            if (walk.is_kind(heap_kind::proxy)) {
                // The proxy's own [[Set]] - its trap - with the store path
                // this engine has, which carries no receiver.
                c.clear_store_rejected();
                c.store_index(walk, c.string(key), v);
                return value::boolean(!c.throw_pending());
            }
            found = c.own_property(walk, key, own);
            if (c.throw_pending()) { return value::undefined(); }
            if (found) { break; }
            walk = prototype_of(c, walk);
        }
        if (found && own.is_accessor()) {
            if (!own.setter.is_callable()) { return value::boolean(false); }
            const value args[1] = {v};
            (void)c.call(own.setter, args, receiver);
            return value::boolean(!c.throw_pending());
        }
        if (found && !own.writable) { return value::boolean(false); }
        if (!receiver.is_object_like()) { return value::boolean(false); }
        context::property_descriptor existing;
        if (c.own_property(receiver, key, existing)) {
            if (existing.is_accessor() || !existing.writable) { return value::boolean(false); }
            context::property_descriptor wanted;
            wanted.has_value = true;
            wanted.held = v;
            return value::boolean(c.define_own_property(receiver, key, wanted));
        }
        if (c.throw_pending()) { return value::undefined(); }
        context::property_descriptor fresh;
        fresh.has_value = fresh.has_writable = fresh.has_enumerable = fresh.has_configurable = true;
        fresh.held = v;
        fresh.writable = fresh.enumerable = fresh.configurable = true;
        return value::boolean(c.define_own_property(receiver, key, fresh));
    });
    // HasProperty, 7.3.11 - not "reads as something other than undefined".
    // `Reflect.has({x: undefined}, 'x')` was false, and so was every accessor
    // whose getter returns undefined. context::has_property is the operator
    // `in` uses and is the same question.
    method(cx, reflect, "has", 2, [target_of, key_of](context & c, std::span<value> a) {
        if (!target_of(c, a, "has")) { return value::undefined(); }
        std::string key;
        if (!key_of(c, arg_at(a, 1), key)) { return value::undefined(); }
        return value::boolean(c.has_property(a[0], key));
    });
    // CreateListFromArrayLike, 7.3.20: any object with a `length`, read
    // through [[Get]] - a non-object is the TypeError of step 2.
    const auto list_from = [](context & c, value list, std::vector<value> & out,
                              const char * name) {
        if (!list.is_object_like()) {
            c.throw_error("TypeError",
                          std::string{"Reflect."} + name + ": arguments list must be an object");
            return false;
        }
        if (list.is_array() && detail::dense_array_this(list) != nullptr &&
            !list.is_kind(heap_kind::proxy)) {
            out = static_cast<array_object *>(list.as_heap())->items;
            return true;
        }
        const double n = detail::array_like_length(c, list);
        if (c.throw_pending()) { return false; }
        if (!detail::generic_walk_ok(c, n)) { return false; }
        for (double i = 0; i < n; i += 1.0) {
            out.push_back(detail::element_at(c, list, i));
            if (c.throw_pending()) { return false; }
        }
        return true;
    };
    // 28.1.2: target and newTarget must both be constructors, and the
    // argument list must be an object (CreateListFromArrayLike step 2).
    method(cx, reflect, "construct", 2, [list_from](context & c, std::span<value> a) {
        if (!is_constructor(arg_at(a, 0))) {
            c.throw_error("TypeError", "Reflect.construct: target is not a constructor");
            return value::undefined();
        }
        if (a.size() > 2 && !is_constructor(a[2])) {
            c.throw_error("TypeError", "Reflect.construct: newTarget is not a constructor");
            return value::undefined();
        }
        std::vector<value> args;
        if (!list_from(c, arg_at(a, 1), args, "construct")) { return value::undefined(); }
        const context::rooted_values keep{c, args};
        const value made = c.construct(a[0], args);
        // GetPrototypeFromConstructor off newTarget (10.1.14): context::construct
        // takes none, so an ordinary object a built-in made is re-parented
        // afterwards - what `Reflect.construct(Error, [], NewTarget)` observes.
        if (a.size() > 2 && !a[2].strict_equals(a[0]) && made.is_object() && !c.throw_pending()) {
            const value proto = c.lookup_property(a[2], "prototype");
            if (proto.is_object_like()) {
                static_cast<object_object *>(made.as_heap())->prototype = proto;
            }
        }
        return made;
    });
    // 28.1.1 Reflect.apply: a non-callable target is a TypeError BEFORE the
    // list is read.
    method(cx, reflect, "apply", 3, [list_from](context & c, std::span<value> a) {
        if (!arg_at(a, 0).is_callable()) {
            c.throw_error("TypeError", "Reflect.apply: target is not a function");
            return value::undefined();
        }
        std::vector<value> args;
        if (!list_from(c, arg_at(a, 2), args, "apply")) { return value::undefined(); }
        const context::rooted_values keep{c, args};
        return c.call(a[0], args, arg_at(a, 1));
    });
    method(cx, reflect, "ownKeys", 1, [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        const context::rooted keep{c, out};
        // The UNFILTERED walk, over any receiver: `Reflect.ownKeys` reports
        // symbols as well as names, and an array's indices as well as a plain
        // object's keys. It answered `[]` for everything that was not an
        // object_object.
        if (!arg_at(a, 0).is_object_like()) {
            c.throw_error("TypeError", "Reflect.ownKeys called on non-object");
            return value::undefined();
        }
        for (const std::string & key : own_property_names(c, a[0], key_filter::all)) {
            result->items.push_back(detail::key_value(c, key));
        }
        return out;
    });
    // The un-throwing halves of Object.defineProperty and friends: Reflect
    // ANSWERS FALSE where Object throws, which is the whole difference between
    // the two namespaces.
    method(cx, reflect, "defineProperty", 3, [target_of, key_of](context & c, std::span<value> a) {
        // FALSE is only for the VALIDATION step. A target that is not an object
        // and a descriptor that is malformed are both TypeErrors here exactly
        // as they are for Object.defineProperty (28.1.3 steps 1 and 3) - the
        // difference between the two namespaces is what a REFUSED but
        // well-formed define does, and that is the boolean below.
        if (!target_of(c, a, "defineProperty")) { return value::undefined(); }
        std::string key;
        if (!key_of(c, arg_at(a, 1), key)) { return value::undefined(); }
        if (!arg_at(a, 2).is_object_like()) {
            c.throw_error("TypeError", "Property description must be an object");
            return value::boolean(false);
        }
        const context::property_descriptor wanted = c.to_property_descriptor(a[2]);
        if (!valid_descriptor(c, wanted)) { return value::boolean(false); }
        return value::boolean(c.define_own_property(a[0], key, wanted));
    });
    method(cx, reflect, "getOwnPropertyDescriptor", 2,
           [target_of, key_of](context & c, std::span<value> a) {
               if (!target_of(c, a, "getOwnPropertyDescriptor")) { return value::undefined(); }
               std::string key;
               if (!key_of(c, arg_at(a, 1), key)) { return value::undefined(); }
               context::property_descriptor found;
               if (!c.own_property(a[0], key, found)) { return value::undefined(); }
               return c.from_property_descriptor(found);
           });
    method(cx, reflect, "deleteProperty", 2, [target_of, key_of](context & c, std::span<value> a) {
        if (!target_of(c, a, "deleteProperty")) { return value::undefined(); }
        std::string key;
        if (!key_of(c, arg_at(a, 1), key)) { return value::undefined(); }
        return value::boolean(c.delete_own_property(a[0], key));
    });
    // 28.1.9, and 10.5.3 for a proxy: the `isExtensible` trap's boolean,
    // which must agree with the target's.
    method(cx, reflect, "isExtensible", 1, [target_of](context & c, std::span<value> a) {
        if (!target_of(c, a, "isExtensible")) { return value::undefined(); }
        if (a[0].is_kind(heap_kind::proxy)) {
            auto * p = static_cast<proxy_object *>(a[0].as_heap());
            if (!p->handler.is_object_like()) {
                c.throw_error("TypeError",
                              "Cannot perform 'isExtensible' on a proxy that has been revoked");
                return value::undefined();
            }
            const value trap = c.lookup_property(p->handler, "isExtensible");
            if (c.throw_pending()) { return value::undefined(); }
            if (trap.is_callable()) {
                const value args[1] = {p->target};
                const bool answered = context::truthy(c.call(trap, args, p->handler));
                if (c.throw_pending()) { return value::undefined(); }
                if (answered != c.is_extensible(p->target)) {
                    c.throw_error("TypeError", "'isExtensible' on proxy: trap result does not "
                                               "reflect extensibility of proxy target");
                    return value::undefined();
                }
                return value::boolean(answered);
            }
            if (!trap.is_nullish()) {
                c.throw_error("TypeError", "proxy trap 'isExtensible' is not a function");
                return value::undefined();
            }
        }
        return value::boolean(c.is_extensible(a[0]));
    });
    // 28.1.10: a proxy's trap answers the boolean (10.5.4); an ordinary
    // object always says true.
    method(cx, reflect, "preventExtensions", 1, [target_of](context & c, std::span<value> a) {
        if (!target_of(c, a, "preventExtensions")) { return value::undefined(); }
        if (a[0].is_kind(heap_kind::proxy)) {
            auto * p = static_cast<proxy_object *>(a[0].as_heap());
            const value trap = c.proxy_trap(a[0], "preventExtensions");
            if (trap.is_callable()) {
                const value args[1] = {p->target};
                const bool ok = context::truthy(c.call(trap, args, p->handler));
                if (c.throw_pending()) { return value::undefined(); }
                if (ok && c.is_extensible(p->target)) {
                    c.throw_error("TypeError",
                                  "'preventExtensions' on proxy: trap returned truish but the "
                                  "proxy target is extensible");
                    return value::undefined();
                }
                return value::boolean(ok);
            }
        }
        c.prevent_extensions(a[0]);
        return value::boolean(true);
    });
    // THE SAME [[GetPrototypeOf]] Object.getPrototypeOf answers with. It read
    // `object_object::prototype` directly, so the two disagreed about every
    // function, every primitive and every plain object.
    method(cx, reflect, "getPrototypeOf", 1, [](context & c, std::span<value> a) {
        const value of = arg_at(a, 0);
        // 28.1.8 step 1: Reflect REFUSES a non-object rather than coercing it,
        // which is the one place it is stricter than Object.
        if (!of.is_object_like()) {
            c.throw_error("TypeError", "Reflect.getPrototypeOf called on non-object");
            return value::undefined();
        }
        return prototype_of(c, of);
    });
    method(cx, reflect, "setPrototypeOf", 2, [](context & c, std::span<value> a) {
        if (!arg_at(a, 0).is_object_like()) {
            c.throw_error("TypeError", "Reflect.setPrototypeOf called on non-object");
            return value::boolean(false);
        }
        const value proto = arg_at(a, 1);
        if (!proto.is_object_like() && !proto.is_null()) {
            c.throw_error("TypeError", "Object prototype may only be an Object or null");
            return value::boolean(false);
        }
        if (a[0].is_kind(heap_kind::proxy) &&
            !static_cast<proxy_object *>(a[0].as_heap())->handler.is_object_like()) {
            c.throw_error("TypeError",
                          "Cannot perform 'setPrototypeOf' on a proxy that has been revoked");
            return value::undefined();
        }
        return value::boolean(detail::set_prototype_of(c, a[0], proto));
    });
    reflect->define("@@toStringTag", cx.string("Reflect"), attr_configurable); // 28.1.14
    cx.define_global("Reflect", value::object(reflect));
}

} // namespace ctbrowser::script::builtins_detail
