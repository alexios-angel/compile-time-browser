// ctbrowser.script builtins - Symbol.
//
// One of four files carved out of a 1,333-line builtins/text.cpp on 2026-09-08
// - which was itself one of five carved out of builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in ../internal.hpp; nothing is shared between
// these four alone, so there is no second header.

#include "../collections/typed_arrays/internal.hpp"
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
    const auto counter = std::make_shared<std::uint64_t>(0);

    object_object * symbol_proto = new_table(cx);
    // thisSymbolValue (20.4.3): the symbol, or a wrapper's (`Object(sym)`,
    // detail::wrap_primitive) - anything else is a TypeError.
    const auto this_symbol = [](context & c, const char * method) {
        value self = c.current_this();
        if (value * slot = primitive_slot(self); slot != nullptr) { self = *slot; }
        if (self.is_kind(heap_kind::symbol)) { return self; }
        c.throw_error("TypeError", std::string{method} + " requires that 'this' be a Symbol");
        return value::undefined();
    };
    method(cx, symbol_proto, "toString", 0, [this_symbol](context & c, std::span<value>) {
        const value self = this_symbol(c, "Symbol.prototype.toString");
        if (!self.is_kind(heap_kind::symbol)) { return value::undefined(); }
        return c.string(static_cast<symbol_object *>(self.as_heap())->to_string());
    });
    method(cx, symbol_proto, "valueOf", 0, [this_symbol](context & c, std::span<value>) {
        return this_symbol(c, "Symbol.prototype.valueOf");
    });
    // 20.4.3.2 get Symbol.prototype.description: the [[Description]], which
    // is UNDEFINED for `Symbol()` and "" for `Symbol("")` - told apart by the
    // key, see the constructor.
    {
        auto * getter = detail::method_native(
            cx, "get description", [this_symbol](context & c, std::span<value>) {
                const value self = this_symbol(c, "Symbol.prototype.description");
                if (!self.is_kind(heap_kind::symbol)) { return value::undefined(); }
                const auto description =
                    static_cast<symbol_object *>(self.as_heap())->description_value();
                return description ? c.string(std::string{*description}) : value::undefined();
            });
        detail::install_arity(cx, getter, 0);
        symbol_proto->define_accessor("description", value::object(getter), value::undefined(),
                                      attr_configurable);
    }
    // 20.4.3.5 Symbol.prototype[@@toPrimitive]: thisSymbolValue, whatever the
    // hint. { false, false, true }, length 1.
    {
        auto * exotic = detail::method_native(
            cx, "[Symbol.toPrimitive]", [this_symbol](context & c, std::span<value>) {
                return this_symbol(c, "Symbol.prototype[Symbol.toPrimitive]");
            });
        detail::install_arity(cx, exotic, 1);
        symbol_proto->define("@@toPrimitive", value::object(exotic), attr_configurable);
    }
    // 20.4.3.6: { false, false, true }, and what Object.prototype.toString
    // reads for a Symbol wrapper.
    symbol_proto->define("@@toStringTag", cx.string("Symbol"), attr_configurable);
    cx.set_prototype(context::proto_kind::symbol, symbol_proto);

    // Callable AND a namespace: `Symbol('x')` and `Symbol.iterator` are both
    // ordinary uses, which is why a native carries a property table now.
    auto * symbol =
        cx.allocate<native_object>("Symbol", [counter](context & c, std::span<value> a) {
            // 20.4.1.1 step 1: `new Symbol()` is a TypeError - there is no
            // Symbol wrapper to construct (Object(sym) is how one is made).
            if (detail::constructing_this(c.current_this())) {
                c.throw_error("TypeError", "Symbol is not a constructor");
                return value::undefined();
            }
            // Step 3: an undefined description stays UNDEFINED, which the
            // `description` getter can tell from "" only by the key:
            // `@@sym:<n>` carries no second colon, `@@sym:<n>:` carries an
            // empty one. Anything else is ToString'd, and a symbol refuses.
            const bool described = !arg_at(a, 0).is_undefined();
            if (described && !stringable_arg(c, a[0])) { return value::undefined(); }
            const std::string description = described ? c.to_string(a[0]) : std::string{};
            if (c.throw_pending()) { return value::undefined(); }
            return value::object(c.allocate<symbol_object>(ctbrowser::make_symbol(
                (*counter)++,
                described ? std::optional<std::string_view>{description} : std::nullopt)));
        });
    // It KEEPS [[Construct]] (20.4.1: `new Symbol()` is a TypeError from the
    // body, not "not a constructor" - IsConstructor(Symbol) is true).
    // { false, false, false } - a well-known symbol is not writable and not
    // configurable (20.4.2), and enumerable would put `iterator` in
    // `Object.keys(Symbol)`. Its [[Description]] is "Symbol.iterator" (the
    // table in 6.1.5.1), which is what key_value rebuilds too.
    const auto well_known = [&](const char * name, ctbrowser::well_known_symbol kind) {
        symbol->define(
            name,
            value::object(cx.allocate<symbol_object>(ctbrowser::make_well_known_symbol(kind))),
            attr_none);
    };
