//===- ctbrowser/lib/Script/builtins/values.cpp - Math, Boolean, Number ---===//
//
// Script builtins: Math, Boolean, Number and the value globals (NaN,
// Infinity, undefined, parseInt, parseFloat). Date is in date.cpp. Shared
// argument helpers and these installers' declarations are in internal.hpp.
//
//===----------------------------------------------------------------------===//

#include "ctbrowser/core/number.hpp"
#include "internal.hpp"
#include "text/internal.hpp"

#include <chrono>
#include <format>
#include <numbers>

namespace ctbrowser::script::builtins_detail {

/// Installs the global `Math`. `seed` seeds Math.random's xorshift64* stream
/// (0 is treated as 1), so renders are deterministic.
void install_math(context & cx, std::uint64_t seed) {
    using detail::method;
    using detail::new_table;
    object_object * math = new_table(cx);
    // Constants come from <numbers>, not written out by hand: a transcription
    // error in the last few digits is invisible to most tests.
    detail::constant(math, "PI", value::number(std::numbers::pi));
    detail::constant(math, "E", value::number(std::numbers::e));
    // A Math argument, where a missing one is NaN, not zero.
    //
    // Every Math function's step 1 is `Let n be ? ToNumber(x)`, an absent
    // argument is `undefined`, and ToNumber(undefined) is NaN, so `Math.sin()`
    // is NaN, not sin(0). The shared `num_at` substitutes 0.0 instead, which is
    // right for its other callers (`"abc".slice()` and `[1,2].indexOf(x)` want
    // a zero default), so Math gets its own accessor.
    // The context is threaded through so an object argument coerces through
    // ToPrimitive: `Math.abs([])` is 0 and `Math.max([1],[2])` is 2, where the
    // static to_number answers NaN for anything on the heap.
    const auto math_arg = [](context & c, std::span<value> a, std::size_t i) {
        return i < a.size() ? c.to_number_value(a[i]) : std::nan("");
    };
    const auto unary = [&](std::string name, double (*fn)(double)) {
        method(cx, math, name, 1, [fn, math_arg](context & c, std::span<value> a) {
            return value::number(fn(math_arg(c, a, 0)));
        });
    };
    detail::constant(math, "SQRT2", value::number(std::numbers::sqrt2));
    // <numbers> has no SQRT1_2, so it is derived rather than transcribed.
    //
    // SQRT1_2 is sqrt2 / 2, not 1 / sqrt2: halving is exact (it only
    // decrements the binary exponent), so this rounds once and gives the
    // correctly rounded 0.7071067811865476 that 21.3.1.8 requires, while the
    // reciprocal rounds twice and lands one ulp low.
    detail::constant(math, "SQRT1_2", value::number(std::numbers::sqrt2 / 2.0));
    detail::constant(math, "LN2", value::number(std::numbers::ln2));
    detail::constant(math, "LN10", value::number(std::numbers::ln10));
    detail::constant(math, "LOG2E", value::number(std::numbers::log2e));
    detail::constant(math, "LOG10E", value::number(std::numbers::log10e));
    // glibc's cbrt can be an ulp off on perfect cubes (cbrt(27) is
    // 3.0000000000000004; V8 returns 3), so an exact integer root is returned
    // when one exists. Other inputs keep libm's answer, which may sit an ulp
    // from V8: a Newton step would fix the cubes but make cases like
    // cbrt(0.001) worse.
    unary("cbrt", [](double x) {
        const double y = std::cbrt(x);
        const double whole = std::nearbyint(y);
        // Overflow in the cube just fails the comparison, which is the right
        // answer anyway: a finite x never equals an infinity.
        return whole * whole * whole == x ? whole : y;
    });
    unary("log2", [](double x) { return std::log2(x); });
    unary("log10", [](double x) { return std::log10(x); });
    unary("log1p", [](double x) { return std::log1p(x); });
    unary("expm1", [](double x) { return std::expm1(x); });
    unary("sinh", [](double x) { return std::sinh(x); });
    unary("cosh", [](double x) { return std::cosh(x); });
    unary("tanh", [](double x) { return std::tanh(x); });
    unary("asinh", [](double x) { return std::asinh(x); });
    unary("acosh", [](double x) { return std::acosh(x); });
    unary("atanh", [](double x) { return std::atanh(x); });
    unary("fround", [](double x) { return static_cast<double>(to_float(x)); });
    // 21.3.2.17 Math.f16round: the nearest binary16, ties to even, straight
    // from the double - through `float` first would round twice. A binary16
    // has 10 fraction bits and a least exponent of -14, so the unit in the
    // last place is 2^(e-10) with e clamped there, the quotient by it is
    // exact, and nearbyint under the default rounding mode is ties-to-even.
    // 65520 is the halfway point above the largest finite value (65504) and
    // rounds to the even 65536, which binary16 cannot hold: Infinity.
    unary("f16round", [](double x) {
        if (!std::isfinite(x) || x == 0) { return x; }
        const double a = std::fabs(x);
        if (a >= 65520.0) { return std::copysign(std::numeric_limits<double>::infinity(), x); }
        int e = 0;
        (void)std::frexp(a, &e); // a = m * 2^e, m in [0.5, 1).
        const int exponent = std::max(e - 1, -14);
        const double ulp = std::ldexp(1.0, exponent - 10);
        return std::copysign(std::nearbyint(a / ulp) * ulp, x);
    });
    // Coerce through the context before Core ToUint32 so object valueOf hooks run.
    method(cx, math, "clz32", 1, [math_arg](context & c, std::span<value> a) {
        const std::uint32_t x = ctbrowser::number_to_uint32(math_arg(c, a, 0));
        int n = 0;
        for (std::uint32_t bit = 0x80000000u; bit != 0 && (x & bit) == 0; bit >>= 1) { ++n; }
        return value::number(x == 0 ? 32 : n);
    });
    method(cx, math, "imul", 2, [math_arg](context & c, std::span<value> a) {
        // Named locals, so the two coercions happen left to right. As arguments
        // to one expression their order is unspecified in C++ and observable in
        // JavaScript: `Math.imul({valueOf: f}, {valueOf: g})` must call f then g.
        const std::uint32_t x = ctbrowser::number_to_uint32(math_arg(c, a, 0));
        const std::uint32_t y = ctbrowser::number_to_uint32(math_arg(c, a, 1));
        return value::number(static_cast<double>(static_cast<std::int32_t>(x * y)));
    });
    unary("floor", [](double x) { return std::floor(x); });
    unary("ceil", [](double x) { return std::ceil(x); });
    unary("abs", [](double x) { return std::fabs(x); });
    unary("sqrt", [](double x) { return std::sqrt(x); });
    unary("sin", [](double x) { return std::sin(x); });
    unary("cos", [](double x) { return std::cos(x); });
    unary("tan", [](double x) { return std::tan(x); });
    unary("asin", [](double x) { return std::asin(x); });
    unary("acos", [](double x) { return std::acos(x); });
    unary("atan", [](double x) { return std::atan(x); });
    unary("log", [](double x) { return std::log(x); });
    unary("exp", [](double x) { return std::exp(x); });
    unary("trunc", [](double x) { return std::trunc(x); });
    unary("sign", [](double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : x); });
    // JS rounds .5 toward +Infinity (Math.round(-0.5) is -0), unlike
    // std::round, which rounds away from zero.
    //
    // floor(x + 0.5) is also wrong, in three ways:
    //
    //  * The addition rounds: for x = 0.49999999999999994 the sum ties to 1.0,
    //    giving 1 where 21.3.2.28 step 3 requires +0.
    //  * It loses the sign of zero: steps 2 and 4 require -0 for every x in
    //    [-0.5, -0], and floor(-0.5 + 0.5) is +0.
    //  * Above 2^52 the addition is inexact and moves integral values, which
    //    step 2 requires returned unchanged (Math.round(2**53-1) gave 2**53).
    //
    // Comparing x with floor(x) adds nothing to x, and integers and the
    // near-zero ranges are returned before any arithmetic.
    method(cx, math, "round", 1, [math_arg](context & c, std::span<value> a) {
        const double x = math_arg(c, a, 0);
        // NaN, the infinities and every integral value (including both zeros)
        // come straight back (step 2).
        if (!std::isfinite(x) || x == std::floor(x)) { return value::number(x); }
        if (x > 0 && x < 0.5) { return value::number(0.0); }
        if (x < 0 && x >= -0.5) { return value::number(-0.0); }
        const double down = std::floor(x);
        return value::number(x - down >= 0.5 ? down + 1.0 : down);
    });
    // context::exponentiate, not std::pow: C99 says pow(+-1, y) is 1 for every
    // y, including NaN and the infinities, where Number::exponentiate says NaN
    // for exactly those, so `Math.pow(1, NaN)` is NaN.
    //
    // Named locals for the two arguments (here and in atan2, as in imul above):
    // C++ leaves the evaluation order of call arguments unspecified, and the
    // specification requires valueOf on the first argument to run before the
    // second: `Math.pow({valueOf: f}, {valueOf: g})` must call f, then g.
    method(cx, math, "pow", 2, [math_arg](context & c, std::span<value> a) {
        const double base = math_arg(c, a, 0);
        const double exponent = math_arg(c, a, 1);
        return value::number(context::exponentiate(base, exponent));
    });
    method(cx, math, "atan2", 2, [math_arg](context & c, std::span<value> a) {
        const double y = math_arg(c, a, 0);
        const double x = math_arg(c, a, 1);
        return value::number(std::atan2(y, x));
    });
    // hypot is scaled, and Infinity beats NaN. `sqrt(sum of squares)` is wrong
    // at both ends of the range:
    //
    //  * It overflows: `v * v` passes 1.8e308 once |v| is over about 1.34e154,
    //    so `Math.hypot(1e300, 1e300)` would be Infinity rather than 1.41e300.
    //    Dividing through by the largest magnitude makes every term at most 1.
    //  * It underflows: below about 1e-162 the squares flush to zero
    //    (`Math.hypot(3e-300, 4e-300)` would be 0 rather than 5e-300), and just
    //    above that the denormals cost real precision.
    //
    // 21.3.2.18 also orders the special cases: step 3 returns +Infinity for an
    // infinite argument before step 5 looks at NaN, so `Math.hypot(Infinity,
    // NaN)` is Infinity, which is why the scan comes first.
    //
    // The scale factor is the largest magnitude rather than a power of two,
    // which keeps `hypot(3, 4)` exactly 5.
    //
    // Every argument is coerced first, once, in order, and only then are any
    // of them looked at: step 1 of hypot, min and max builds a List called
    // `coerced` before step 3 inspects it. This is observable three ways:
    //
    //  * A `valueOf` that throws aborts at the first such argument and leaves
    //    the later ones uncalled.
    //  * A `valueOf` after a NaN must still be called (`Math.min(NaN, obj)`).
    //  * Each argument is coerced exactly once, so a `valueOf` that returns a
    //    different number each time cannot make two passes disagree.
    const auto coerce_all = [](context & c, std::span<value> a) {
        std::vector<double> out;
        out.reserve(a.size());
        for (const value & v : a) { out.push_back(c.to_number_value(v)); }
        return out;
    };
    method(cx, math, "hypot", 2, [coerce_all](context & c, std::span<value> a) {
        const std::vector<double> coerced = coerce_all(c, a);
        bool saw_nan = false;
        double largest = 0;
        for (const double x : coerced) {
            if (std::isinf(x)) { return value::number(std::numeric_limits<double>::infinity()); }
            if (std::isnan(x)) {
                saw_nan = true;
                continue;
            }
            largest = std::max(largest, std::fabs(x));
        }
        if (saw_nan) { return value::number(std::nan("")); }
        // Every argument was a zero, or there were none at all.
        if (largest == 0) { return value::number(0.0); }
        double total = 0;
        for (const double x : coerced) {
            const double scaled = x / largest;
            total += scaled * scaled;
        }
        return value::number(largest * std::sqrt(total));
    });
    // 21.3.2.35 Math.sumPrecise: the exact sum of an iterable of Numbers,
    // rounded once. Every double is an integer multiple of 2^-1074, so the sum
    // is kept as a bigint in that unit (at most ~2100 bits) and rounded to
    // nearest-even by hand at the end; that is what makes
    // `[1e308, 1e308, -1e308, -1e308, 0.1, 0.1]` come out 0.2 rather than NaN.
    // An element that is not a Number is a TypeError, uncoerced, and closes
    // the iterator (steps 5.b.iii-iv).
    method(cx, math, "sumPrecise", 1, [](context & c, std::span<value> a) {
        const value iterator = c.get_iterator(arg_at(a, 0));
        if (c.throw_pending() || !iterator.is_object_like()) { return value::undefined(); }
        const context::rooted keep_iterator{c, iterator};
        const value next = c.lookup_property(iterator, "next");
        if (c.throw_pending()) { return value::undefined(); }
        const auto close = [&] {
            const value ret = c.lookup_property(iterator, "return");
            if (ret.is_callable()) { (void)c.call(ret, std::span<const value>{}, iterator); }
        };
        enum class state {
            minus_zero,
            finite,
            plus_inf,
            minus_inf,
            nan
        } st = state::minus_zero;
        bigint total = 0;
        for (;;) {
            bool done = false;
            const value item = c.iterator_step(iterator, next, done);
            if (c.throw_pending()) { return value::undefined(); }
            if (done) { break; }
            if (!item.is_number()) {
                close();
                c.throw_error("TypeError", "Math.sumPrecise: every element must be a Number");
                return value::undefined();
            }
            const double x = item.as_number();
            if (std::isnan(x)) {
                st = state::nan;
            } else if (std::isinf(x)) {
                const state want = x > 0 ? state::plus_inf : state::minus_inf;
                if (st == state::nan ||
                    (st != want && st != state::minus_zero && st != state::finite)) {
                    st = state::nan;
                } else if (st != state::nan) {
                    st = want;
                }
            } else if (st == state::minus_zero || st == state::finite) {
                if (x != 0 || !std::signbit(x)) { st = state::finite; }
                int exp = 0;
                const double mant = std::frexp(x, &exp); // x = mant * 2^exp, |mant| in [0.5, 1).
                auto scaled = static_cast<std::int64_t>(std::ldexp(mant, 53));
                const int shift = exp - 53 + 1074;
                // A subnormal has fewer than 53 significant bits: the shift is
                // negative and the low bits it drops are zero.
                if (shift < 0) {
                    total += bigint{scaled >> -shift};
                } else {
                    total += bigint{scaled} << shift;
                }
            }
        }
        switch (st) {
        case state::nan: return value::number(std::nan(""));
        case state::plus_inf: return value::number(std::numeric_limits<double>::infinity());
        case state::minus_inf: return value::number(-std::numeric_limits<double>::infinity());
        case state::minus_zero: return value::number(-0.0);
        case state::finite: break;
        }
        if (total == 0) { return value::number(0.0); }
        const bool negative = total < 0;
        bigint magnitude = negative ? -total : total;
        const auto bits = static_cast<int>(boost::multiprecision::msb(magnitude)) + 1;
        int shift = std::max(bits - 53, 0);
        bigint q = magnitude >> shift;
        if (shift > 0) {
            const bigint rem = magnitude - (q << shift);
            const bigint half = bigint{1} << (shift - 1);
            if (rem > half || (rem == half && (q & 1) != 0)) { q += 1; }
            if (q == (bigint{1} << 53)) {
                q >>= 1;
                ++shift;
            }
        }
        const double out =
            std::ldexp(static_cast<double>(q.convert_to<std::int64_t>()), shift - 1074);
        return value::number(negative ? -out : out);
    });
    // min/max with no arguments are Infinity and -Infinity, which is what makes
    // `Math.max(...list)` on an empty list behave.
    //
    // Not std::min/std::max: both use `<`, which is false for anything
    // involving NaN, so `std::min(1.0, NaN)` is 1 where 21.3.2.24 step 4.a
    // returns NaN. And `-0 < +0` is false, so which zero came back would depend
    // on argument order; step 4.b makes -0 smaller than +0, and so does this.
    method(cx, math, "min", 2, [coerce_all](context & c, std::span<value> a) {
        const std::vector<double> coerced = coerce_all(c, a);
        double best = std::numeric_limits<double>::infinity();
        for (const double x : coerced) {
            if (std::isnan(x)) { return value::number(std::nan("")); }
            if (x < best || (x == 0 && best == 0 && std::signbit(x))) { best = x; }
        }
        return value::number(best);
    });
    method(cx, math, "max", 2, [coerce_all](context & c, std::span<value> a) {
        const std::vector<double> coerced = coerce_all(c, a);
        double best = -std::numeric_limits<double>::infinity();
        for (const double x : coerced) {
            if (std::isnan(x)) { return value::number(std::nan("")); }
            if (x > best || (x == 0 && best == 0 && std::signbit(best))) { best = x; }
        }
        return value::number(best);
    });
    // xorshift64*, held in the closure so each context has its own stream. The
    // top 53 bits of the product, divided by 2^53 (9007199254740992), give a
    // double in [0, 1).
    const auto state = std::make_shared<std::uint64_t>(seed == 0 ? 1 : seed);
    method(cx, math, "random", 0, [state](context &, std::span<value>) {
        std::uint64_t x = *state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        *state = x;
        const std::uint64_t bits = x * 0x2545F4914F6CDD1DULL;
        return value::number(static_cast<double>(bits >> 11) / 9007199254740992.0);
    });
    math->define(CTBROWSER_SYMBOL_KEY("toStringTag"), cx.string("Math"),
                 attr_configurable); // 21.3.1.9
    cx.define_global("Math", value::object(math));
}

