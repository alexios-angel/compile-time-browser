// ctbrowser.script context - the coercions - ToNumber, ToString, ToPrimitive and the comparisons.
//
// One of four files carved out of a 3,232-line vm.cpp on 2026-08-09. All
// members of `context`, declared in include/ctbrowser/script/vm.hpp - so
// they split across translation units with nothing to declare.

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/number_format.hpp>
#include <ctbrowser/script/vm.hpp>

// The VM's implementation.
//
// `run_loop` alone is 15 KB of object code - the whole instruction dispatch -
// and while it lived in the interface every translation unit that imported the
// module emitted its own copy and optimised it again. The class declaration
// stays in :vm; the bodies live here and are compiled once.

namespace ctbrowser::script {

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
    return true; // every other object is truthy
}

double context::to_number(value v) {
    if (v.is_number()) { return v.as_number(); }
    if (v.is_boolean()) { return v.as_boolean() ? 1 : 0; }
    if (v.is_null()) { return 0; }
    if (v.is_undefined()) { return std::nan(""); }
    if (v.is_string()) {
        // string_to_number, not std::stod: stod reads LC_NUMERIC for the decimal
        // separator, throws to report a failure this VM then had to catch, and
        // accepted neither "+5" nor the radix prefixes. See script/number_format.hpp.
        return string_to_number(static_cast<string_object *>(v.as_heap())->text);
    }
    return std::nan("");
}

// Number::exponentiate, 6.1.6.1.3. Three special cases and then libm.
//
// C99 F.10.4.4 makes `pow(+-1, y)` exactly 1 for EVERY y, on the reasoning that
// 1 to any power is 1 even when the power is unknown. JavaScript takes the
// opposite view for an unknown or infinite exponent, and it matters: `1 ** NaN`
// coming back as 1 is a number a page will keep computing with, where NaN is a
// value `isNaN` can catch.
double context::exponentiate(double base, double exponent) {
    if (std::isnan(exponent)) { return std::nan(""); }
    // Even NaN to the zeroth power is 1 - this is checked before the base.
    if (exponent == 0) { return 1; }
    if (std::isinf(exponent) && std::fabs(base) == 1) { return std::nan(""); }
    return std::pow(base, exponent);
}

std::string context::to_string(value v) {
    if (v.is_undefined()) { return "undefined"; }
    if (v.is_null()) { return "null"; }
    if (v.is_boolean()) { return v.as_boolean() ? "true" : "false"; }
    // number_to_string, not std::to_string(double), which is `%f` to six
    // decimals - so this returned "0.333333" for 1/3, "0" for anything smaller
    // than about 1e-7, and 309 literal digits for the largest double. It is also
    // locale-dependent. See script/number_format.hpp.
    if (v.is_number()) { return number_to_string(v.as_number()); }
    if (v.is_string()) { return static_cast<string_object *>(v.as_heap())->text; }
    // The KEY, not the description: this is what makes `o[sym]` reach a slot
    // no literal can name, since a computed property goes through here.
    if (v.is_kind(heap_kind::symbol)) { return static_cast<symbol_object *>(v.as_heap())->key; }
    // The DIGITS, without the trailing `n` - `String(1n)` is "1", and the
    // suffix is literal syntax rather than part of the value.
    if (v.is_kind(heap_kind::bigint)) {
        return bigint_to_string(static_cast<bigint_object *>(v.as_heap())->digits);
    }
    // ONE LEVEL OF C++ RE-ENTRY, and everything past this point is capable of
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
    if (v.is_callable()) { return "function"; }
    // AN OBJECT CONVERTS THROUGH ITS OWN `toString`, then `valueOf`.
    //
    // That is ToPrimitive, and it is not a nicety: a class that defines
    // toString does so precisely because it expects `'' + x`, a template hole
    // and String(x) to use it. Returning the tag regardless turned every such
    // object into "[object Object]" - which for colorjs, whose colour spaces
    // print as "HSB (hsb)", made an error message name the wrong thing and a
    // page's own labels come out as tags.
    return to_primitive_string(v);
}

