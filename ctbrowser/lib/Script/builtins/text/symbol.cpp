//===- ctbrowser/lib/Script/builtins/text/symbol.cpp - Symbol and BigInt --===//
//
// Script builtins: Symbol and BigInt (constructors, prototypes, Symbol.for /
// Symbol.keyFor, BigInt.asIntN / asUintN), following ECMA-262 sections 20.4
// and 21.2. Shared helpers and these functions' declarations are in
// ../internal.hpp.
//
//===----------------------------------------------------------------------===//

#include "../collections/typed_arrays/internal.hpp"
#include "../internal.hpp"

namespace ctbrowser::script::builtins_detail {

/// Installs the globals `Symbol` (constructor, prototype, well-known symbols
/// and the Symbol.for registry) and `BigInt`.
///
/// A symbol's identity is a string key that no source literal can produce, so
/// symbol-keyed properties reuse the string-keyed property machinery. The
/// well-known symbols get fixed keys, so `Symbol.iterator` means the same thing
/// to unrelated code.
void install_symbol(context & cx) {
    using detail::method;
    using detail::new_table;
    const auto counter = std::make_shared<std::uint64_t>(0);

    object_object * symbol_proto = new_table(cx);
    // thisSymbolValue (20.4.3): the symbol, or a wrapper's (`Object(sym)`,
    // detail::wrap_primitive); anything else is a TypeError.
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
    // is undefined for `Symbol()` and "" for `Symbol("")`, told apart by the
    // key (see the constructor).
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
    // hint. Attributes { writable: false, enumerable: false, configurable:
    // true }, length 1.
    {
        auto * exotic = detail::method_native(
            cx, "[Symbol.toPrimitive]", [this_symbol](context & c, std::span<value>) {
                return this_symbol(c, "Symbol.prototype[Symbol.toPrimitive]");
            });
        detail::install_arity(cx, exotic, 1);
        symbol_proto->define(CTBROWSER_SYMBOL_KEY("toPrimitive"), value::object(exotic),
                             attr_configurable);
    }
    // 20.4.3.6 @@toStringTag: non-writable, non-enumerable, configurable, and
    // what Object.prototype.toString reads for a Symbol wrapper.
    symbol_proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("Symbol"),
                         attr_configurable);
    cx.set_prototype(context::proto_kind::symbol, symbol_proto);