/// Installs the global `Boolean` and Boolean.prototype.
void install_boolean(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * boolean_proto = new_table(cx);
    // thisBooleanValue (20.3.3): a Boolean, a wrapper's [[BooleanData]], or
    // Boolean.prototype itself (whose slot is false); anything else is a
    // TypeError rather than a truthiness test, so
    // `Boolean.prototype.toString.call({})` throws.
    const auto this_boolean_value = [](context & c, const char * method, bool & out) {
        const value self = c.current_this();
        if (self.is_boolean()) {
            out = self.as_boolean();
            return true;
        }
        if (value * slot = primitive_slot(self); slot != nullptr && slot->is_boolean()) {
            out = slot->as_boolean();
            return true;
        }
        if (self.is_object() && self.as_heap() == c.prototype(context::proto_kind::boolean)) {
            out = false;
            return true;
        }
        c.throw_error("TypeError", std::string{method} + " requires that 'this' be a Boolean");
        return false;
    };
    method(cx, boolean_proto, "toString", 0, [this_boolean_value](context & c, std::span<value>) {
        bool b = false;
        if (!this_boolean_value(c, "Boolean.prototype.toString", b)) { return value::undefined(); }
        return c.string(b ? "true" : "false");
    });
    method(cx, boolean_proto, "valueOf", 0, [this_boolean_value](context & c, std::span<value>) {
        bool b = false;
        if (!this_boolean_value(c, "Boolean.prototype.valueOf", b)) { return value::undefined(); }
        return value::boolean(b);
    });
    cx.set_prototype(context::proto_kind::boolean, boolean_proto);
    // 20.3.1.1: a call converts, `new` wraps; see detail::wrap_primitive.
    auto * boolean_ctor =
        cx.allocate<native_object>("Boolean", [](context & c, std::span<value> a) {
            const value made = value::boolean(!a.empty() && context::truthy(a[0]));
            const value self = c.current_this();
            return detail::constructing_this(self) ? detail::wrap_primitive(c, self, made) : made;
        });
    detail::constant(boolean_ctor, "prototype", value::object(boolean_proto));
    link_constructor(cx, boolean_proto, "Boolean", 1, value::object(boolean_ctor));
    cx.define_global("Boolean", value::object(boolean_ctor));
}