// `toString` then `valueOf`, either of which may be inherited. Bounded: a
// method that hands back another object falls through to the tag rather than
// recursing, which is what the spec does after trying both.
// ToPrimitive with the DEFAULT hint: `valueOf`, then `toString`, and whatever
// comes back is a primitive the operator can work on. `+` needs this before it
// can even decide what it means - `{valueOf: () => 42} + 1` is 43 and
// `{toString: () => 'x'} + 1` is "x1", and which one it is depends on what the
// object hands back rather than on the object being an object.
value context::to_primitive(value v) {
    const reentry_scope guard{*this};
    if (guard.overflowed()) { return value::undefined(); }
    // A BIGINT IS ALREADY A PRIMITIVE, heap-allocated though it is. Letting it
    // into the valueOf/toString walk below turns it into a STRING the moment
    // BigInt.prototype carries those methods - which is exactly what made
    // `1n + 2n` evaluate to "12" once it did.
    //
    // A SYMBOL IS DELIBERATELY NOT LISTED HERE, though it is a primitive too.
    // It should REFUSE this conversion outright (a TypeError), and it cannot
    // until ToPropertyKey is separated from ToString - `o[sym]` resolves
    // through the same call. Until then the toString walk at least yields
    // "Symbol(x)" rather than the internal key, which is the nearer wrong
    // answer; unittests/js/symbol_basics.cpp pins that and says so.
    if (!v.is_heap() || v.is_string() || v.is_kind(heap_kind::bigint)) { return v; }
    for (const char * name : {"valueOf", "toString"}) {
        const value fn = lookup_property(v, name);
        if (!fn.is_callable()) { continue; }
        const value produced = call(fn, std::span<const value>{}, v);
        if (produced.is_heap() && !produced.is_string()) { continue; }
        return produced;
    }
    return v;
}

// The numeric half of the same rule: `valueOf` then `toString`. An object that
// defines valueOf means it to be used in arithmetic - that is the only reason to
// define one - and without this every such object was NaN.
double context::to_number_value(value v) {
    const reentry_scope guard{*this};
    if (guard.overflowed()) { return std::nan(""); }
    // A BIGINT REFUSES IMPLICIT CONVERSION TO A NUMBER, and that refusal is the
    // type's whole safety property: `+1n`, `Math.abs(1n)` and `1n * 2` all land
    // here, and each would silently round past 2^53. `Number(1n)` is the
    // EXPLICIT conversion and does not come through this path.
    if (v.is_kind(heap_kind::bigint)) {
        throw_error("TypeError", "Cannot convert a BigInt value to a number");
        return std::nan("");
    }
    if (!v.is_heap() || v.is_string()) { return to_number(v); }
    for (const char * name : {"valueOf", "toString"}) {
        const value fn = lookup_property(v, name);
        if (!fn.is_callable()) { continue; }
        const value produced = call(fn, std::span<const value>{}, v);
        if (produced.is_heap() && !produced.is_string()) { continue; }
        return to_number(produced);
    }
    return to_number(v);
}

std::string context::to_primitive_string(value v) {
    // Guarded in its own right as well as through to_string, its only caller
    // today: this is the half of the cycle that CALLS, so a second caller must
    // not be able to reach it uncounted.
    const reentry_scope guard{*this};
    if (guard.overflowed()) { return "[object Object]"; }
    for (const char * name : {"toString", "valueOf"}) {
        const value fn = lookup_property(v, name);
        if (!fn.is_callable()) { continue; }
        const value produced = call(fn, std::span<const value>{}, v);
        if (produced.is_heap() && !produced.is_string()) { continue; }
        return to_string(produced);
    }
    return "[object Object]";
}

std::string_view context::type_of(value v) {
    if (v.is_kind(heap_kind::symbol)) { return "symbol"; }
    if (v.is_kind(heap_kind::bigint)) { return "bigint"; }
    if (v.is_undefined()) { return "undefined"; }
    if (v.is_null()) { return "object"; } // the famous wart, preserved
    if (v.is_boolean()) { return "boolean"; }
    if (v.is_number()) { return "number"; }
    if (v.is_string()) { return "string"; }
    if (v.is_callable()) { return "function"; }
    return "object";
}

