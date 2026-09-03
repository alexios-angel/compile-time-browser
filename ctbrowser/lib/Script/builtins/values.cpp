// ctbrowser.script builtins - Math, Boolean, Number, Date, and the bare globals.
//
// One of five files carved out of a 4,118-line builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in internal.hpp.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

// Math
void install_math(context & cx, std::uint64_t seed) {
    using detail::method;
    using detail::new_table;
    object_object * math = new_table(cx);
    // FROM <numbers>, not written out by hand. A transcribed constant is a digit
    // waiting to be wrong, and one that is wrong in its last few places is
    // invisible: it agrees with every printed value a test is likely to check
    // and disagrees with the real one by an amount that accumulates.
    detail::constant(math, "PI", value::number(std::numbers::pi));
    detail::constant(math, "E", value::number(std::numbers::e));
    // A MATH ARGUMENT, WHERE A MISSING ONE IS NaN AND NOT ZERO.
    //
    // Every Math function's step 1 is `Let n be ? ToNumber(x)`, an absent
    // argument is `undefined`, and ToNumber(undefined) is NaN - so `Math.sin()`
    // is NaN, not sin(0). The shared `num_at` substitutes 0.0 instead, and that
    // is RIGHT for its other callers: `"abc".slice()` and `[1,2].indexOf(x)`
    // want a zero default. So Math gets its own accessor rather than the shared
    // one changing under thirty-odd call sites that depend on the zero.
    // The context is threaded through so an OBJECT argument coerces through
    // ToPrimitive: `Math.abs([])` is 0 and `Math.max([1],[2])` is 2, where the
    // static to_number answers NaN for anything on the heap.
    const auto math_arg = [](context & c, std::span<value> a, std::size_t i) {
        return i < a.size() ? c.to_number_value(a[i]) : std::nan("");
    };
    const auto unary = [&](std::string name, double (*fn)(double)) {
        method(cx, math, name, [fn, math_arg](context & c, std::span<value> a) {
            return value::number(fn(math_arg(c, a, 0)));
        });
    };
    detail::constant(math, "SQRT2", value::number(std::numbers::sqrt2));
    // <numbers> has no SQRT1_2 or LN10, so those two are DERIVED rather than
    // transcribed - neither can be typed wrong.
    //
    // SQRT1_2 IS sqrt2/2 AND NOT 1/sqrt2, which is not the same double. The
    // reciprocal was one ulp low - 0.7071067811865475 where the correctly
    // rounded value is 0.7071067811865476 - because it rounds twice, once in
    // sqrt2 and again in the division. Halving is EXACT (it decrements the
    // binary exponent and touches no bit of the mantissa), so this rounds once.
    // 21.3.1.8 asks for the Number value NEAREST the true root, which makes it
    // an exact requirement rather than an approximation; the engine used to
    // disagree with its own `Math.sqrt(0.5)`.
    detail::constant(math, "SQRT1_2", value::number(std::numbers::sqrt2 / 2.0));
    detail::constant(math, "LN2", value::number(std::numbers::ln2));
    detail::constant(math, "LN10", value::number(std::numbers::ln10));
    detail::constant(math, "LOG2E", value::number(std::numbers::log2e));
    detail::constant(math, "LOG10E", value::number(std::numbers::log10e));
    // THE EXACT ROOT WHEN THERE IS ONE. glibc's `cbrt` is up to an ulp out on a
    // perfect cube - `cbrt(27)` is 3.0000000000000004 and `cbrt(216)` is
    // 6.0000000000000009 - where V8 returns 3 and 6. That was invisible while
    // this engine printed numbers to six decimals, and `unittests/js/vm_basics` was
    // asserting "3" against a value that was never 3; full-precision printing
    // is what exposed it.
    //
    // Only the exact case is corrected, and deliberately so: a Newton step
    // refines 27 and 216 and makes `cbrt(0.001)` a worse answer than the one
    // libm already gives. Non-cubes still come from the host's libm and may sit
    // an ulp from V8.
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
    unary("fround", [](double x) { return static_cast<double>(static_cast<float>(x)); });
    method(cx, math, "clz32", [](context &, std::span<value> a) {
        const std::uint32_t x = context::to_uint32(arg_at(a, 0));
        int n = 0;
        for (std::uint32_t bit = 0x80000000u; bit != 0 && (x & bit) == 0; bit >>= 1) { ++n; }
        return value::number(x == 0 ? 32 : n);
    });
    method(cx, math, "imul", [](context &, std::span<value> a) {
        return value::number(static_cast<double>(static_cast<std::int32_t>(
            context::to_uint32(arg_at(a, 0)) * context::to_uint32(arg_at(a, 1)))));
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
    // JS rounds .5 toward POSITIVE infinity, so Math.round(-0.5) is -0 and not
    // -1. std::round rounds away from zero and gets that wrong.
    //
    // AND `std::floor(x + 0.5)` GETS IT WRONG THREE OTHER WAYS, all of which
    // this used to have and none of which is visible from reading it:
    //
    //  * `x + 0.5` ROUNDS. For x = 0.49999999999999994 the sum is exactly
    //    halfway between 1-2^-53 and 1, ties-to-even lifts it to 1.0, and floor
    //    then answers 1 where 21.3.2.28 step 3 requires +0.
    //  * `+ 0.5` DESTROYS THE SIGN OF ZERO. Steps 2 and 4 require -0 back from
    //    every x in [-0.5, -0], and floor(-0.5 + 0.5) is +0. The comment above
    //    claimed this case worked; only the -1 half of it did.
    //  * ABOVE 2^52 the addition is inexact, so an integral Number is MOVED
    //    where step 2 requires it returned unchanged - Math.round(2**53-1) came
    //    back as 2**53, out of the safe-integer range entirely.
    //
    // Splitting on floor(x) instead adds nothing to x, so none of the three can
    // happen: an integral value is already its own floor, and the zero cases are
    // handled before any arithmetic.
    method(cx, math, "round", [math_arg](context & c, std::span<value> a) {
        const double x = math_arg(c, a, 0);
        // NaN, the infinities and every integral value (including both zeros)
        // come straight back - step 2.
        if (!std::isfinite(x) || x == std::floor(x)) { return value::number(x); }
        if (x > 0 && x < 0.5) { return value::number(0.0); }
        if (x < 0 && x >= -0.5) { return value::number(-0.0); }
        const double down = std::floor(x);
        return value::number(x - down >= 0.5 ? down + 1.0 : down);
    });
    // C99 says pow(+-1, y) is 1 for EVERY y, including NaN and the infinities;
    // Number::exponentiate says NaN for exactly those. Three lines of special
    // case, and without them `Math.pow(1, NaN)` was 1 - a value a page will
    // happily do arithmetic on rather than checking with isNaN.
    method(cx, math, "pow", [math_arg](context & c, std::span<value> a) {
        return value::number(context::exponentiate(math_arg(c, a, 0), math_arg(c, a, 1)));
    });
    method(cx, math, "atan2", [math_arg](context & c, std::span<value> a) {
        return value::number(std::atan2(math_arg(c, a, 0), math_arg(c, a, 1)));
    });
    // SCALED, AND INFINITY BEATS NaN. `sqrt(sum of squares)` is the obvious
    // shape and is wrong at both ends of the range:
    //
    //  * it OVERFLOWS. `v * v` passes 1.8e308 once |v| is over about 1.34e154,
    //    so `Math.hypot(1e300, 1e300)` was Infinity where the true answer,
    //    1.41e300, is a perfectly ordinary double. Dividing through by the
    //    largest magnitude first makes every term at most 1.
    //  * it UNDERFLOWS. Below about 1e-162 the squares flush to zero and the
    //    answer came back 0 - `Math.hypot(3e-300, 4e-300)` was 0 rather than
    //    5e-300 - and just above that the denormals cost real precision:
    //    hypot(2e-162, 3e-162) was out by 6.8%.
    //
    // 21.3.2.18 also ORDERS the special cases: step 3 returns +Infinity for an
    // infinite argument BEFORE step 5 looks at NaN, so `Math.hypot(Infinity,
    // NaN)` is Infinity and not NaN. A single accumulating loop cannot express
    // that ordering, which is why the scan comes first.
    //
    // The scale factor is the largest magnitude rather than a power of two, and
    // that is deliberate: it keeps `hypot(3, 4)` exactly 5 and leaves the
    // already-correct mid-range answers bit-identical.
    method(cx, math, "hypot", [](context & c, std::span<value> a) {
        bool saw_nan = false;
        double largest = 0;
        for (const value & v : a) {
            const double x = c.to_number_value(v);
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
        for (const value & v : a) {
            const double scaled = c.to_number_value(v) / largest;
            total += scaled * scaled;
        }
        return value::number(largest * std::sqrt(total));
    });
    // min/max with no arguments are Infinity and -Infinity, which is what makes
    // `Math.max(...list)` on an empty list behave.
    //
    // NOT std::min/std::max, which are the wrong function twice over. Both are
    // written in terms of `<`, and IEEE comparison says NO to everything
    // involving NaN - so `std::min(1.0, NaN)` is 1 and the NaN vanishes, where
    // 21.3.2.24 step 4.a returns NaN the moment it sees one. And `-0 < +0` is
    // false, so which zero came back depended on ARGUMENT ORDER: `Math.min(0,
    // -0)` gave +0 while `Math.min(-0, 0)` was accidentally right. Steps 4.b
    // make the zero ordering explicit, and so does this.
    method(cx, math, "min", [](context & c, std::span<value> a) {
        double best = std::numeric_limits<double>::infinity();
        for (const value & v : a) {
            const double x = c.to_number_value(v);
            if (std::isnan(x)) { return value::number(std::nan("")); }
            if (x < best || (x == 0 && best == 0 && std::signbit(x))) { best = x; }
        }
        return value::number(best);
    });
    method(cx, math, "max", [](context & c, std::span<value> a) {
        double best = -std::numeric_limits<double>::infinity();
        for (const value & v : a) {
            const double x = c.to_number_value(v);
            if (std::isnan(x)) { return value::number(std::nan("")); }
            if (x > best || (x == 0 && best == 0 && std::signbit(best))) { best = x; }
        }
        return value::number(best);
    });
    // xorshift64*, held in the closure so each context has its own stream.
    auto state = std::make_shared<std::uint64_t>(seed == 0 ? 1 : seed);
    method(cx, math, "random", [state](context &, std::span<value>) {
        std::uint64_t x = *state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        *state = x;
        const std::uint64_t bits = x * 0x2545F4914F6CDD1DULL;
        return value::number(static_cast<double>(bits >> 11) / 9007199254740992.0);
    });
    cx.define_global("Math", value::object(math));
}

void install_boolean(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * boolean_proto = new_table(cx);
    method(cx, boolean_proto, "toString", [](context & c, std::span<value>) {
        return c.string(context::truthy(c.current_this()) ? "true" : "false");
    });
    method(cx, boolean_proto, "valueOf", [](context & c, std::span<value>) {
        return value::boolean(context::truthy(c.current_this()));
    });
    cx.set_prototype(context::proto_kind::boolean, boolean_proto);
    auto * boolean_ctor = cx.allocate<native_object>("Boolean", [](context &, std::span<value> a) {
        return value::boolean(!a.empty() && context::truthy(a[0]));
    });
    // A CONVERSION, not a constructor of wrappers - see context::construct. `new
    // Boolean(x)` evaluates to the converted value here rather than to a wrapper
    // object; before the flag it evaluated to an empty object and the value was
    // gone.
    detail::constant(boolean_ctor, "__conversion", value::boolean(true));
    detail::constant(boolean_ctor, "prototype", value::object(boolean_proto));
    link_constructor(cx, boolean_proto, "Boolean", value::object(boolean_ctor));
    cx.define_global("Boolean", value::object(boolean_ctor));
}

void install_number(context & cx) {
    using detail::method;
    using detail::new_table;

    // `Number` as a namespace as well as a coercion. It was only the latter,
    // so every `Number.isFinite(x)` guard in a page read undefined and called
    // it - the failure landing well away from the test that caused it.
    // to_number_value, not the static to_number: `Number([])` is 0 and
    // `Number({valueOf(){return 7}})` is 7, because ToNumber of an object goes
    // through ToPrimitive. The static form cannot re-enter the VM to call
    // valueOf, so it answered NaN for every object.
    auto * number_ctor = cx.allocate<native_object>("Number", [](context & c, std::span<value> a) {
        // The EXPLICIT conversion, which a BigInt permits - unlike every
        // implicit one. Past the double range it saturates to an infinity.
        if (!a.empty() && a[0].is_kind(heap_kind::bigint)) {
            return value::number(
                bigint_to_double(static_cast<bigint_object *>(a[0].as_heap())->digits));
        }
        return value::number(a.empty() ? 0.0 : c.to_number_value(a[0]));
    });
    // A CONVERSION, not a constructor of wrappers - see context::construct. `new
    // Number(x)` evaluates to the converted value here rather than to a wrapper
    // object; before the flag it evaluated to an empty object and the value was
    // gone.
    detail::constant(number_ctor, "__conversion", value::boolean(true));
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
    const auto predicate = [&](const char * name, bool (*fn)(const value &)) {
        number_ctor->set(name, value::object(cx.allocate<native_object>(
                                   name, [fn](context &, std::span<value> a) {
                                       const value v = arg_at(a, 0);
                                       return value::boolean(fn(v));
                                   })));
    };
    // These do NOT coerce - `Number.isFinite("1")` is false where the global
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
    method(cx, number_proto, "toFixed", [](context & c, std::span<value> a) {
        const double self = detail::this_number_value(c, "Number.prototype.toFixed");
        const auto digits = static_cast<int>(std::clamp(num_at(a, 0), 0.0, 20.0));
        // NOT snprintf("%.*f"): it is locale-dependent, it prints a thousand
        // digits past 1e21 where the specification hands back ToString, and it
        // rounds a tie to EVEN where toFixed rounds away from zero - so
        // `(0.5).toFixed(0)` came out "0" and `(8.5).toFixed(0)` came out "8".
        return c.string(number_to_fixed(self, digits));
    });
    // `toString(radix)` HONOURS ITS RADIX. Ignoring it is not a small gap:
    // `n.toString(16)` is how essentially every program turns a colour channel
    // into hex, and dropping the argument returned the DECIMAL digits - so
    // `'#' + (220).toString(16)` came out as "#220" rather than "#dc". That is
    // a string a colour parser can neither reject nor read correctly, which is
    // how p5.js ended up filling a sketch's background with white.
    method(cx, number_proto, "toString", [](context & c, std::span<value> a) {
        const double v = detail::this_number_value(c, "Number.prototype.toString");
        // AN OUT-OF-RANGE RADIX IS A RangeError (21.1.3.6 step 4), not a silent
        // fall back to 10 - and that fall back was the segfault: it reached
        // `c.to_string(c.current_this())`, which for an object receiver calls
        // this native again. Nothing below asks the CONTEXT to stringify the
        // receiver any more; it stringifies the NUMBER, which cannot re-enter.
        const int radix =
            a.empty() || a[0].is_undefined() ? 10 : static_cast<int>(c.to_number_value(a[0]));
        if (radix < 2 || radix > 36) {
            c.throw_error("RangeError", "toString() radix must be between 2 and 36");
            return c.string("");
        }
        if (radix == 10 || std::isnan(v) || std::isinf(v)) { return c.string(number_to_string(v)); }
        const bool negative = v < 0;
        double magnitude = std::fabs(v);
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
    method(cx, number_proto, "valueOf", [](context & c, std::span<value>) {
        return value::number(detail::this_number_value(c, "Number.prototype.valueOf"));
    });
    method(cx, number_proto, "toExponential", [](context & c, std::span<value> a) {
        const double v = detail::this_number_value(c, "Number.prototype.toExponential");
        // NO ARGUMENT IS NOT SIX. The specification asks for as many digits as
        // uniquely specify the value, so `(5).toExponential()` is "5e+0" and
        // not "5.000000e+0"; -1 is how number_to_exponential is told that.
        const int places = a.empty() || a[0].is_undefined()
                               ? -1
                               : std::clamp(static_cast<int>(context::to_number(a[0])), 0, 100);
        return c.string(number_to_exponential(v, places));
    });
    method(cx, number_proto, "toPrecision", [](context & c, std::span<value> a) {
        const double v = detail::this_number_value(c, "Number.prototype.toPrecision");
        // No argument at all is toString, not zero significant digits - of the
        // NUMBER, not of the receiver. `c.to_string(c.current_this())` here was
        // the second half of the toString cycle: see this_number_value.
        if (a.empty() || a[0].is_undefined()) { return c.string(number_to_string(v)); }
        const int digits = std::clamp(static_cast<int>(context::to_number(a[0])), 1, 100);
        // NOT "%.*g", which drops trailing zeros where toPrecision keeps them -
        // `(1.5).toPrecision(3)` is "1.50" - and writes two exponent digits.
        return c.string(number_to_precision(v, digits));
    });
    detail::constant(number_ctor, "prototype", value::object(number_proto));
    link_constructor(cx, number_proto, "Number", value::object(number_ctor));
    cx.set_prototype(context::proto_kind::number, number_proto);
}

// Date
// `Date` - CONSTRUCTIBLE, and reading a calendar out of a millisecond count.
//
// It was a namespace with `now()` on it and nothing else, so `new Date()` was
// "Date is not a function". p5 exposes day()/month()/year()/hour() and every
// one of them builds a Date, so a sketch showing a clock - which is most
// beginners' second sketch - failed on its first line.
//
// UTC only, and no parsing: `new Date(string)` is a calendar and a timezone
// database, which is a different project. What is here is the civil date
// arithmetic that turns a millisecond count into fields and back, which is what
// a page reading the clock actually needs.
void install_date(context & cx) {
    using detail::method;
    using detail::new_table;

    object_object * date_proto = new_table(cx);

    // Days since the epoch to y/m/d, by Howard Hinnant's civil_from_days - the
    // standard branch-free algorithm, valid for any year a double can hold.
    // Written out rather than reached for through <chrono>'s calendar types
    // because those are C++20 library, and this file is the standard library
    // for a different language.
    const auto civil_from_days = [](long long z, int & y, unsigned & m, unsigned & d) {
        z += 719468;
        const long long era = (z >= 0 ? z : z - 146096) / 146097;
        const auto doe = static_cast<unsigned long long>(z - era * 146097);
        const unsigned long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const long long yr = static_cast<long long>(yoe) + era * 400;
        const unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned long long mp = (5 * doy + 2) / 153;
        d = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);
        m = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9);
        y = static_cast<int>(yr + (m <= 2 ? 1 : 0));
    };
    const auto days_from_civil = [](int y, unsigned m, unsigned d) -> long long {
        y -= m <= 2 ? 1 : 0;
        const long long era = (y >= 0 ? y : y - 399) / 400;
        const auto yoe = static_cast<unsigned long long>(y - era * 400);
        const unsigned long long doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
        const unsigned long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<long long>(doe) - 719468;
    };

    // The instant this Date holds, in milliseconds. Kept as an ordinary
    // property so the object is inspectable and the collector needs to know
    // nothing new about it.
    const auto epoch_ms = [](context & c) {
        const value self = c.current_this();
        if (!self.is_object()) { return 0.0; }
        const value * held = static_cast<object_object *>(self.as_heap())->find("__ms");
        return held == nullptr ? 0.0 : context::to_number(*held);
    };
    // The civil fields of that instant.
    struct fields {
        int year;
        unsigned month; // 1-12
        unsigned day;
        int hour;
        int minute;
        int second;
        int weekday; // 0 = Sunday
    };
    const auto split = [civil_from_days](double ms) {
        fields out{};
        const auto total = static_cast<long long>(std::floor(ms));
        long long days = total / 86400000;
        long long rest = total % 86400000;
        if (rest < 0) {
            rest += 86400000;
            --days;
        }
        civil_from_days(days, out.year, out.month, out.day);
        out.hour = static_cast<int>(rest / 3600000);
        out.minute = static_cast<int>(rest / 60000 % 60);
        out.second = static_cast<int>(rest / 1000 % 60);
        // 1970-01-01 was a Thursday, which is what anchors the cycle.
        out.weekday = static_cast<int>(((days % 7) + 11) % 7);
        return out;
    };
    const auto field_method = [&](const char * name, int fields::* which) {
        method(cx, date_proto, name, [epoch_ms, split, which](context & c, std::span<value>) {
            return value::number(split(epoch_ms(c)).*which);
        });
    };
    field_method("getHours", &fields::hour);
    field_method("getMinutes", &fields::minute);
    field_method("getSeconds", &fields::second);
    field_method("getDay", &fields::weekday);
    field_method("getFullYear", &fields::year);
    method(cx, date_proto, "getMonth", [epoch_ms, split](context & c, std::span<value>) {
        // ZERO-BASED, which is the wart every calendar bug starts with and
        // which a page's arithmetic is written against.
        return value::number(static_cast<double>(split(epoch_ms(c)).month) - 1);
    });
    method(cx, date_proto, "getDate", [epoch_ms, split](context & c, std::span<value>) {
        return value::number(static_cast<double>(split(epoch_ms(c)).day));
    });
    method(cx, date_proto, "getMilliseconds", [epoch_ms](context & c, std::span<value>) {
        const double ms = epoch_ms(c);
        return value::number(std::fmod(std::fmod(ms, 1000.0) + 1000.0, 1000.0));
    });
    method(cx, date_proto, "getTime",
           [epoch_ms](context & c, std::span<value>) { return value::number(epoch_ms(c)); });
    method(cx, date_proto, "valueOf",
           [epoch_ms](context & c, std::span<value>) { return value::number(epoch_ms(c)); });
    // No timezone here, so the local getters ARE the UTC ones and say so rather
    // than pretending to a zone this engine does not have.
    method(cx, date_proto, "getTimezoneOffset",
           [](context &, std::span<value>) { return value::number(0); });
    method(cx, date_proto, "toISOString", [epoch_ms, split](context & c, std::span<value>) {
        const fields f = split(epoch_ms(c));
        std::array<char, 40> out{};
        const int written = std::snprintf(
            out.data(), out.size(), "%04d-%02u-%02uT%02d:%02d:%02d.%03dZ", f.year, f.month, f.day,
            f.hour, f.minute, f.second,
            static_cast<int>(std::fmod(std::fmod(epoch_ms(c), 1000.0) + 1000.0, 1000.0)));
        return c.string(std::string{out.data(), static_cast<std::size_t>(std::max(0, written))});
    });
    method(cx, date_proto, "toString", [epoch_ms, split](context & c, std::span<value>) {
        const fields f = split(epoch_ms(c));
        std::array<char, 48> out{};
        const int written = std::snprintf(out.data(), out.size(), "%04d-%02u-%02u %02d:%02d:%02d",
                                          f.year, f.month, f.day, f.hour, f.minute, f.second);
        return c.string(std::string{out.data(), static_cast<std::size_t>(std::max(0, written))});
    });

    auto * ctor = cx.allocate<native_object>(
        "Date", [date_proto, days_from_civil](context & c, std::span<value> a) {
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<object_object *>(self.as_heap());
            made->prototype = value::object(date_proto);
            double ms = 0;
            if (a.size() == 1 && a[0].is_number()) {
                ms = a[0].as_number();
            } else if (a.size() >= 2) {
                // (year, monthIndex, day, hours, minutes, seconds, ms)
                const auto part = [&](std::size_t i, double fallback) {
                    return i < a.size() ? context::to_number(a[i]) : fallback;
                };
                const long long days = days_from_civil(static_cast<int>(part(0, 1970)),
                                                       static_cast<unsigned>(part(1, 0)) + 1,
                                                       static_cast<unsigned>(part(2, 1)));
                ms = static_cast<double>(days) * 86400000.0 + part(3, 0) * 3600000.0 +
                     part(4, 0) * 60000.0 + part(5, 0) * 1000.0 + part(6, 0);
            }
            // `new Date()` with no argument is NOW, and now comes from the
            // context's clock - see context::set_clock. It used to be the literal
            // epoch, so every page here believed it was 1970.
            if (a.empty()) { ms = c.clock_ms(); }
            // NON-ENUMERABLE, like every other internal slot this engine
            // spells as a property: a Date has no own enumerable property in
            // the specification, and `Object.defineProperties(obj, new Date)`
            // handed this Number over as a descriptor and threw.
            made->define("__ms", value::number(ms), attr_builtin);
            return self;
        });
    detail::constant(ctor, "prototype", value::object(date_proto));
    link_constructor(cx, date_proto, "Date", value::object(ctor));
    method(cx, ctor, "now",
           [](context & c, std::span<value>) { return value::number(c.clock_ms()); });
    method(cx, ctor, "UTC", [days_from_civil](context & c, std::span<value> a) {
        const auto part = [&](std::size_t i, double fallback) {
            return i < a.size() ? context::to_number(a[i]) : fallback;
        };
        const long long days =
            days_from_civil(static_cast<int>(part(0, 1970)), static_cast<unsigned>(part(1, 0)) + 1,
                            static_cast<unsigned>(part(2, 1)));
        (void)c;
        return value::number(static_cast<double>(days) * 86400000.0 + part(3, 0) * 3600000.0 +
                             part(4, 0) * 60000.0 + part(5, 0) * 1000.0 + part(6, 0));
    });
    cx.define_global("Date", value::object(ctor));
}

// global functions
void install_globals(context & cx) {
    using detail::method;
    using detail::new_table;
    cx.define_native("parseInt", [](context & c, std::span<value> a) {
        const std::string s = str_at(c, a, 0);
        const int given = a.size() > 1 ? static_cast<int>(num_at(a, 1)) : 0;
        int base = given == 0 ? 10 : given;
        // A LEADING 0x IS HEXADECIMAL when no radix was demanded - 19.2.5 step
        // 8. Defaulting to 10 made `parseInt("0xFF")` stop at the `x` and
        // answer 0, which is how a colour parser reads black without erroring.
        const std::string_view body = trim(s, js_whitespace);
        const std::string_view digits =
            !body.empty() && (body.front() == '+' || body.front() == '-') ? body.substr(1) : body;
        if ((given == 0 || given == 16) && digits.size() > 1 && digits[0] == '0' &&
            (digits[1] == 'x' || digits[1] == 'X')) {
            base = 16;
        }
        try {
            std::size_t used = 0;
            const long long out = std::stoll(s, &used, base == 0 ? 10 : base);
            return used == 0 ? value::number(std::nan(""))
                             : value::number(static_cast<double>(out));
        } catch (...) {
            // parseInt("abc") is NaN, not an error - a page must not blow up on
            // a malformed number it is about to check with isNaN.
            return value::number(std::nan(""));
        }
    });
    cx.define_native("parseFloat", [](context & c, std::span<value> a) {
        // string_to_number_prefix, not std::stod: stod reads LC_NUMERIC for the
        // decimal separator, so `parseFloat("1.5")` would answer 1 on a host
        // whose locale writes a comma - and this repository byte-compares
        // rendered output across two platforms.
        return value::number(string_to_number_prefix(str_at(c, a, 0)));
    });

    // The value globals. Missing entirely before, so `NaN` was an undefined
    // global that read as `undefined` - and `NaN === NaN` was therefore TRUE,
    // because two undefineds are equal.
    cx.define_global("NaN", value::number(std::nan("")));
    cx.define_global("Infinity", value::number(std::numeric_limits<double>::infinity()));
    cx.define_global("undefined", value::undefined());
}

} // namespace ctbrowser::script::builtins_detail