/// Installs the global `Number`, its static constants and predicates, and
/// Number.prototype.
void install_number(context & cx) {
    using detail::method;
    using detail::new_table;

    // Number(value) uses to_number_value rather than the static to_number, so
    // an object argument goes through ToPrimitive (`Number([])` is 0) and can
    // call valueOf; the static form cannot re-enter the VM.
    auto * number_ctor = cx.allocate<native_object>("Number", [](context & c, std::span<value> a) {
        // The explicit conversion, which a BigInt permits, unlike every
        // implicit one. Past the double range it saturates to an infinity.
        value made = value::number(0.0);
        if (!a.empty() && a[0].is_kind(heap_kind::bigint)) {
            made = value::number(
                bigint_to_double(static_cast<bigint_object *>(a[0].as_heap())->digits));
        } else if (!a.empty()) {
            // ToNumeric (21.1.1.1 step 1): a Symbol refuses (7.1.4).
            if (!numeric_arg(c, a[0])) { return value::undefined(); }
            made = value::number(c.to_number_value(a[0]));
            if (c.throw_pending()) { return value::undefined(); }
        }
        // 21.1.1.1 step 3: a call converts, `new` wraps; see
        // detail::wrap_primitive.
        const value self = c.current_this();
        return detail::constructing_this(self) ? detail::wrap_primitive(c, self, made) : made;
    });
    const auto constant = [&](const char * name, double v) {
        detail::constant(number_ctor, name, value::number(v));
    };
    constant("EPSILON", 2.220446049250313e-16);
    constant("MAX_SAFE_INTEGER", 9007199254740991.0);
    constant("MIN_SAFE_INTEGER", -9007199254740991.0);
    constant("MAX_VALUE", 1.7976931348623157e308);
    constant("MIN_VALUE", 5e-324);
    constant("POSITIVE_INFINITY", std::numeric_limits<double>::infinity());
    constant("NEGATIVE_INFINITY", -std::numeric_limits<double>::infinity());
    constant("NaN", std::nan(""));
    // detail::method, not `set`: clause 17 makes each of these
    // { writable: true, enumerable: false, configurable: true } with a `length`
    // of 1, so `Object.keys` and a for-in over `Number` must not see them.
    const auto predicate = [&](const char * name, bool (*fn)(const value &)) {
        detail::method(cx, number_ctor, name, 1, [fn](context &, std::span<value> a) {
            const value v = arg_at(a, 0);
            return value::boolean(fn(v));
        });
    };
    // These do not coerce: `Number.isFinite("1")` is false where the global
    // `isFinite("1")` is true, and code uses the difference deliberately.
    predicate("isFinite",
              [](const value & v) { return v.is_number() && std::isfinite(v.as_number()); });
    predicate("isNaN", [](const value & v) { return v.is_number() && std::isnan(v.as_number()); });
    predicate("isInteger", [](const value & v) {
        return v.is_number() && std::isfinite(v.as_number()) &&
               v.as_number() == std::trunc(v.as_number());
    });
    predicate("isSafeInteger", [](const value & v) {
        return v.is_number() && std::isfinite(v.as_number()) &&
               v.as_number() == std::trunc(v.as_number()) &&
               std::abs(v.as_number()) <= 9007199254740991.0;
    });
    cx.define_global("Number", value::object(number_ctor));

    object_object * number_proto = new_table(cx);
    // A digit count out of range is a RangeError, not a clamp (21.1.3.3 step 4,
    // 21.1.3.2 step 5, 21.1.3.5 step 5); the limit is 100, and Infinity counts
    // as out of range (integer_arg preserves it). Coercing the argument and
    // checking its range are separate because toExponential and toPrecision
    // return early for a non-finite receiver between the two
    // (`Infinity.toPrecision(1000)` is "Infinity"), while toFixed checks the
    // range first (`NaN.toFixed(101)` throws). refuse_digits returns true, with
    // the RangeError thrown, when `n` is outside [low, high].
    const auto refuse_digits = [](context & c, const char * method, double n, double low,
                                  double high) {
        if (n >= low && n <= high) { return false; }
        c.throw_error("RangeError", std::string{method} + "() argument must be between " +
                                        number_to_string(low) + " and " + number_to_string(high));
        return true;
    };
    method(cx, number_proto, "toFixed", 1, [refuse_digits](context & c, std::span<value> a) {
        const detail::unwind_watch watch{c};
        const double self = detail::this_number_value(c, "Number.prototype.toFixed");
        if (watch.threw()) { return c.string(""); }
        const double asked = integer_arg(c, a, 0);
        if (watch.threw()) { return c.string(""); } // A valueOf or a Symbol threw.
        if (refuse_digits(c, "toFixed", asked, 0, 100)) { return c.string(""); }
        const auto digits = static_cast<int>(asked);
        // Not snprintf("%.*f"): it is locale-dependent, it prints a thousand
        // digits past 1e21 where the specification hands back ToString, and it
        // rounds a tie to even where toFixed rounds away from zero
        // (`(0.5).toFixed(0)` is "1").
        return c.string(number_to_fixed(self, digits));
    });
    method(cx, number_proto, "toString", 1, [](context & c, std::span<value> a) {
        const double v = detail::this_number_value(c, "Number.prototype.toString");
        // An out-of-range radix is a RangeError (21.1.3.6 step 4). The radix
        // goes through ToIntegerOrInfinity rather than a cast, because casting
        // NaN or Infinity to int is undefined behaviour; NaN becomes 0, which
        // is out of range. Everything below stringifies the number `v`, never
        // the receiver, so it cannot re-enter this native.
        const double asked = a.empty() || a[0].is_undefined() ? 10.0 : integer_arg(c, a, 0);
        const int radix = asked >= 2 && asked <= 36 ? static_cast<int>(asked) : 0;
        if (radix < 2 || radix > 36) {
            c.throw_error("RangeError", "toString() radix must be between 2 and 36");
            return c.string("");
        }
        if (radix == 10 || std::isnan(v) || std::isinf(v)) { return c.string(number_to_string(v)); }
        const bool negative = v < 0;
        const double magnitude = std::fabs(v);
        constexpr std::string_view digits = "0123456789abcdefghijklmnopqrstuvwxyz";
        double whole = std::floor(magnitude);
        std::string out;
        if (whole == 0) {
            out = "0";
        } else {
            while (whole >= 1) {
                const auto digit =
                    static_cast<std::size_t>(std::fmod(whole, static_cast<double>(radix)));
                out.insert(out.begin(), digits[digit]);
                whole = std::floor(whole / radix);
            }
        }
        // The fraction, to as many places as a double can distinguish. A
        // fixed count would print 0.1 in binary as 0.0999... or drop it.
        double fraction = magnitude - std::floor(magnitude);
        if (fraction > 0) {
            out += '.';
            for (int place = 0; place < 52 && fraction > 0; ++place) {
                fraction *= radix;
                const auto digit = static_cast<std::size_t>(std::floor(fraction));
                out += digits[std::min<std::size_t>(digit, 35)];
                fraction -= std::floor(fraction);
            }
        }
        return c.string(negative ? "-" + out : out);
    });
    method(cx, number_proto, "valueOf", 0, [](context & c, std::span<value>) {
        return value::number(detail::this_number_value(c, "Number.prototype.valueOf"));
    });
    method(cx, number_proto, "toExponential", 1, [refuse_digits](context & c, std::span<value> a) {
        const detail::unwind_watch watch{c};
        const double v = detail::this_number_value(c, "Number.prototype.toExponential");
        if (watch.threw()) { return c.string(""); }
        // No argument does not mean six. The specification asks for as many digits as
        // uniquely specify the value, so `(5).toExponential()` is "5e+0" and
        // not "5.000000e+0"; -1 is how number_to_exponential is told that.
        if (a.empty() || a[0].is_undefined()) { return c.string(number_to_exponential(v, -1)); }
        // The argument is coerced (step 2) even when the receiver is not finite
        // and the answer cannot depend on it, so a `valueOf` on it still runs
        // (test262's toExponential/nan.js counts that call).
        const double asked = integer_arg(c, a, 0);
        if (watch.threw()) { return c.string(""); }
        if (!std::isfinite(v)) { return c.string(number_to_string(v)); }
        if (refuse_digits(c, "toExponential", asked, 0, 100)) { return c.string(""); }
        return c.string(number_to_exponential(v, static_cast<int>(asked)));
    });
    method(cx, number_proto, "toPrecision", 1, [refuse_digits](context & c, std::span<value> a) {
        const detail::unwind_watch watch{c};
        const double v = detail::this_number_value(c, "Number.prototype.toPrecision");
        if (watch.threw()) { return c.string(""); }
        // No argument at all is ToString of the number (not of the receiver,
        // which could re-enter toString; see this_number_value), not zero
        // significant digits.
        if (a.empty() || a[0].is_undefined()) { return c.string(number_to_string(v)); }
        // The same step ordering as toExponential above, and 21.1.3.5's lower
        // bound is one rather than zero: there is no such thing as a number
        // written to no significant digits.
        const double asked = integer_arg(c, a, 0);
        if (watch.threw()) { return c.string(""); }
        if (!std::isfinite(v)) { return c.string(number_to_string(v)); }
        if (refuse_digits(c, "toPrecision", asked, 1, 100)) { return c.string(""); }
        // Not "%.*g", which drops trailing zeros where toPrecision keeps them
        // (`(1.5).toPrecision(3)` is "1.50") and writes two exponent digits.
        return c.string(number_to_precision(v, static_cast<int>(asked)));
    });
    // 21.1.3.4 toLocaleString. There is no ECMA-402 here, so it is ToString of
    // the number, which the specification allows an implementation without an
    // Intl library to do.
    method(cx, number_proto, "toLocaleString", 0, [](context & c, std::span<value>) {
        return c.string(
            number_to_string(detail::this_number_value(c, "Number.prototype.toLocaleString")));
    });
    detail::constant(number_ctor, "prototype", value::object(number_proto));
    link_constructor(cx, number_proto, "Number", 1, value::object(number_ctor));
    cx.set_prototype(context::proto_kind::number, number_proto);
}

