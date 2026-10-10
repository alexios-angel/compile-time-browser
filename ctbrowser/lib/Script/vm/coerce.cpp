//===- ctbrowser/lib/Script/vm/coerce.cpp - Type coercions ------*- C++ -*-===//
//
// The coercions (ToNumber, ToString, ToPrimitive), the binary operators built
// on them, and the equality and relational comparisons. All functions here are
// members of `context`, declared in include/ctbrowser/script/vm.hpp.
//
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <ctbrowser/core/number_format.hpp>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/vm.hpp>

namespace ctbrowser::script {

/// ToBoolean (7.1.2).
bool context::truthy(value v) {
    if (v.is_boolean()) { return v.as_boolean(); }
    if (v.is_nullish()) { return false; }
    if (v.is_number()) {
        const double d = v.as_number();
        return d != 0 && !std::isnan(d);
    }
    if (v.is_string()) { return !static_cast<string_object *>(v.as_heap())->text.empty(); }
    // 0n is falsy and every other bigint is truthy, exactly as for Number.
    if (v.is_kind(heap_kind::bigint)) {
        return static_cast<bigint_object *>(v.as_heap())->digits != 0;
    }
    return true; // Every other object is truthy.
}

/// ToNumber for a primitive \p v; an object gives NaN without calling into
/// script (see to_number_value for the full conversion).
double context::to_number(value v) {
    if (v.is_number()) { return v.as_number(); }
    if (v.is_boolean()) { return v.as_boolean() ? 1 : 0; }
    if (v.is_null()) { return 0; }
    if (v.is_undefined()) { return std::nan(""); }
    if (v.is_string()) {
        // string_to_number, not std::stod: stod reads LC_NUMERIC for the
        // decimal separator, reports failure by throwing, and accepts neither
        // "+5" nor the radix prefixes. See core/number_format.hpp.
        return string_to_number(static_cast<string_object *>(v.as_heap())->text);
    }
    return std::nan("");
}

/// Number::exponentiate, 6.1.6.1.3. Three special cases and then libm.
///
/// C99 F.10.4.4 makes `pow(+-1, y)` exactly 1 for every y, on the reasoning that
/// 1 to any power is 1 even when the power is unknown. JavaScript takes the
/// opposite view for an unknown or infinite exponent, and it matters: `1 ** NaN`
/// coming back as 1 is a number a page will keep computing with, where NaN is a
/// value `isNaN` can catch.
double context::exponentiate(double base, double exponent) {
    if (std::isnan(exponent)) { return std::nan(""); }
    // Even NaN to the zeroth power is 1, so this is checked before the base.
    if (exponent == 0) { return 1; }
    if (std::isinf(exponent) && std::fabs(base) == 1) { return std::nan(""); }
    return std::pow(base, exponent);
}

/// ToString (7.1.17), except that a symbol yields its property key (this is
/// also ToPropertyKey). Objects go through ToPrimitive with the string hint.
std::string context::to_string(value v) {
    if (v.is_undefined()) { return "undefined"; }
    if (v.is_null()) { return "null"; }
    if (v.is_boolean()) { return v.as_boolean() ? "true" : "false"; }
    // number_to_string, not std::to_string(double), which is `%f` to six
    // decimals: "0.333333" for 1/3, "0" for anything smaller than about 1e-7,
    // and 309 literal digits for the largest double. It is also
    // locale-dependent. See core/number_format.hpp.
    if (v.is_number()) { return number_to_string(v.as_number()); }
    if (v.is_string()) { return static_cast<string_object *>(v.as_heap())->text; }
    // The key, not the description: this is what makes `o[sym]` reach a slot
    // no literal can name, since a computed property goes through here.
    if (v.is_kind(heap_kind::symbol)) { return static_cast<symbol_object *>(v.as_heap())->key; }
    // The digits, without the trailing `n`: `String(1n)` is "1", and the
    // suffix is literal syntax rather than part of the value.
    if (v.is_kind(heap_kind::bigint)) {
        return bigint_to_string(static_cast<bigint_object *>(v.as_heap())->digits);
    }
    // One level of C++ re-entry, and everything past this point is capable of
    // it: an array's elements can include the array, and an object converts
    // through a `toString` that can ask to convert the same object again. See
    // context::reentry_scope.
    const reentry_scope guard{*this};
    if (guard.overflowed()) { return "[object Object]"; }
    if (v.is_array()) {
        auto * arr = static_cast<array_object *>(v.as_heap());
        std::string out;
        for (std::size_t i = 0; i < arr->items.size(); ++i) {
            if (i != 0) { out += ','; }
            if (!arr->items[i].is_nullish()) { out += to_string(arr->items[i]); }
        }
        return out;
    }
    // Functions and other objects convert through ToPrimitive with the string
    // hint, so a user `toString` is honoured (`'' + x`, template holes and
    // String(x) all use it), and `String(f)` agrees with `f.toString()`, which
    // returns the function's source text. Libraries rely on this: feature
    // probes like `('' + fn).indexOf('native code')`, p5's Friendly Error
    // System and DI containers that scrape parameter names. A native function
    // prints as `function () { [native code] }`, as in every engine.
    return to_primitive_string(v);
}

/// ToPrimitive (ECMA-262 7.1.1): the object's @@toPrimitive first, then
/// OrdinaryToPrimitive (7.1.1.1) in the order \p hint ("default", "number" or
/// "string") gives. Returns false when a TypeError is pending: the exotic
/// method is not callable or returns an object, or neither `toString` nor
/// `valueOf` returns a primitive. Every ToPrimitive in this file goes through
/// this one function, so a conversion fails the same way everywhere instead of
/// falling back to "[object Object]" in one place and NaN in another.
bool context::to_primitive_hint(value v, const char * hint, value & out) {
    const value exotic = lookup_property(v, CTBROWSER_SYMBOL_KEY("toPrimitive"));
    if (throw_pending()) { return false; }
    if (!exotic.is_nullish()) {
        if (!exotic.is_callable()) {
            throw_error("TypeError", "Symbol.toPrimitive is not a function");
            return false;
        }
        const value hint_arg[1] = {string(hint)};
        out = call(exotic, hint_arg, v);
        if (throw_pending()) { return false; }
        if (out.is_object_like()) {
            throw_error("TypeError", "Cannot convert object to primitive value");
            return false;
        }
        return true;
    }
    const bool string_first = hint[0] == 's';
    for (const char * name :
         {string_first ? "toString" : "valueOf", string_first ? "valueOf" : "toString"}) {
        const value fn = lookup_property(v, name);
        if (throw_pending()) { return false; }
        if (!fn.is_callable()) { continue; }
        out = call(fn, std::span<const value>{}, v);
        if (throw_pending()) { return false; }
        if (!out.is_object_like()) { return true; }
    }
    throw_error("TypeError", "Cannot convert object to primitive value");
    return false;
}

/// ToPrimitive with the default hint, as `+` and `==` need it: the operator
/// can only decide what it means once both sides are primitive
/// (`{valueOf: () => 42} + 1` is 43, `{toString: () => 'x'} + 1` is "x1").
/// Returns undefined when the conversion threw.
value context::to_primitive(value v) {
    const reentry_scope guard{*this};
    if (guard.overflowed()) { return value::undefined(); }
    // BigInt and Symbol are primitives even though they are heap-allocated, so
    // they are returned as-is (ECMA-262 7.1.1 step 2). Sending a BigInt through
    // valueOf/toString would turn `1n + 2n` into "12". The operators that
    // cannot take a Symbol (binary_op, compare_relational) throw the TypeError
    // themselves.
    if (!v.is_heap() || v.is_string() || v.is_kind(heap_kind::bigint) ||
        v.is_kind(heap_kind::symbol)) {
        return v;
    }
    value out = value::undefined();
    return to_primitive_hint(v, "default", out) ? out : value::undefined();
}

/// ToNumber (7.1.4) including objects: ToPrimitive with the number hint
/// (`valueOf` then `toString`), then ToNumber of the result. BigInt and Symbol
/// throw a TypeError and give NaN.
double context::to_number_value(value v) {
    const reentry_scope guard{*this};
    if (guard.overflowed()) { return std::nan(""); }
    // A BigInt refuses implicit conversion to a Number, and that refusal is the
    // type's whole safety property: `+1n`, `Math.abs(1n)` and `1n * 2` all land
    // here, and each would silently round past 2^53. `Number(1n)` is the
    // explicit conversion and does not come through this path.
    if (v.is_kind(heap_kind::bigint)) {
        throw_error("TypeError", "Cannot convert a BigInt value to a number");
        return std::nan("");
    }
    // A Symbol refuses it outright too (7.1.4 step 2): `+sym`, `-sym`,
    // `sym * 1`. A built-in's argument helpers refuse one before they get
    // here (numeric_arg, internal.hpp), so this is the operators' refusal.
    if (v.is_kind(heap_kind::symbol)) {
        throw_error("TypeError", "Cannot convert a Symbol value to a number");
        return std::nan("");
    }
    if (!v.is_heap() || v.is_string()) { return to_number(v); }
    value out = value::undefined();
    if (!to_primitive_hint(v, "number", out)) { return std::nan(""); }
    if (out.is_kind(heap_kind::bigint)) {
        throw_error("TypeError", "Cannot convert a BigInt value to a number");
        return std::nan("");
    }
    // A Symbol out of ToPrimitive (`Object(Symbol())`, or a valueOf returning
    // one) is 7.1.4's TypeError; a bare symbol argument is the caller's
    // numeric_arg check, for the reason internal.hpp gives.
    if (out.is_kind(heap_kind::symbol)) {
        throw_error("TypeError", "Cannot convert a Symbol value to a number");
        return std::nan("");
    }
    return to_number(out);
}

/// ToString of an object: ToPrimitive with the string hint, then ToString.
/// Gives "[object Object]" when the conversion threw or recursed too deeply.
std::string context::to_primitive_string(value v) {
    // Guarded in its own right as well as through to_string, its only caller
    // today: this is the half of the cycle that calls, so a second caller must
    // not be able to reach it uncounted.
    const reentry_scope guard{*this};
    if (guard.overflowed()) { return "[object Object]"; }
    value out = value::undefined();
    if (!to_primitive_hint(v, "string", out)) { return "[object Object]"; }
    return to_string(out);
}

/// The `typeof` operator (13.5.3).
std::string_view context::type_of(value v) {
    if (v.is_kind(heap_kind::symbol)) { return "symbol"; }
    if (v.is_kind(heap_kind::bigint)) { return "bigint"; }
    if (v.is_undefined()) { return "undefined"; }
    if (v.is_null()) { return "object"; } // The famous wart, preserved.
    if (v.is_boolean()) { return "boolean"; }
    if (v.is_number()) { return "number"; }
    if (v.is_string()) { return "string"; }
    if (v.is_callable()) { return "function"; }
    return "object";
}

/// IsLooselyEqual (`==`, 7.2.14).
bool context::loose_equals(value a, value b) {
    if (a.is_nullish() && b.is_nullish()) { return true; }
    if (a.is_nullish() || b.is_nullish()) { return false; }
    if (a.is_boolean()) { a = value::number(a.as_boolean() ? 1 : 0); }
    if (b.is_boolean()) { b = value::number(b.as_boolean() ? 1 : 0); }
    if (a.is_number() && b.is_number()) { return a.as_number() == b.as_number(); }
    if (a.is_string() && b.is_string()) {
        return static_cast<string_object *>(a.as_heap())->text ==
               static_cast<string_object *>(b.as_heap())->text;
    }
    // `1n == 1` is true while `1n === 1` is false: loose equality compares the
    // mathematical values across the two numeric types, which is exactly the
    // distinction the two operators exist to draw.
    {
        const bool a_big = a.is_kind(heap_kind::bigint);
        const bool b_big = b.is_kind(heap_kind::bigint);
        if (a_big && b_big) {
            return static_cast<bigint_object *>(a.as_heap())->digits ==
                   static_cast<bigint_object *>(b.as_heap())->digits;
        }
        if (a_big && b.is_number()) {
            const std::optional<int> o = bigint_compare_double(
                static_cast<bigint_object *>(a.as_heap())->digits, b.as_number());
            return o && *o == 0;
        }
        if (b_big && a.is_number()) {
            const std::optional<int> o = bigint_compare_double(
                static_cast<bigint_object *>(b.as_heap())->digits, a.as_number());
            return o && *o == 0;
        }
        // Against a string, the string is converted to a BigInt: `1n == "1"`.
        if (a_big && b.is_string()) {
            const std::optional<bigint> parsed =
                bigint_from_string(static_cast<string_object *>(b.as_heap())->text);
            return parsed && *parsed == static_cast<bigint_object *>(a.as_heap())->digits;
        }
        if (b_big && a.is_string()) {
            const std::optional<bigint> parsed =
                bigint_from_string(static_cast<string_object *>(a.as_heap())->text);
            return parsed && *parsed == static_cast<bigint_object *>(b.as_heap())->digits;
        }
    }
    if (a.is_kind(heap_kind::symbol) && b.is_kind(heap_kind::symbol)) { return a == b; }
    // Strings, BigInts and Symbols are primitives despite being heap allocated.
    // Only actual objects compare by identity or undergo ToPrimitive.
    const bool a_object = a.is_object_like();
    const bool b_object = b.is_object_like();
    if (a_object && b_object) { return a == b; }
    if (a_object != b_object) {
        const value primitive = a_object ? to_primitive(a) : to_primitive(b);
        if (throw_pending()) { return false; }
        return a_object ? loose_equals(primitive, b) : loose_equals(a, primitive);
    }
    return to_number(a) == to_number(b); // The coercing cases.
}

/// The seven binary operations that never call back into script: add (on
/// values already known to be primitives) and the six bitwise/shift operators.
/// Each tries the BigInt arm, and otherwise converts statically and combines;
/// nothing is converted with valueOf/toString here. The interpreter and
/// AOT-compiled code both call this function (ct_aot_binary_op_static in
/// include/ctbrowser/aot/helpers/values-and-properties.def), so both run the
/// same code instead of two implementations that only happen to agree.
value context::binary_op_static(op kind, value lhs, value rhs) {
    if (value made; bigint_binary(kind, lhs, rhs, made)) { return made; }
    switch (kind) {
    case op::add: return value::number(to_number(lhs) + to_number(rhs));
    case op::bit_and: return value::number(to_int32(lhs) & to_int32(rhs));
    case op::bit_or: return value::number(to_int32(lhs) | to_int32(rhs));
    case op::bit_xor: return value::number(to_int32(lhs) ^ to_int32(rhs));
    case op::shl:
        return value::number(static_cast<std::int32_t>(static_cast<std::uint32_t>(to_int32(lhs))
                                                       << (to_uint32(rhs) & 31U)));
    case op::shr: return value::number(to_int32(lhs) >> (to_uint32(rhs) & 31U));
    case op::ushr:
        return value::number(static_cast<double>(to_uint32(lhs) >> (to_uint32(rhs) & 31U)));
    default: break;
    }
    // Not reachable from the interpreter, which only ever passes the seven, and
    // deliberately not an assert: this is also the AOT entry point, and an
    // image carrying a bad op_kind is untrusted input rather than a bug in this
    // file. Undefined is what every other unreachable arm in this VM returns.
    return value::undefined();
}

/// Unary minus. The BigInt arm allocates a new BigInt; the Number arm goes
/// through to_number_value, which may call valueOf/toString but never
/// allocates.
value context::negate_value(value v) {
    // -0n is 0n, not -0: a BigInt has one zero.
    if (v.is_kind(heap_kind::bigint)) {
        return value::object(
            allocate<bigint_object>(-static_cast<bigint_object *>(v.as_heap())->digits));
    }
    return value::number(-to_number_value(v));
}

/// ToPrimitive with the number hint, for an operand of a numeric operator.
/// Primitives (including BigInt and Symbol) are returned unchanged; returns
/// null when the conversion threw, so callers must check for a throw.
value context::numeric_operand(value v) {
    if (!v.is_heap() || v.is_string() || v.is_kind(heap_kind::bigint) ||
        v.is_kind(heap_kind::symbol)) {
        return v;
    }
    value out = value::undefined();
    if (!to_primitive_hint(v, "number", out)) { return value::null(); }
    return out;
}

/// Converts both operands with numeric_operand, left then right, into \p l and
/// \p r. Returns false when either conversion threw or yielded a Symbol.
bool context::numeric_operands(value lhs, value rhs, value & l, value & r) {
    // See binary_op for why a landed throw is asked of unwinds() as well.
    const std::size_t unwound = unwinds();
    const bool pending = throw_pending();
    const auto threw = [this, unwound, pending] {
        return (!pending && throw_pending()) || unwinds() != unwound;
    };
    const auto numeric = [&](value from, value & to) {
        to = numeric_operand(from);
        if (threw()) { return false; }
        // 7.1.4 ToNumber step 2: a Symbol is never implicitly a number.
        if (to.is_kind(heap_kind::symbol)) {
            throw_error("TypeError", "Cannot convert a Symbol value to a number");
            return false;
        }
        return true;
    };
    return numeric(lhs, l) && numeric(rhs, r);
}

/// Bitwise NOT (`~`). The operand must already be a primitive (see
/// numeric_operand).
value context::bit_not_value(value v) {
    // ~1n is -2n, on the unbounded two's-complement value: there is no ToInt32
    // step, because a BigInt has no width to truncate to.
    if (v.is_kind(heap_kind::bigint)) {
        return value::object(
            allocate<bigint_object>(~static_cast<bigint_object *>(v.as_heap())->digits));
    }
    return value::number(~to_int32(v));
}

/// The binary operations that may call back into script, because they convert
/// objects through valueOf/toString first: concat (template literals),
/// add_generic (`+`), sub, mul, div, mod and pow, plus the bitwise operators
/// when their operands are not yet known to be primitives. AOT-compiled code
/// reaches this function through ct_aot_binary_op.
///
/// The cases differ in one way worth noticing: concat never consults the
/// BigInt arm, and add_generic consults it only after both sides are
/// primitive and neither is a string.
value context::binary_op(op kind, value lhs, value rhs) {
    // If the left operand's conversion throws, the operator stops. A throw from
    // a user valueOf/toString has already reached its handler by the time the
    // conversion returns, so throw_pending() cannot see it; converting the
    // right operand anyway would run user code again with no handler
    // (test262 Temporal valueOf/basic.js: `try { o < o } catch {}`).
    // unwinds() counts landed throws, so `threw` compares it. A throw that was
    // already pending on entry is not the operand's: IteratorClose calls a
    // `return()` while the TypeError that closed the iterator is still in
    // flight.
    const std::size_t unwound = unwinds();
    const bool pending = throw_pending();
    const auto threw = [this, unwound, pending] {
        return (!pending && throw_pending()) || unwinds() != unwound;
    };
    // A Symbol is never implicitly a string or a number (7.1.17 step 2,
    // 7.1.4 step 2): a template substitution, `+`, `-` and the relational
    // operators all refuse one. to_string cannot (it is ToPropertyKey too,
    // and hands a symbol's key back), so the operators check here.
    const auto refuse_symbol = [this](value a, value b, const char * to) {
        if (!a.is_kind(heap_kind::symbol) && !b.is_kind(heap_kind::symbol)) { return false; }
        throw_error("TypeError", std::string{"Cannot convert a Symbol value to a "} + to);
        return true;
    };
    // Concat is handled first because it must never reach bigint_binary: that
    // switch has no concat case, so `${1n}` would hit its default arm and throw
    // "BigInts have no unsigned right shift". Template literals emit concat,
    // and it is always two ToStrings.
    if (kind == op::concat) {
        if (refuse_symbol(lhs, lhs, "string")) { return value::undefined(); }
        std::string left = to_string(lhs);
        if (threw()) { return value::undefined(); }
        if (refuse_symbol(rhs, rhs, "string")) { return value::undefined(); }
        return string(std::move(left) + to_string(rhs));
    }

    if (kind == op::add_generic) {
        // JS `+`: string concatenation if either side is a string, numeric
        // addition otherwise. The one operator whose meaning is decided by its
        // operands, which is why it is not folded into `add`.
        //
        // Both sides are made primitive first, and only then does the operator
        // decide what it is. An object's own valueOf or toString is what settles
        // it, so `{valueOf: () => 42} + 1` is 43 while `{toString: () => 'x'} + 1`
        // is "x1".
        const value l = to_primitive(lhs);
        if (threw()) { return value::undefined(); }
        const value r = to_primitive(rhs);
        if (threw()) { return value::undefined(); }
        if (refuse_symbol(l, r, l.is_string() || r.is_string() ? "string" : "number")) {
            return value::undefined();
        }
        // Before the string-or-number decision: `1n + 1n` is addition and
        // `1n + 1` is a TypeError, neither of which is either arm below. Note
        // `1n + "a"` is concatenation, so the string test still comes first.
        if (!l.is_string() && !r.is_string()) {
            if (value made; bigint_binary(op::add_generic, l, r, made)) { return made; }
        }
        if (l.is_string() || r.is_string()) { return string(to_string(l) + to_string(r)); }
        return value::number(to_number(l) + to_number(r));
    }

    // ECMA-262 13.15.3 ApplyStringOrNumericBinaryOperator: ToNumeric of both
    // operands first (ToPrimitive with the number hint, left then right), and
    // only then the BigInt-or-Number decision, so
    // `{valueOf() { return 2n; }} - 1n` is 1n.
    value l = value::undefined();
    value r = value::undefined();
    if (!numeric_operands(lhs, rhs, l, r)) { return value::undefined(); }
    if (value made; bigint_binary(kind, l, r, made)) { return made; }
    // `to_number_value`, not `to_number`: ToNumber runs valueOf/toString on
    // an object (none is left here), and a Symbol is its TypeError.
    switch (kind) {
    case op::sub: return value::number(to_number_value(l) - to_number_value(r));
    case op::mul: return value::number(to_number_value(l) * to_number_value(r));
    case op::div: return value::number(to_number_value(l) / to_number_value(r));
    case op::mod: return value::number(std::fmod(to_number_value(l), to_number_value(r)));
    // `**` is context::exponentiate, not libm's pow: the specification's edge
    // cases for it do not agree with C's.
    case op::pow: return value::number(exponentiate(to_number_value(l), to_number_value(r)));
    // The six bitwise operators, after ToNumeric of both operands (a Symbol's
    // TypeError, an object's valueOf). They are listed here so that
    // AOT-compiled code calling ct_aot_binary_op gets the interpreter's
    // result; calling binary_op_static directly would skip the conversion.
    case op::bit_and:
    case op::bit_or:
    case op::bit_xor:
    case op::shl:
    case op::shr:
    case op::ushr: return binary_op_static(kind, l, r);
    default: break;
    }
    // Same position as binary_op_static's default arm: unreachable from the
    // interpreter, and untrusted input from an image rather than a bug.
    return value::undefined();
}

/// Applies \p kind when either operand is a BigInt, writing the result (or
/// undefined after a throw) to \p out and returning true. Returns false when
/// neither operand is a BigInt. Mixing a BigInt with another type throws.
bool context::bigint_binary(op kind, value a, value b, value & out) {
    const bool a_big = a.is_kind(heap_kind::bigint);
    const bool b_big = b.is_kind(heap_kind::bigint);
    if (!a_big && !b_big) { return false; }
    if (a_big != b_big) {
        // The one message worth getting right, because it is the whole contract:
        // a page that meant to mix has a real bug and needs to be told so.
        throw_error("TypeError", "Cannot mix BigInt and other types, use explicit conversions");
        out = value::undefined();
        return true;
    }
    const bigint & x = static_cast<bigint_object *>(a.as_heap())->digits;
    const bigint & y = static_cast<bigint_object *>(b.as_heap())->digits;
    const auto give = [&](const bigint & r) { out = value::object(allocate<bigint_object>(r)); };
    const auto give_or_throw = [&](const std::optional<bigint> & r, const char * what) {
        if (!r) {
            throw_error("RangeError", what);
            out = value::undefined();
            return;
        }
        give(*r);
    };
    switch (kind) {
    case op::add_generic:
    case op::add: give(x + y); break;
    case op::sub: give(x - y); break;
    case op::mul: give_or_throw(bigint_mul(x, y), "Maximum BigInt size exceeded"); break;
    case op::div: give_or_throw(bigint_div(x, y), "Division by zero"); break;
    case op::mod: give_or_throw(bigint_rem(x, y), "Division by zero"); break;
    case op::pow:
        give_or_throw(bigint_pow(x, y),
                      y < 0 ? "Exponent must be non-negative" : "Maximum BigInt size exceeded");
        break;
    case op::bit_and: give(x & y); break;
    case op::bit_or: give(x | y); break;
    case op::bit_xor: give(x ^ y); break;
    case op::shl: give_or_throw(bigint_shl(x, y), "Maximum BigInt size exceeded"); break;
    case op::shr: give_or_throw(bigint_shr(x, y), "Maximum BigInt size exceeded"); break;
    default:
        // An operator with no BigInt meaning at all. `>>>` is the one the
        // specification names, because an unsigned shift needs a width and a
        // BigInt has none.
        throw_error("TypeError", "BigInts have no unsigned right shift");
        out = value::undefined();
        break;
    }
    return true;
}

/// IsLessThan, the abstract relational comparison (7.2.13).
///
/// Two strings compare by code unit, not as numbers: ToNumber of a
/// non-numeric string is NaN, so a numeric-only comparison would make
/// `"a" < "b"` false and break any sort of text.
///
/// Returning an ordering rather than a bool is what lets all four operators
/// share one comparison: `unordered` is the specification's `undefined` result,
/// and `std::is_lt`/`is_lteq`/`is_gt`/`is_gteq` are each false for it, which is
/// exactly the required NaN behaviour.
std::partial_ordering context::compare_relational(value a, value b) {
    // ToPrimitive with the number hint, left operand first. The order is
    // observable because `valueOf` can have side effects, and it stays
    // source-order for `>` and `>=` because the caller passes the operands in
    // the order they were written.
    // The left operand's throw stops the comparison - see binary_op.
    const std::size_t unwound = unwinds();
    const bool pending = throw_pending();
    const auto threw = [this, unwound, pending] {
        return (!pending && throw_pending()) || unwinds() != unwound;
    };
    // The number hint (7.2.13 steps 1-2: ToPrimitive(x, NUMBER)) is
    // numeric_operand's. The default hint would be wrong: a Date's
    // @@toPrimitive treats "default" as "string", so `d1 < d2` would compare
    // weekday names.
    const value pa = numeric_operand(a);
    if (threw()) { return std::partial_ordering::unordered; }
    const value pb = numeric_operand(b);
    if (threw()) { return std::partial_ordering::unordered; }
    // ToNumeric of a symbol (7.2.13 step 4; two strings are the only pair
    // compared without it) is the TypeError, as in binary_op.
    if (pa.is_kind(heap_kind::symbol) || pb.is_kind(heap_kind::symbol)) {
        throw_error("TypeError", "Cannot convert a Symbol value to a number");
        return std::partial_ordering::unordered;
    }
    // A BigInt compares against a Number even though it cannot be added to one:
    // `1n < 2` is true. The comparison is exact rather than going through a
    // double, because a bigint past 2^53 must not round into equality with the
    // number beside it, which is the one loss the type exists to prevent.
    {
        const bool a_big = pa.is_kind(heap_kind::bigint);
        const bool b_big = pb.is_kind(heap_kind::bigint);
        if (a_big || b_big) {
            std::optional<int> order;
            // Against a string, the string becomes a BigInt (StringToBigInt,
            // 7.2.13 steps 3.a-3.b) exactly ("9007199254740993" is not the
            // double 2^53), and a string that is no integer ("0.", "1e3") makes
            // the comparison undefined, which is false every way round.
            const auto exact = [](const bigint & x, const bigint & y) {
                return x < y ? -1 : (x > y ? 1 : 0);
            };
            const auto parsed = [](value s) {
                return bigint_from_string(static_cast<string_object *>(s.as_heap())->text);
            };
            if (a_big && b_big) {
                order = exact(static_cast<bigint_object *>(pa.as_heap())->digits,
                              static_cast<bigint_object *>(pb.as_heap())->digits);
            } else if (a_big && pb.is_string()) {
                if (const std::optional<bigint> y = parsed(pb)) {
                    order = exact(static_cast<bigint_object *>(pa.as_heap())->digits, *y);
                }
            } else if (b_big && pa.is_string()) {
                if (const std::optional<bigint> x = parsed(pa)) {
                    order = exact(*x, static_cast<bigint_object *>(pb.as_heap())->digits);
                }
            } else if (a_big) {
                order = bigint_compare_double(static_cast<bigint_object *>(pa.as_heap())->digits,
                                              to_number(pb));
            } else {
                const std::optional<int> flipped = bigint_compare_double(
                    static_cast<bigint_object *>(pb.as_heap())->digits, to_number(pa));
                if (flipped) { order = -*flipped; }
            }
            if (!order) { return std::partial_ordering::unordered; }
            return *order < 0   ? std::partial_ordering::less
                   : *order > 0 ? std::partial_ordering::greater
                                : std::partial_ordering::equivalent;
        }
    }
    if (pa.is_string() && pb.is_string()) {
        const std::string & x = static_cast<string_object *>(pa.as_heap())->text;
        const std::string & y = static_cast<string_object *>(pb.as_heap())->text;
        // Code unit order (7.2.13 step 3), which UTF-8's byte order is not
        // once a surrogate pair meets U+E000-U+FFFF; hence utf16::compare.
        const int r = utf16::compare(x, y);
        return r < 0   ? std::partial_ordering::less
               : r > 0 ? std::partial_ordering::greater
                       : std::partial_ordering::equivalent;
    }
    const double x = to_number(pa);
    const double y = to_number(pb);
    if (std::isnan(x) || std::isnan(y)) { return std::partial_ordering::unordered; }
    return x < y   ? std::partial_ordering::less
           : x > y ? std::partial_ordering::greater
                   : std::partial_ordering::equivalent;
}

} // namespace ctbrowser::script