bool context::loose_equals(value a, value b) {
    if (a.is_nullish() && b.is_nullish()) { return true; }
    if (a.is_nullish() || b.is_nullish()) { return false; }
    if (a.is_number() && b.is_number()) { return a.as_number() == b.as_number(); }
    if (a.is_string() && b.is_string()) {
        return static_cast<string_object *>(a.as_heap())->text ==
               static_cast<string_object *>(b.as_heap())->text;
    }
    // A STRING IS ON THE HEAP TOO, so "both heap means compare identity" is not
    // the test - it caught `"" == []` and answered false before ToPrimitive ever
    // ran. Only two genuine OBJECTS compare by identity.
    // AN OBJECT AGAINST A PRIMITIVE COERCES THROUGH ToPrimitive - 7.2.15 steps
    // 10 and 11 - and then the comparison is retried on the result. Falling
    // straight through to ToNumber instead makes the object NaN, so the answer
    // was always false: `0 == []`, `1 == [1]` and `"" == []` are all TRUE in
    // every browser and were all false here.
    //
    // The guard against re-entering forever is that to_primitive hands back the
    // object UNCHANGED when neither valueOf nor toString produces a primitive;
    // in that case there is nothing to retry with and the numeric compare below
    // is the right answer.
    // `1n == 1` IS TRUE while `1n === 1` is false - loose equality compares the
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
        // Against a STRING, the string is converted to a BigInt: `1n == "1"`.
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
    const bool a_object = a.is_heap() && !a.is_string();
    const bool b_object = b.is_heap() && !b.is_string();
    if (a_object && b_object) { return a == b; }
    if (a_object != b_object) {
        const value primitive = a_object ? to_primitive(a) : to_primitive(b);
        const bool progressed = a_object ? !(primitive == a) : !(primitive == b);
        if (progressed) {
            return a_object ? loose_equals(primitive, b) : loose_equals(a, primitive);
        }
    }
    return to_number(a) == to_number(b); // the coercing cases
}

// Abstract Relational Comparison, 7.2.13.
//
// THIS USED TO BE `to_number(a) < to_number(b)` AND NOTHING ELSE, which makes
// every relational comparison between two strings FALSE - `"a" < "b"`, `"b" >
// "a"`, `"abc" < "abd"`, all of them - because ToNumber of a non-numeric string
// is NaN and every comparison against NaN is false. It is not a small corner:
// `["b","a","c"].sort((x, y) => x < y ? -1 : 1)` returned its input untouched,
// and any page ordering names, keys or dates as text got silence rather than an
// error. `===` was unaffected, which is why it survived so long.
//
// Returning an ordering rather than a bool is what lets all four operators
// share one comparison: `unordered` is the specification's `undefined` result,
// and `std::is_lt`/`is_lteq`/`is_gt`/`is_gteq` are each false for it, which is
// exactly the required NaN behaviour.
// The bigint half of every arithmetic opcode. See the declaration for why
// mixing throws rather than coercing.
// THE SEVEN NON-RE-ENTERING BINARY OPERATIONS. Phase 5's extraction, and the
// row in aot_helpers.def names this function by name.
//
// Every one of them was the same eleven lines in run_loop.cpp: try the BigInt
// arm, and otherwise convert statically and combine. They are here so that the
// interpreter and a compiled body run the SAME code rather than two
// implementations that agree today - which is the entire point of the phase,
// and the reason its discipline is one helper per commit with the suite green
// in between.
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
    // NOT REACHABLE FROM THE INTERPRETER, which only ever passes the seven, and
    // deliberately not an assert: this is also the AOT entry point, and an
    // image carrying a bad op_kind is untrusted input rather than a bug in this
    // file. Undefined is what every other unreachable arm in this VM returns.
    return value::undefined();
}

// THE SEVEN RE-ENTERING BINARY OPERATIONS. Phase 5's second extraction, and its
// row names this function.
//
// Every arm below is the handler it came from, unchanged. What is worth reading
// twice is what is NOT uniform: concat never consults the BigInt arm, and
// add_generic consults it only after both sides are primitive and only when
// neither is a string.
// UNARY MINUS. The body is VM_CASE(negate) unchanged, in the order it had.
// Both arms matter and neither is the other's fast path: the BigInt arm
// allocates and cannot throw catchably; the Number arm re-enters through
// to_number_value and cannot allocate.
value context::negate_value(value v) {
    // -0n is 0n, not -0: a BigInt has one zero.
    if (v.is_kind(heap_kind::bigint)) {
        return value::object(
            allocate<bigint_object>(-static_cast<bigint_object *>(v.as_heap())->digits));
    }
    return value::number(-to_number_value(v));
}

// BITWISE NOT, likewise VM_CASE(bit_not) unchanged.
value context::bit_not_value(value v) {
    // ~1n is -2n, on the unbounded two's-complement value - there is no ToInt32
    // step, because a BigInt has no width to truncate to.
    if (v.is_kind(heap_kind::bigint)) {
        return value::object(
            allocate<bigint_object>(~static_cast<bigint_object *>(v.as_heap())->digits));
    }
    return value::number(~to_int32(v));
}