/// Installs parseInt, parseFloat and the value globals (NaN, Infinity,
/// undefined), and aliases Number.parseInt / Number.parseFloat to them.
void install_globals(context & cx) {
    using detail::method;
    using detail::new_table;
    // 19.2.5 gives parseInt two parameters and 19.2.4 gives parseFloat one.
    detail::global_fn(cx, "parseInt", 2, [](context & c, std::span<value> a) {
        // 19.2.5: ? ToString(string) and ? ToInt32(radix) here, the rest in
        // ctbrowser::parse_int, which native code calls too.
        const std::string s = str_at(c, a, 0);
        if (c.throw_pending()) { return value::undefined(); }
        const value radix_arg = arg_at(a, 1);
        if (!numeric_arg(c, radix_arg)) { return value::undefined(); }
        const double radix_number = c.to_number_value(radix_arg);
        if (c.throw_pending()) { return value::undefined(); }
        return value::number(parse_int(
            s, static_cast<std::int32_t>(context::to_int32(value::number(radix_number)))));
    });
    detail::global_fn(cx, "parseFloat", 1, [](context & c, std::span<value> a) {
        // string_to_number_prefix, not std::stod: stod reads LC_NUMERIC for the
        // decimal separator, so `parseFloat("1.5")` would answer 1 on a host
        // whose locale writes a comma, and rendered output is byte-compared
        // across platforms.
        return value::number(string_to_number_prefix(str_at(c, a, 0)));
    });

    // The value properties of the global object (19.1).
    cx.define_global("NaN", value::number(std::nan("")));
    cx.define_global("Infinity", value::number(std::numeric_limits<double>::infinity()));
    cx.define_global("undefined", value::undefined());

    // `Number.parseFloat` and `Number.parseInt` are the same function objects
    // as the globals (21.1.2.12 and 21.1.2.13), so this is an alias, not a
    // second copy. Installed here, not in install_number, because
    // install_builtins runs install_number before this function, when neither
    // global exists yet.
    if (const value number = cx.global("Number"); number.is_kind(heap_kind::native)) {
        for (const char * name : {"parseInt", "parseFloat"}) {
            static_cast<native_object *>(number.as_heap())
                ->define(name, cx.global(name), attr_builtin);
        }
    }
}

} // namespace ctbrowser::script::builtins_detail