    // Symbol is both callable (`Symbol('x')`) and a namespace
    // (`Symbol.iterator`), so this native object also carries properties.
    auto * symbol =
        cx.allocate<native_object>("Symbol", [counter](context & c, std::span<value> a) {
            // 20.4.1.1 step 1: `new Symbol()` is a TypeError; there is no
            // Symbol wrapper to construct (Object(sym) is how one is made).
            if (detail::constructing_this(c.current_this())) {
                c.throw_error("TypeError", "Symbol is not a constructor");
                return value::undefined();
            }
            // Step 3: an undefined description stays undefined, which the
            // `description` getter can tell from "" only by the key:
            // symbol_key_prefix `<n>` carries no second colon, `<n>:` carries an
            // empty one. Anything else is ToString'd, and a symbol refuses.
            const bool described = !arg_at(a, 0).is_undefined();
            if (described && !stringable_arg(c, a[0])) { return value::undefined(); }
            const std::string description = described ? c.to_string(a[0]) : std::string{};
            if (c.throw_pending()) { return value::undefined(); }
            return value::object(c.allocate<symbol_object>(ctbrowser::make_symbol(
                (*counter)++,
                described ? std::optional<std::string_view>{description} : std::nullopt)));
        });
    // Symbol keeps [[Construct]]: IsConstructor(Symbol) is true, and
    // `new Symbol()` throws from the body (20.4.1).
    //
    // Each well-known symbol is defined non-writable, non-enumerable and
    // non-configurable (20.4.2); enumerable would put `iterator` in
    // `Object.keys(Symbol)`. Its [[Description]] is e.g. "Symbol.iterator"
    // (the table in 6.1.5.1), which is what key_value rebuilds too.
    const auto well_known = [&](const char * name, ctbrowser::well_known_symbol kind) {
        symbol->define(
            name,
            value::object(cx.allocate<symbol_object>(ctbrowser::make_well_known_symbol(kind))),
            attr_none);
    };
#define CTBROWSER_WELL_KNOWN_SYMBOL(name) well_known(#name, ctbrowser::well_known_symbol::name);
#include "ctbrowser/core/well_known_symbols.def"
#undef CTBROWSER_WELL_KNOWN_SYMBOL
    // The Symbol.for registry. It must return the same symbol object for the
    // same key, because `===` compares identity. The keys live in a C++ vector;
    // the symbols live in a script array (`registry`), which mark_object traces,
    // so the registry stays rooted as it grows. A `value` held only in a lambda
    // capture is invisible to the precise collector and would be freed while
    // still listed. `for` retains the one array handle.
    const auto keys = std::make_shared<std::vector<std::string>>();
    const value registry = cx.make_array();
    auto * symbol_for =
        cx.allocate<native_object>("for", [keys, registry](context & c, std::span<value> a) {
            // ToString(key): a missing key is undefined, so "undefined".
            const std::string d = a.empty() ? std::string{"undefined"} : str_at(c, a, 0);
            if (c.throw_pending()) { return value::undefined(); }
            auto * held = static_cast<array_object *>(registry.as_heap());
            for (std::size_t i = 0; i < keys->size() && i < held->items.size(); ++i) {
                if ((*keys)[i] == d) { return held->items[i]; }
            }
            const value made = value::object(
                c.allocate<symbol_object>(d, std::string{registered_symbol_key_prefix} + d));
            keys->push_back(d);
            held->items.push_back(made);
            return made;
        });
    symbol_for->retained.push_back(registry);
    symbol_for->is_constructor = false;
    detail::install_arity(cx, symbol_for, 1);
    symbol->define("for", value::object(symbol_for), attr_builtin);
    // Symbol.keyFor: the key a registered symbol was made under, or undefined
    // for one that never went through the registry. It is read off the symbol's
    // own key, which only `for` mints with that prefix: the
    // GlobalSymbolRegistry belongs to the agent (20.4.2.2), so a symbol a
    // ShadowRealm's `Symbol.for` made (equal to this realm's by key, as every
    // symbol is) has its key here too.
    auto * symbol_key_for =
        cx.allocate<native_object>("keyFor", [](context & c, std::span<value> a) {
            const value want = arg_at(a, 0);
            if (!want.is_kind(heap_kind::symbol)) {
                c.throw_error("TypeError", "Symbol.keyFor: argument is not a symbol");
                return value::undefined();
            }
            const std::string & key = static_cast<symbol_object *>(want.as_heap())->key;
            if (!key.starts_with(registered_symbol_key_prefix)) { return value::undefined(); }
            return c.string(key.substr(registered_symbol_key_prefix.size()));
        });
    symbol_key_for->is_constructor = false;
    detail::install_arity(cx, symbol_key_for, 1);
    symbol->define("keyFor", value::object(symbol_key_for), attr_builtin);
    // Symbol.prototype, and Symbol.length = 0 (20.4.2.10).
    detail::constant(symbol, "prototype", value::object(symbol_proto));
    link_constructor(cx, symbol_proto, "Symbol", 0, value::object(symbol));
    cx.define_global("Symbol", value::object(symbol));

    // --- BigInt --------------------------------------------------------------
    // BigInt converts a primitive; Object(bigint) creates its wrapper.
    object_object * bigint_proto = new_table(cx);
    // thisBigIntValue (21.2.3): the BigInt, or a wrapper's (`Object(1n)`,
    // detail::wrap_primitive); anything else is a TypeError.
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
    bigint_proto->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("BigInt"),
                         attr_configurable); // 21.2.3.5
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
    // ToBigInt(bigint), then the value modulo 2^bits, signed for asIntN.
    // to_bigint is bigint.hpp's, shared with the BigInt typed arrays and
    // DataView; it rejects a Number with a TypeError, where the constructor
    // above converts one.
    const auto wrap_bits = [](context & c, std::span<value> a, bool is_signed) {
        double bits = 0;
        if (!to_index(c, arg_at(a, 0), bits)) { return value::undefined(); }
        std::optional<bigint> n{bigint{}};
        if (!to_bigint(c, arg_at(a, 1), *n)) { return value::undefined(); }
        if (bits == 0) { return value::object(c.allocate<bigint_object>(bigint{0})); }
        // A width past the value's own is the value itself (or, negative and
        // unsigned, 2^bits + n): computed without materialising 2^(2^53).
        // Widths above 1e9 are capped; such a width throws unless the value
        // already fits.
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