#define CTBROWSER_WELL_KNOWN_SYMBOL(name) well_known(#name, ctbrowser::well_known_symbol::name);
#include "ctbrowser/core/well_known_symbols.def"
#undef CTBROWSER_WELL_KNOWN_SYMBOL
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
    const auto keys = std::make_shared<std::vector<std::string>>();
    const value registry = cx.make_array();
    auto * symbol_for =
        cx.allocate<native_object>("for", [keys, registry](context & c, std::span<value> a) {
            const std::string d = str_at(c, a, 0);
            if (c.throw_pending()) { return value::undefined(); }
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
    symbol_for->is_constructor = false;
    detail::install_arity(cx, symbol_for, 1);
    symbol->define("for", value::object(symbol_for), attr_builtin);
    // The inverse: the key a registered symbol was made under, or undefined for
    // one that never went through the registry.
    auto * symbol_key_for =
        cx.allocate<native_object>("keyFor", [keys, registry](context & c, std::span<value> a) {
            const value want = arg_at(a, 0);
            if (!want.is_kind(heap_kind::symbol)) {
                c.throw_error("TypeError", "Symbol.keyFor: argument is not a symbol");
                return value::undefined();
            }
            auto * held = static_cast<array_object *>(registry.as_heap());
            for (std::size_t i = 0; i < keys->size() && i < held->items.size(); ++i) {
                if (held->items[i] == want) { return c.string((*keys)[i]); }
            }
            return value::undefined();
        });
    symbol_key_for->retained.push_back(registry);
    symbol_key_for->is_constructor = false;
    detail::install_arity(cx, symbol_key_for, 1);
    symbol->define("keyFor", value::object(symbol_key_for), attr_builtin);
    // `Symbol.prototype` is reachable from the constructor, like every other
    // built-in's - a page that walks it found undefined. 20.4.2.10: length 0.
    detail::constant(symbol, "prototype", value::object(symbol_proto));
    link_constructor(cx, symbol_proto, "Symbol", 0, value::object(symbol));
    cx.define_global("Symbol", value::object(symbol));

    // --- BigInt --------------------------------------------------------------
    // BigInt converts a primitive; Object(bigint) creates its wrapper.
    object_object * bigint_proto = new_table(cx);
    // thisBigIntValue (21.2.3): the BigInt, or a wrapper's (`Object(1n)`,
    // detail::wrap_primitive) - anything else is a TypeError.
    const auto this_bigint = [](context & c, const char * method) {
        value self = c.current_this();
        if (value * slot = primitive_slot(self); slot != nullptr) { self = *slot; }
        if (self.is_kind(heap_kind::bigint)) { return self; }
        c.throw_error("TypeError", std::string{method} + " requires that 'this' be a BigInt");
        return value::undefined();
    };
    method(cx, bigint_proto, "toString", 0, [this_bigint](context & c, std::span<value> a) {
        const value self = this_bigint(c, "BigInt.prototype.toString");
        if (!self.is_kind(heap_kind::bigint)) { return value::undefined(); }
        double radix = 10;
        if (!arg_at(a, 0).is_undefined() && !to_integer_or_infinity(c, a[0], radix)) {
            return value::undefined();
        }
        if (radix < 2 || radix > 36) {
            c.throw_error("RangeError", "toString() radix must be between 2 and 36");
            return value::undefined();
        }
        return c.string(bigint_to_string(static_cast<bigint_object *>(self.as_heap())->digits,
                                         static_cast<int>(radix)));
    });
    method(cx, bigint_proto, "valueOf", 0, [this_bigint](context & c, std::span<value>) {
        return this_bigint(c, "BigInt.prototype.valueOf");
    });
    bigint_proto->define("@@toStringTag", cx.string("BigInt"), attr_configurable); // 21.2.3.5
    cx.set_prototype(context::proto_kind::bigint, bigint_proto);

    auto * bigint_ctor = cx.allocate<native_object>("BigInt", [](context & c, std::span<value> a) {
        value prim = arg_at(a, 0);
        if (prim.is_object_like() && !c.to_primitive_hint(prim, "number", prim)) {
            return value::undefined();
        }
        if (prim.is_kind(heap_kind::bigint)) { return prim; }
        bigint converted{};
        if (prim.is_number()) {
            // NumberToBigInt permits integers; ToBigInt refuses every Number.
            const std::optional<bigint> parsed = bigint_from_double(prim.as_number());
            if (!parsed) {
                c.throw_error("RangeError", "Cannot convert a non-integer to a BigInt");
                return value::undefined();
            }
            converted = *parsed;
        } else if (!to_bigint(c, prim, converted)) {
            return value::undefined();
        }
        return value::object(c.allocate<bigint_object>(std::move(converted)));
    });
    // 21.2.2.1-2 BigInt.asIntN / asUintN (bits, bigint): ToIndex(bits), then
    // ToBigInt(bigint) - bigint.hpp's, shared with the BigInt typed arrays
    // and DataView; a Number is a TypeError there where the constructor
    // above converts it - then the value modulo 2^bits, signed for asIntN.
    const auto wrap_bits = [](context & c, std::span<value> a, bool is_signed) {
        double bits = 0;
        if (!to_index(c, arg_at(a, 0), bits)) { return value::undefined(); }
        std::optional<bigint> n{bigint{}};
        if (!to_bigint(c, arg_at(a, 1), *n)) { return value::undefined(); }
        if (bits == 0) { return value::object(c.allocate<bigint_object>(bigint{0})); }
        // A width past the value's own is the value itself (or, negative and
        // unsigned, 2^bits + n): computed without materialising 2^(2^53).
        const auto width = static_cast<unsigned>(std::min(bits, 1.0e9));
        const unsigned used = *n == 0 ? 0
                                      : static_cast<unsigned>(boost::multiprecision::msb(
                                            boost::multiprecision::abs(*n))) +
                                            1;
        bigint out = *n;
        if (bits > 1.0e9 && !(is_signed ? used < width : *n >= 0)) {
            c.throw_error("RangeError", "BigInt.asIntN/asUintN: the width is too large");
            return value::undefined();
        }
        if (is_signed ? used >= width : (*n < 0 || used > width)) {
            const bigint modulus = bigint{1} << width;
            out = ((*n % modulus) + modulus) % modulus;
            if (is_signed && out >= (modulus >> 1)) { out -= modulus; }
        }
        return value::object(c.allocate<bigint_object>(std::move(out)));
    };
    method(cx, bigint_ctor, "asIntN", 2,
           [wrap_bits](context & c, std::span<value> a) { return wrap_bits(c, a, true); });
    method(cx, bigint_ctor, "asUintN", 2,
           [wrap_bits](context & c, std::span<value> a) { return wrap_bits(c, a, false); });
    detail::constant(bigint_ctor, "prototype", value::object(bigint_proto));
    link_constructor(cx, bigint_proto, "BigInt", 1, value::object(bigint_ctor));
    cx.define_global("BigInt", value::object(bigint_ctor));
}

} // namespace ctbrowser::script::builtins_detail
