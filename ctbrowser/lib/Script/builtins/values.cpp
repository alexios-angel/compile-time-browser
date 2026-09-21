// ctbrowser.script builtins - Math, Boolean, Number, Date, and the bare globals.
//
// One of five files carved out of a 4,118-line builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in internal.hpp.

#include "ctbrowser/core/number.hpp"
#include "internal.hpp"
#include "text/internal.hpp"

#include <chrono>
#include <format>
#include <numbers>

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
        method(cx, math, name, 1, [fn, math_arg](context & c, std::span<value> a) {
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
    // this engine printed numbers to six decimals, and `unittests/js/vm_stdlib` was
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
        (void)std::frexp(a, &e); // a = m * 2^e, m in [0.5, 1)
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
        // NAMED LOCALS, so the two coercions happen LEFT TO RIGHT. As arguments
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
    method(cx, math, "round", 1, [math_arg](context & c, std::span<value> a) {
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
    // NAMED LOCALS FOR THE TWO ARGUMENTS, in both of these and in `imul` below.
    // The order in which C++ evaluates the arguments of one call is
    // UNSPECIFIED, and clang evaluates them right to left - so
    // `Math.pow({valueOf: f}, {valueOf: g})` ran g before f, where the
    // specification's steps 1 and 2 fix the order and a test can see it.
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
    // EVERY ARGUMENT IS COERCED FIRST, ONCE, IN ORDER, and only then are any of
    // them looked at. Step 1 of hypot, min and max builds a List called
    // `coerced` before step 3 inspects it, and that ordering is OBSERVABLE
    // three ways:
    //
    //  * a `valueOf` that throws must abort at the FIRST such argument and
    //    leave the later ones uncalled. `Math.hypot(Infinity, {valueOf: throws},
    //    {valueOf: counts})` returned Infinity from the infinity without ever
    //    reaching either object.
    //  * a `valueOf` after a NaN must still be called. `Math.min(NaN, obj)`
    //    returned NaN from the first argument, so obj.valueOf never ran - which
    //    is exactly what Math.min_each-element-coerced.js counts.
    //  * an argument must be coerced EXACTLY once. hypot's two passes ran every
    //    `valueOf` twice, and a `valueOf` that returns a different number each
    //    time - a counter, which is how the suite detects this - made the scan
    //    and the sum disagree about what they were adding up.
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
    // 21.3.2.35 Math.sumPrecise: the EXACT sum of an iterable of Numbers,
    // rounded once. Every double is an integer multiple of 2^-1074, so the sum
    // is kept as a bigint in that unit - at most ~2100 bits - and rounded to
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
                const double mant = std::frexp(x, &exp); // x = mant * 2^exp, |mant| in [0.5, 1)
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
    // NOT std::min/std::max, which are the wrong function twice over. Both are
    // written in terms of `<`, and IEEE comparison says NO to everything
    // involving NaN - so `std::min(1.0, NaN)` is 1 and the NaN vanishes, where
    // 21.3.2.24 step 4.a returns NaN the moment it sees one. And `-0 < +0` is
    // false, so which zero came back depended on ARGUMENT ORDER: `Math.min(0,
    // -0)` gave +0 while `Math.min(-0, 0)` was accidentally right. Steps 4.b
    // make the zero ordering explicit, and so does this.
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
    // xorshift64*, held in the closure so each context has its own stream.
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
    math->define("@@toStringTag", cx.string("Math"), attr_configurable); // 21.3.1.9
    cx.define_global("Math", value::object(math));
}

void install_boolean(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * boolean_proto = new_table(cx);
    // thisBooleanValue, 20.3.3: a Boolean, a wrapper's [[BooleanData]], or
    // Boolean.prototype itself (whose slot is false) - anything else is a
    // TypeError rather than a truthiness test. `Boolean.prototype.toString
    // .call({})` answered "true".
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
    // 20.3.1.1: a call converts, `new` wraps - see detail::wrap_primitive.
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
        value made = value::number(0.0);
        if (!a.empty() && a[0].is_kind(heap_kind::bigint)) {
            made = value::number(
                bigint_to_double(static_cast<bigint_object *>(a[0].as_heap())->digits));
        } else if (!a.empty()) {
            // ToNumeric, 21.1.1.1 step 1: a Symbol refuses (7.1.4).
            if (!numeric_arg(c, a[0])) { return value::undefined(); }
            made = value::number(c.to_number_value(a[0]));
            if (c.throw_pending()) { return value::undefined(); }
        }
        // 21.1.1.1 step 3: a call converts, `new` wraps - see
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
    // { writable: true, enumerable: FALSE, configurable: true } with a `length`
    // of 1, and `set` gave them the default attributes - so `Object.keys` and a
    // for-in over `Number` walked them.
    const auto predicate = [&](const char * name, bool (*fn)(const value &)) {
        detail::method(cx, number_ctor, name, 1, [fn](context &, std::span<value> a) {
            const value v = arg_at(a, 0);
            return value::boolean(fn(v));
        });
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
    // A DIGIT COUNT OUT OF RANGE IS A RangeError, NOT A CLAMP - 21.1.3.3 step
    // 4, 21.1.3.2 step 5 and 21.1.3.5 step 5, one per method below.
    //
    // All three clamped instead, and a clamp is the wrong answer twice over.
    // `(1).toFixed(-1)` answered "1" where every engine throws, so a page
    // computing a digit count and getting it wrong was handed a plausible
    // string rather than told; and the ceiling was 20 where the specification's
    // is 100, so `(3).toFixed(50)` was silently 20 places. It also coerced with
    // the STATIC `num_at`, which answers NaN for an object - so
    // `(1).toFixed({valueOf: () => 2})` never ran the valueOf and clamped the
    // NaN to zero.
    //
    // Infinity is an out-of-range count and not a huge one: `integer_arg`
    // preserves it precisely so the range test can see it.
    //
    // The COERCION and the REFUSAL are separate because the specification puts
    // a step between them in two of the three methods: toExponential and
    // toPrecision answer for a non-finite receiver AFTER coercing the argument
    // and BEFORE checking its range, so `Infinity.toPrecision(1000)` is
    // "Infinity" and not a RangeError - while toFixed checks the range first
    // and `NaN.toFixed(101)` therefore throws.
    const auto refuse_digits = [](context & c, const char * method, double n, double low,
                                  double high) {
        if (n >= low && n <= high) { return false; }
        c.throw_error("RangeError", std::string{method} + "() argument must be between " +
                                        number_to_string(low) + " and " + number_to_string(high));
        return true;
    };
    method(cx, number_proto, "toFixed", 1, [refuse_digits](context & c, std::span<value> a) {
        const double self = detail::this_number_value(c, "Number.prototype.toFixed");
        const double asked = integer_arg(c, a, 0);
        if (refuse_digits(c, "toFixed", asked, 0, 100)) { return c.string(""); }
        const auto digits = static_cast<int>(asked);
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
    method(cx, number_proto, "toString", 1, [](context & c, std::span<value> a) {
        const double v = detail::this_number_value(c, "Number.prototype.toString");
        // AN OUT-OF-RANGE RADIX IS A RangeError (21.1.3.6 step 4), not a silent
        // fall back to 10 - and that fall back was the segfault: it reached
        // `c.to_string(c.current_this())`, which for an object receiver calls
        // this native again. Nothing below asks the CONTEXT to stringify the
        // receiver any more; it stringifies the NUMBER, which cannot re-enter.
        // ToIntegerOrInfinity, NOT a cast. `static_cast<int>` of a NaN or of an
        // infinity is undefined behaviour - on x86-64 it is `cvttsd2si`'s
        // indefinite value, 0x80000000 - so `(1).toString(NaN)` and
        // `(1).toString(Infinity)` were UB that happened to land outside the
        // range and throw. 7.1.5 makes NaN zero, and zero is out of range.
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
        const double v = detail::this_number_value(c, "Number.prototype.toExponential");
        // NO ARGUMENT IS NOT SIX. The specification asks for as many digits as
        // uniquely specify the value, so `(5).toExponential()` is "5e+0" and
        // not "5.000000e+0"; -1 is how number_to_exponential is told that.
        if (a.empty() || a[0].is_undefined()) { return c.string(number_to_exponential(v, -1)); }
        // The argument is coerced (step 2) even when the receiver is not finite
        // and the answer cannot depend on it, so a `valueOf` on it still runs -
        // which is exactly what toExponential/nan.js counts.
        const double asked = integer_arg(c, a, 0);
        if (!std::isfinite(v)) { return c.string(number_to_string(v)); }
        if (refuse_digits(c, "toExponential", asked, 0, 100)) { return c.string(""); }
        return c.string(number_to_exponential(v, static_cast<int>(asked)));
    });
    method(cx, number_proto, "toPrecision", 1, [refuse_digits](context & c, std::span<value> a) {
        const double v = detail::this_number_value(c, "Number.prototype.toPrecision");
        // No argument at all is toString, not zero significant digits - of the
        // NUMBER, not of the receiver. `c.to_string(c.current_this())` here was
        // the second half of the toString cycle: see this_number_value.
        if (a.empty() || a[0].is_undefined()) { return c.string(number_to_string(v)); }
        // The same step ordering as toExponential above, and 21.1.3.5's lower
        // bound is ONE rather than zero: there is no such thing as a number
        // written to no significant digits.
        const double asked = integer_arg(c, a, 0);
        if (!std::isfinite(v)) { return c.string(number_to_string(v)); }
        if (refuse_digits(c, "toPrecision", asked, 1, 100)) { return c.string(""); }
        // NOT "%.*g", which drops trailing zeros where toPrecision keeps them -
        // `(1.5).toPrecision(3)` is "1.50" - and writes two exponent digits.
        return c.string(number_to_precision(v, static_cast<int>(asked)));
    });
    // 21.1.3.4. No ECMA-402 here, so it is ToString of the number - which is
    // what the specification itself allows an implementation without an Intl
    // library to do, and it is the difference between a page's
    // `n.toLocaleString()` printing a number and throwing "not a function".
    method(cx, number_proto, "toLocaleString", 0, [](context & c, std::span<value>) {
        return c.string(
            number_to_string(detail::this_number_value(c, "Number.prototype.toLocaleString")));
    });
    detail::constant(number_ctor, "prototype", value::object(number_proto));
    link_constructor(cx, number_proto, "Number", 1, value::object(number_ctor));
    cx.set_prototype(context::proto_kind::number, number_proto);
}

void install_globals(context & cx) {
    using detail::method;
    using detail::new_table;
    // 19.2.5 gives parseInt two parameters and 19.2.4 gives parseFloat one.
    detail::global_fn(cx, "parseInt", 2, [](context & c, std::span<value> a) {
        // 19.2.5, step by step. ? ToString(string), then StrWhiteSpaceChar
        // trimmed from the front (the Unicode spaces too - TrimString's set,
        // not the ASCII one), the sign, ? ToInt32(radix) with 0 meaning 10
        // and a 0x prefix meaning 16 unless a radix was demanded, [2, 36]
        // or NaN, the longest digit prefix or NaN, and the value - through a
        // double, which rounds past 2^53 as the note allows.
        const std::string s = str_at(c, a, 0);
        if (c.throw_pending()) { return value::undefined(); }
        std::size_t from = 0;
        std::size_t to = 0;
        trim_bounds(s, true, false, from, to);
        std::string_view rest = std::string_view{s}.substr(from);
        double sign = 1;
        if (!rest.empty() && (rest.front() == '+' || rest.front() == '-')) {
            if (rest.front() == '-') { sign = -1; }
            rest.remove_prefix(1);
        }
        const value radix_arg = arg_at(a, 1);
        if (!numeric_arg(c, radix_arg)) { return value::undefined(); }
        const double radix_number = c.to_number_value(radix_arg);
        if (c.throw_pending()) { return value::undefined(); }
        int radix = static_cast<int>(context::to_int32(value::number(radix_number)));
        bool strip_prefix = true;
        if (radix != 0) {
            if (radix < 2 || radix > 36) { return value::number(std::nan("")); }
            if (radix != 16) { strip_prefix = false; }
        } else {
            radix = 10;
        }
        if (strip_prefix && rest.size() >= 2 && rest[0] == '0' &&
            (rest[1] == 'x' || rest[1] == 'X')) {
            rest.remove_prefix(2);
            radix = 16;
        }
        const auto digit = [](char ch) {
            if (ch >= '0' && ch <= '9') { return ch - '0'; }
            if (ch >= 'a' && ch <= 'z') { return ch - 'a' + 10; }
            if (ch >= 'A' && ch <= 'Z') { return ch - 'A' + 10; }
            return 36;
        };
        std::size_t used = 0;
        while (used < rest.size() && digit(rest[used]) < radix) { ++used; }
        if (used == 0) { return value::number(std::nan("")); }
        // Base 10 goes through the exact decimal-to-double conversion, as the
        // note asks; the other radices accumulate.
        if (radix == 10) {
            return value::number(sign * string_to_number_prefix(rest.substr(0, used)));
        }
        double out = 0;
        for (std::size_t i = 0; i < used; ++i) { out = out * radix + digit(rest[i]); }
        return value::number(sign * out);
    });
    detail::global_fn(cx, "parseFloat", 1, [](context & c, std::span<value> a) {
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

    // `Number.parseFloat` and `Number.parseInt` are THE SAME FUNCTION OBJECTS
    // as the globals - 21.1.2.12 and 21.1.2.13 say "the same function object",
    // and `Number.parseFloat === parseFloat` is what test262 asserts. So this
    // is an alias, not a second copy.
    //
    // Installed HERE rather than in install_number because that runs first (see
    // install_builtins), when neither global exists yet. Four `css/css-values`
    // files in the WPT sweep fail on `Number.parseFloat` alone.
    if (const value number = cx.global("Number"); number.is_kind(heap_kind::native)) {
        for (const char * name : {"parseInt", "parseFloat"}) {
            static_cast<native_object *>(number.as_heap())
                ->define(name, cx.global(name), attr_builtin);
        }
    }
}

} // namespace ctbrowser::script::builtins_detail