value context::binary_op(op kind, value lhs, value rhs) {
    // CONCAT FIRST, because it is the one that must not reach bigint_binary at
    // all: coerce.cpp's switch has no case for it, so `${1n}` would fall to the
    // default arm and throw "BigInts have no unsigned right shift". It is
    // emitted only by template literals and is unconditionally two ToStrings.
    if (kind == op::concat) { return string(to_string(lhs) + to_string(rhs)); }

    if (kind == op::add_generic) {
        // JS `+`: string concatenation if EITHER side is a string, numeric
        // addition otherwise. The one operator whose meaning is decided by its
        // operands, which is why it is not folded into `add`.
        //
        // BOTH SIDES ARE MADE PRIMITIVE FIRST, and only then does the operator
        // decide what it is. An object's own valueOf or toString is what settles
        // it, so `{valueOf: () => 42} + 1` is 43 while `{toString: () => 'x'} + 1`
        // is "x1".
        const value l = to_primitive(lhs);
        const value r = to_primitive(rhs);
        // BEFORE the string-or-number decision: `1n + 1n` is addition and
        // `1n + 1` is a TypeError, neither of which is either arm below. Note
        // `1n + "a"` IS concatenation, so the string test still gets first
        // refusal.
        if (!l.is_string() && !r.is_string()) {
            if (value made; bigint_binary(op::add_generic, l, r, made)) { return made; }
        }
        if (l.is_string() || r.is_string()) { return string(to_string(l) + to_string(r)); }
        return value::number(to_number(l) + to_number(r));
    }

    if (value made; bigint_binary(kind, lhs, rhs, made)) { return made; }
    // `to_number_value`, NOT `to_number`: ToNumber on an object runs
    // valueOf/toString first. Without it `[] - 0` was 0 while `-[]` was NaN,
    // which is one conversion spelled two ways.
    switch (kind) {
    case op::sub: return value::number(to_number_value(lhs) - to_number_value(rhs));
    case op::mul: return value::number(to_number_value(lhs) * to_number_value(rhs));
    case op::div: return value::number(to_number_value(lhs) / to_number_value(rhs));
    case op::mod: return value::number(std::fmod(to_number_value(lhs), to_number_value(rhs)));
    // `**` is context::exponentiate, not libm's pow: the specification's edge
    // cases for it do not agree with C's.
    case op::pow: return value::number(exponentiate(to_number_value(lhs), to_number_value(rhs)));
    default: break;
    }
    // Same position as binary_op_static's default arm: unreachable from the
    // interpreter, and untrusted input from an image rather than a bug.
    return value::undefined();
}

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
    case op::mul: give(x * y); break;
    case op::div: give_or_throw(bigint_div(x, y), "Division by zero"); break;
    case op::mod: give_or_throw(bigint_rem(x, y), "Division by zero"); break;
    case op::pow: give_or_throw(bigint_pow(x, y), "Exponent must be non-negative"); break;
    case op::bit_and: give(x & y); break;
    case op::bit_or: give(x | y); break;
    case op::bit_xor: give(x ^ y); break;
    case op::shl: give_or_throw(bigint_shl(x, y), "BigInt shift is too large"); break;
    case op::shr: give_or_throw(bigint_shr(x, y), "BigInt shift is too large"); break;
    default:
        // An operator with no BigInt meaning at all - `>>>` is the one the
        // specification names, because an unsigned shift needs a width and a
        // BigInt has none.
        throw_error("TypeError", "BigInts have no unsigned right shift");
        out = value::undefined();
        break;
    }
    return true;
}

std::partial_ordering context::compare_relational(value a, value b) {
    // ToPrimitive with the NUMBER hint, LEFT OPERAND FIRST. The order is
    // observable because `valueOf` can have side effects, and it stays
    // source-order for `>` and `>=` because the caller passes the operands in
    // the order they were written.
    const value pa = to_primitive(a);
    const value pb = to_primitive(b);
    // A BIGINT COMPARES AGAINST A NUMBER even though it cannot be added to one:
    // `1n < 2` is true. The comparison is EXACT rather than going through a
    // double, because a bigint past 2^53 must not round into equality with the
    // number beside it - which is the one loss the type exists to prevent.
    {
        const bool a_big = pa.is_kind(heap_kind::bigint);
        const bool b_big = pb.is_kind(heap_kind::bigint);
        if (a_big || b_big) {
            std::optional<int> order;
            if (a_big && b_big) {
                const bigint & x = static_cast<bigint_object *>(pa.as_heap())->digits;
                const bigint & y = static_cast<bigint_object *>(pb.as_heap())->digits;
                order = x < y ? -1 : (x > y ? 1 : 0);
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
        // Byte order. For ASCII that IS code-unit order, and it parts company
        // with the specification only on non-BMP text, where UTF-16 sorts
        // surrogates below U+E000 and UTF-8 does not - the same representation
        // gap docs/script.md records for `length` and `charCodeAt`.
        const int r = x.compare(y);
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
