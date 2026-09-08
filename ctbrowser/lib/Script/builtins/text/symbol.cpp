// ctbrowser.script builtins - Symbol.
//
// One of four files carved out of a 1,333-line builtins/text.cpp on 2026-09-08
// - which was itself one of five carved out of builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in ../internal.hpp; nothing is shared between
// these four alone, so there is no second header.

#include "../internal.hpp"

namespace ctbrowser::script::builtins_detail {

// `Symbol`. p5.js needs it to EXIST before anything else - the bundled zod
// calls `Symbol(...)` at load, and that one undefined global stopped the whole
// bundle with nothing but "attempted to call a non-function" to say so.
//
// A symbol's identity is a STRING KEY no source literal collides with, so a
// symbol-keyed property works through the existing string-keyed machinery
// without touching the object model. The well-known ones get fixed keys, which
// is what lets `Symbol.iterator` mean the same thing to two different pieces of
// code that never met.
void install_symbol(context & cx) {
    using detail::method;
    using detail::new_table;
    auto counter = std::make_shared<std::uint64_t>(0);

    object_object * symbol_proto = new_table(cx);
    method(cx, symbol_proto, "toString", 0, [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!self.is_kind(heap_kind::symbol)) { return c.string("Symbol()"); }
        return c.string("Symbol(" + static_cast<symbol_object *>(self.as_heap())->description +
                        ")");
    });
    cx.set_prototype(context::proto_kind::symbol, symbol_proto);

    // Callable AND a namespace: `Symbol('x')` and `Symbol.iterator` are both
    // ordinary uses, which is why a native carries a property table now.
    auto * symbol =
        cx.allocate<native_object>("Symbol", [counter](context & c, std::span<value> a) {
            const std::string description = a.empty() ? std::string{} : c.to_string(a[0]);
            const std::string key =
                std::string{symbol_key_prefix} + std::to_string((*counter)++) + ":" + description;
            return value::object(c.allocate<symbol_object>(description, key));
        });
    // { false, false, false } - a well-known symbol is not writable and not
    // configurable (20.4.2), and enumerable would put `iterator` in
    // `Object.keys(Symbol)`.
    const auto well_known = [&](const char * name, const char * key) {
        symbol->define(name, value::object(cx.allocate<symbol_object>(name, key)), attr_none);
    };
    well_known("iterator", "@@iterator");
    well_known("asyncIterator", "@@asyncIterator");
    well_known("hasInstance", "@@hasInstance");
    well_known("toPrimitive", "@@toPrimitive");
    well_known("toStringTag", "@@toStringTag");
    // `Symbol.match` IS CONSULTED, which is why it is here and its four
    // siblings are not. IsRegExp (7.2.8) reads it to decide whether
    // `includes`, `startsWith` and `endsWith` must refuse their argument, so
    // defining it gives a page the documented way to say "this object is a
    // pattern" or "this RegExp is not one" - and test262's
    // `return-abrupt-from-searchstring-regexp-test.js` asserts exactly that.
    //
    // @@replace, @@search, @@split and @@matchAll are DELIBERATELY ABSENT.
    // Nothing here dispatches through them - the string methods drive a
    // pattern's `exec` directly - and a well-known symbol that no operation
    // reads is a promise the engine does not keep: a page would install a
    // custom @@replace, see it ignored, and have nothing to say why.
    well_known("match", "@@match");
    // A REGISTRY, and it has to hold the SYMBOLS rather than mint a fresh one
    // per call. Two `Symbol.for('x')` produced two objects with the same key,
    // and `===` compares identity - so the one guarantee the registry exists to
    // give, that a key looked up twice is the same symbol, was the one it did
    // not keep. The shared_ptr is captured by both `for` and `keyFor`, which is
    // what lets the second answer questions about the first.
    //
    // AND THE SYMBOLS THEMSELVES HAVE TO LIVE IN A HEAP OBJECT, not in the
    // capture. They were `value`s inside the shared_ptr, and a `value` in a C++
    // capture is invisible to the precise collector: the KEYS survived a
    // collection - they are std::strings, which nothing collects - and every
    // symbol they named was freed while the registry went on listing it.
    // `Symbol.for('KEY')` then handed back a pointer into a freed cell, and
    // once another symbol was allocated over it, `Symbol.for('KEY').description`
    // came back as that other symbol's description. Measured, not reasoned:
    // 'recycled'.
    //
    // So the keys stay in C++, where they are safe by construction, and the
    // symbols move into an ARRAY, which mark_object traces like any other -
    // which means the registry roots itself AS IT GROWS, where a snapshot into
    // native_object::retained would only root what was registered by the time
    // the native was built. Both natives retain the one array handle, so
    // neither can be left reading the other's freed entries.
    auto keys = std::make_shared<std::vector<std::string>>();
    const value registry = cx.make_array();
    auto * symbol_for =
        cx.allocate<native_object>("for", [keys, registry](context & c, std::span<value> a) {
            const std::string d = a.empty() ? std::string{} : c.to_string(a[0]);
            auto * held = static_cast<array_object *>(registry.as_heap());
            for (std::size_t i = 0; i < keys->size() && i < held->items.size(); ++i) {
                if ((*keys)[i] == d) { return held->items[i]; }
            }
            const value made = value::object(c.allocate<symbol_object>(d, "@@for:" + d));
            keys->push_back(d);
            held->items.push_back(made);
            return made;
        });
    symbol_for->retained.push_back(registry);
    symbol->define("for", value::object(symbol_for), attr_builtin);
    // The inverse: the key a registered symbol was made under, or undefined for
    // one that never went through the registry.
    auto * symbol_key_for =
        cx.allocate<native_object>("keyFor", [keys, registry](context & c, std::span<value> a) {
            const value want = arg_at(a, 0);
            auto * held = static_cast<array_object *>(registry.as_heap());
            for (std::size_t i = 0; i < keys->size() && i < held->items.size(); ++i) {
                if (held->items[i] == want) { return c.string((*keys)[i]); }
            }
            return value::undefined();
        });
    symbol_key_for->retained.push_back(registry);
    symbol->define("keyFor", value::object(symbol_key_for), attr_builtin);
    // `Symbol.prototype` is reachable from the constructor, like every other
    // built-in's - a page that walks it found undefined.
    detail::constant(symbol, "prototype", value::object(symbol_proto));
    cx.define_global("Symbol", value::object(symbol));

