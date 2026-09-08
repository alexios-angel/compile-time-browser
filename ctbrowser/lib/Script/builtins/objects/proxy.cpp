// ctbrowser.script builtins - Proxy and Reflect.
//
// One of five files carved out of a 1,429-line builtins/objects.cpp on
// 2026-09-08 - which was itself one of five carved out of builtins.cpp on
// 2026-08-09. Everything shared - the argument helpers, namespace detail, and
// these functions' declarations - is in ../internal.hpp; what only this
// directory shares is in internal.hpp beside this.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

using detail::descriptor_object;
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

    cx.define_native("Proxy", [](context & c, std::span<value> a) {
        return value::object(c.allocate<proxy_object>(arg_at(a, 0), arg_at(a, 1)));
    });

    // Reflect is the un-trapped operation a handler calls to do the default
    // thing - `Reflect.get(t, k)` inside a `get` trap is how a proxy adds
    // behaviour instead of replacing it.
    object_object * reflect = new_table(cx);
    method(cx, reflect, "get", 2, [](context & c, std::span<value> a) {
        return c.lookup_index(arg_at(a, 0), arg_at(a, 1));
    });
    method(cx, reflect, "set", 3, [](context & c, std::span<value> a) {
        if (arg_at(a, 0).is_object()) {
            static_cast<object_object *>(a[0].as_heap())
                ->set(c.to_string(arg_at(a, 1)), arg_at(a, 2));
        }
        return value::boolean(true);
    });
    // HasProperty, 7.3.11 - not "reads as something other than undefined".
    // `Reflect.has({x: undefined}, 'x')` was false, and so was every accessor
    // whose getter returns undefined. context::has_property is the operator
    // `in` uses and is the same question.
    method(cx, reflect, "has", 2, [](context & c, std::span<value> a) {
        return value::boolean(c.has_property(arg_at(a, 0), arg_at(a, 1)));
    });
    method(cx, reflect, "construct", 2, [](context & c, std::span<value> a) {
        std::vector<value> args;
        if (arg_at(a, 1).is_array()) { args = static_cast<array_object *>(a[1].as_heap())->items; }
        return c.construct(arg_at(a, 0), args);
    });
    method(cx, reflect, "apply", 3, [](context & c, std::span<value> a) {
        std::vector<value> args;
        if (arg_at(a, 2).is_array()) { args = static_cast<array_object *>(a[2].as_heap())->items; }
        return c.call(arg_at(a, 0), args, arg_at(a, 1));
    });
    method(cx, reflect, "ownKeys", 1, [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        const context::rooted keep{c, out};
        // The UNFILTERED walk, over any receiver: `Reflect.ownKeys` reports
        // symbols as well as names, and an array's indices as well as a plain
        // object's keys. It answered `[]` for everything that was not an
        // object_object.
        for (const std::string & key : own_property_names(c, arg_at(a, 0), key_filter::all)) {
            result->items.push_back(c.string(key));
        }
        return out;
    });
    // The un-throwing halves of Object.defineProperty and friends: Reflect
    // ANSWERS FALSE where Object throws, which is the whole difference between
    // the two namespaces.
    method(cx, reflect, "defineProperty", 3, [](context & c, std::span<value> a) {
        // FALSE is only for the VALIDATION step. A target that is not an object
        // and a descriptor that is malformed are both TypeErrors here exactly
        // as they are for Object.defineProperty (28.1.3 steps 1 and 3) - the
        // difference between the two namespaces is what a REFUSED but
        // well-formed define does, and that is the boolean below.
        if (!arg_at(a, 0).is_object_like()) {
            c.throw_error("TypeError", "Reflect.defineProperty called on non-object");
            return value::boolean(false);
        }
        const std::string key = c.to_string(arg_at(a, 1));
        if (!arg_at(a, 2).is_object_like()) {
            c.throw_error("TypeError", "Property description must be an object");
            return value::boolean(false);
        }
        const context::property_descriptor wanted = read_descriptor(c, a[2]);
        if (!valid_descriptor(c, wanted)) { return value::boolean(false); }
        return value::boolean(c.define_own_property(a[0], key, wanted));
    });
    method(cx, reflect, "getOwnPropertyDescriptor", 2, [](context & c, std::span<value> a) {
        context::property_descriptor found;
        if (!c.own_property(arg_at(a, 0), c.to_string(arg_at(a, 1)), found)) {
            return value::undefined();
        }
        return value::object(descriptor_object(c, found));
    });
    method(cx, reflect, "deleteProperty", 2, [](context & c, std::span<value> a) {
        return value::boolean(c.delete_own_property(arg_at(a, 0), c.to_string(arg_at(a, 1))));
    });
    method(cx, reflect, "isExtensible", 1, [](context & c, std::span<value> a) {
        return value::boolean(c.is_extensible(arg_at(a, 0)));
    });
    method(cx, reflect, "preventExtensions", 1, [](context & c, std::span<value> a) {
        c.prevent_extensions(arg_at(a, 0));
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
    cx.define_global("Reflect", value::object(reflect));
}

} // namespace ctbrowser::script::builtins_detail