    // --- BigInt --------------------------------------------------------------
    // A CONVERSION, like Number and String and unlike Array: `new BigInt(1)` is
    // a TypeError in the specification because there is no wrapper object to
    // make. This engine does not box at all, so calling it is the only form.
    object_object * bigint_proto = new_table(cx);
    method(cx, bigint_proto, "toString", 0, [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!self.is_kind(heap_kind::bigint)) { return c.string("0"); }
        const int radix = a.empty() || a[0].is_undefined()
                              ? 10
                              : std::clamp(static_cast<int>(context::to_number(a[0])), 2, 36);
        return c.string(
            bigint_to_string(static_cast<bigint_object *>(self.as_heap())->digits, radix));
    });
    method(cx, bigint_proto, "valueOf", 0,
           [](context & c, std::span<value>) { return c.current_this(); });
    cx.set_prototype(context::proto_kind::bigint, bigint_proto);

    auto * bigint_ctor = cx.allocate<native_object>("BigInt", [](context & c, std::span<value> a) {
        const value v = arg_at(a, 0);
        if (v.is_kind(heap_kind::bigint)) { return v; }
        if (v.is_boolean()) {
            return value::object(c.allocate<bigint_object>(bigint{v.as_boolean() ? 1 : 0}));
        }
        if (v.is_string()) {
            // A STRING THAT IS NOT AN INTEGER IS A SyntaxError, not NaN -
            // there is no BigInt NaN to return, so the conversion has to
            // refuse rather than degrade.
            const std::optional<bigint> parsed =
                bigint_from_string(static_cast<string_object *>(v.as_heap())->text);
            if (!parsed) {
                c.throw_error("SyntaxError", "Cannot convert this string to a BigInt");
                return value::undefined();
            }
            return value::object(c.allocate<bigint_object>(*parsed));
        }
        // A NON-INTEGRAL Number is a RangeError - `BigInt(1.5)` refuses
        // rather than truncating, because losing the fraction silently is
        // the failure this type exists to make impossible.
        const std::optional<bigint> parsed = bigint_from_double(context::to_number(v));
        if (!parsed) {
            c.throw_error("RangeError", "Cannot convert a non-integer to a BigInt");
            return value::undefined();
        }
        return value::object(c.allocate<bigint_object>(*parsed));
    });
    detail::constant(bigint_ctor, "prototype", value::object(bigint_proto));
    link_constructor(cx, bigint_proto, "BigInt", 1, value::object(bigint_ctor));
    cx.define_global("BigInt", value::object(bigint_ctor));
}

} // namespace ctbrowser::script::builtins_detail
