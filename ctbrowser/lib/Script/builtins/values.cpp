// ctbrowser.script builtins - Math, Boolean, Number, Date, and the bare globals.
//
// One of five files carved out of a 4,118-line builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in internal.hpp.

#include "internal.hpp"

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
    // ToUint32 (7.1.6) OVER AN ALREADY-COERCED NUMBER. `context::to_uint32` is
    // the STATIC conversion and answers 0 for every object, so `Math.imul({
    // valueOf: () => 3}, 2)` was 0 and `Math.clz32("1")` was 32 - both because
    // the receiver's own coercion never ran. Step 1 of each is `? ToUint32(x)`,
    // which is ToNumber and then this.
    const auto to_uint32_of = [](double n) -> std::uint32_t {
        if (!std::isfinite(n)) { return 0; }
        return static_cast<std::uint32_t>(
            static_cast<std::int64_t>(std::fmod(std::trunc(n), 4294967296.0)));
    };
    method(cx, math, "clz32", 1, [math_arg, to_uint32_of](context & c, std::span<value> a) {
        const std::uint32_t x = to_uint32_of(math_arg(c, a, 0));
        int n = 0;
        for (std::uint32_t bit = 0x80000000u; bit != 0 && (x & bit) == 0; bit >>= 1) { ++n; }
        return value::number(x == 0 ? 32 : n);
    });
    method(cx, math, "imul", 2, [math_arg, to_uint32_of](context & c, std::span<value> a) {
        // NAMED LOCALS, so the two coercions happen LEFT TO RIGHT. As arguments
        // to one expression their order is unspecified in C++ and observable in
        // JavaScript: `Math.imul({valueOf: f}, {valueOf: g})` must call f then g.
        const std::uint32_t x = to_uint32_of(math_arg(c, a, 0));
        const std::uint32_t y = to_uint32_of(math_arg(c, a, 1));
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
    auto state = std::make_shared<std::uint64_t>(seed == 0 ? 1 : seed);
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
// --- Date, 21.4 -------------------------------------------------------------
//
// NO TIME ZONE: this engine has none, so local time IS UTC and every local
// method answers what its UTC twin does; getTimezoneOffset says 0 and the
// string forms say "GMT+0000 (Coordinated Universal Time)". The time value is
// the `__ms` slot, a non-enumerable own property, NaN for an invalid date.

namespace {

constexpr double ms_per_day = 86400000.0;
[[nodiscard]] constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}
[[nodiscard]] constexpr bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
constexpr std::string_view invalid_date_slot = "__ms";

// 21.4.1.31 TimeClip.
[[nodiscard]] double time_clip(double t) {
    if (!std::isfinite(t) || std::fabs(t) > 8.64e15) { return std::nan(""); }
    return std::trunc(t) + 0.0; // -0 becomes +0
}

// Days since the epoch <-> y/m/d, proleptic Gregorian, on 64-bit integers
// (Hinnant's algorithms). NOT <chrono>: std::chrono::year stops at +-32767,
// and a time value reaches year +-273790.
[[nodiscard]] long long days_from_civil(long long y, int m, int d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void civil_from_days(long long z, long long & y, int & m, int & d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y += m <= 2;
}

// 21.4.1.28 MakeDay / 21.4.1.29 MakeTime / 21.4.1.30 MakeDate, on doubles: an
// out-of-range month or day normalises (`new Date(2024, 12, 1)` is 1 January
// 2025), a non-finite part is NaN.
[[nodiscard]] double make_date(double year, double month, double day, double h, double m, double s,
                               double ms) {
    for (const double v : {year, month, day, h, m, s, ms}) {
        if (!std::isfinite(v)) { return std::nan(""); }
    }
    const double y = std::trunc(year), mo = std::trunc(month);
    const double ym = y + std::floor(mo / 12.0);
    const int mn = static_cast<int>(mo - std::floor(mo / 12.0) * 12.0);
    if (std::fabs(ym) > 400000.0) { return std::nan(""); }
    const double days =
        static_cast<double>(days_from_civil(static_cast<long long>(ym), mn + 1, 1)) +
        std::trunc(day) - 1.0;
    return days * ms_per_day + std::trunc(h) * 3600000.0 + std::trunc(m) * 60000.0 +
           std::trunc(s) * 1000.0 + std::trunc(ms);
}

struct fields {
    double year, month /* 0-11 */, day, hour, minute, second, ms, weekday /* 0 = Sunday */;
};
[[nodiscard]] fields split(double t) {
    fields out{};
    const double days = std::floor(t / ms_per_day);
    const double rest = t - days * ms_per_day;
    long long y = 0;
    int m = 0, d = 0;
    civil_from_days(static_cast<long long>(days), y, m, d);
    out.year = static_cast<double>(y);
    out.month = m - 1;
    out.day = d;
    out.hour = std::floor(rest / 3600000.0);
    out.minute = std::fmod(std::floor(rest / 60000.0), 60.0);
    out.second = std::fmod(std::floor(rest / 1000.0), 60.0);
    out.ms = std::fmod(rest, 1000.0);
    out.weekday = std::fmod(std::fmod(days + 4.0, 7.0) + 7.0, 7.0); // 1970-01-01 was a Thursday
    return out;
}

constexpr const char * day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char * month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

[[nodiscard]] std::string year_text(double year) {
    return year < 0 ? std::format("-{:06}", -static_cast<int>(year))
                    : std::format("{:04}", static_cast<int>(year));
}
// 21.4.4.41.2 DateString: "Fri Feb 13 2009".
[[nodiscard]] std::string date_string(const fields & f) {
    return std::format("{} {} {:02} {}", day_names[static_cast<int>(f.weekday)],
                       month_names[static_cast<int>(f.month)], static_cast<int>(f.day),
                       year_text(f.year));
}
// 21.4.4.41.1 TimeString + 21.4.4.41.3 TimeZoneString.
[[nodiscard]] std::string time_string(const fields & f) {
    return std::format("{:02}:{:02}:{:02} GMT+0000 (Coordinated Universal Time)",
                       static_cast<int>(f.hour), static_cast<int>(f.minute),
                       static_cast<int>(f.second));
}
// 21.4.4.43 toUTCString: "Fri, 13 Feb 2009 23:31:30 GMT".
[[nodiscard]] std::string utc_string(const fields & f) {
    return std::format(
        "{}, {:02} {} {} {:02}:{:02}:{:02} GMT", day_names[static_cast<int>(f.weekday)],
        static_cast<int>(f.day), month_names[static_cast<int>(f.month)], year_text(f.year),
        static_cast<int>(f.hour), static_cast<int>(f.minute), static_cast<int>(f.second));
}
[[nodiscard]] std::string iso_string(const fields & f) {
    const int y = static_cast<int>(f.year);
    const std::string year = y >= 0 && y <= 9999 ? std::format("{:04}", y)
                             : y < 0             ? std::format("-{:06}", -y)
                                                 : std::format("+{:06}", y);
    return std::format("{}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", year,
                       static_cast<int>(f.month) + 1, static_cast<int>(f.day),
                       static_cast<int>(f.hour), static_cast<int>(f.minute),
                       static_cast<int>(f.second), static_cast<int>(f.ms));
}

// 21.4.3.2 Date.parse: the Date Time String Format (21.4.1.32) - `YYYY`,
// `YYYY-MM`, `YYYY-MM-DD`, each with an optional `THH:mm[:ss[.sss]]` and `Z`
// or `+HH:mm` - and the two forms toString and toUTCString produce. Anything
// else is NaN; a browser's tolerant fallbacks are not specified.
[[nodiscard]] double parse_date(std::string_view s) {
    std::size_t i = 0;
    const auto digits = [&](std::size_t n, double & out) {
        if (i + n > s.size()) { return false; }
        double v = 0;
        for (std::size_t k = 0; k < n; ++k) {
            if (!is_digit(s[i + k])) { return false; }
            v = v * 10 + (s[i + k] - '0');
        }
        i += n;
        out = v;
        return true;
    };
    const auto eat = [&](char ch) {
        if (i < s.size() && s[i] == ch) {
            ++i;
            return true;
        }
        return false;
    };
    // The ISO form.
    double year = 0, month = 1, day = 1, hour = 0, minute = 0, second = 0, ms = 0, offset = 0;
    bool iso = false;
    if (eat('+') || eat('-')) {
        const bool negative = s[i - 1] == '-';
        iso = digits(6, year);
        if (iso && negative) {
            if (year == 0) { return std::nan(""); }
            year = -year;
        }
    } else {
        iso = digits(4, year);
    }
    if (iso) {
        bool has_time = false;
        if (eat('-')) {
            if (!digits(2, month)) { return std::nan(""); }
            if (eat('-') && !digits(2, day)) { return std::nan(""); }
        }
        if (eat('T') || eat('t') || (i < s.size() && s[i] == ' ' && ++i)) {
            has_time = true;
            if (!digits(2, hour) || !eat(':') || !digits(2, minute)) { return std::nan(""); }
            if (eat(':')) {
                if (!digits(2, second)) { return std::nan(""); }
                if (eat('.')) {
                    std::size_t n = 0;
                    double frac = 0;
                    while (i < s.size() && is_digit(s[i])) {
                        if (n < 3) { frac = frac * 10 + (s[i] - '0'); }
                        ++n;
                        ++i;
                    }
                    if (n == 0) { return std::nan(""); }
                    for (; n < 3; ++n) { frac *= 10; }
                    ms = frac;
                }
            }
        }
        if (eat('Z') || eat('z')) {
            if (!has_time) { return std::nan(""); }
        } else if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            const double sign = s[i] == '-' ? -1 : 1;
            ++i;
            double oh = 0, om = 0;
            if (!digits(2, oh) || !eat(':') || !digits(2, om)) { return std::nan(""); }
            offset = sign * (oh * 60 + om) * 60000.0;
        }
        if (i != s.size() || month < 1 || month > 12 || day < 1 || day > 31 || hour > 24 ||
            minute > 59 || second > 59 || (hour == 24 && (minute != 0 || second != 0 || ms != 0))) {
            return std::nan("");
        }
        return time_clip(make_date(year, month - 1, day, hour, minute, second, ms) - offset);
    }
    // "Fri Feb 13 2009 23:31:30 GMT+0000 (...)" and "Fri, 13 Feb 2009 23:31:30 GMT".
    const auto word = [&] {
        std::string w;
        while (i < s.size() && is_alpha(s[i])) { w += s[i++]; }
        return w;
    };
    const auto number = [&](double & out) {
        double v = 0;
        std::size_t n = 0;
        while (i < s.size() && is_digit(s[i])) {
            v = v * 10 + (s[i++] - '0');
            ++n;
        }
        out = v;
        return n > 0;
    };
    const auto month_of = [&](const std::string & w) {
        for (int m = 0; m < 12; ++m) {
            if (w == month_names[m]) { return m; }
        }
        return -1;
    };
    const auto spaces = [&] {
        while (i < s.size() && s[i] == ' ') { ++i; }
    };
    std::string first = word();
    (void)eat(',');
    spaces();
    int mon = -1;
    if (i < s.size() && is_digit(s[i])) {
        // "13 Feb 2009"
        if (!number(day)) { return std::nan(""); }
        spaces();
        mon = month_of(word());
    } else {
        // "Feb 13 2009" - `first` was the weekday, or the month itself.
        mon = month_of(first);
        if (mon < 0) { mon = month_of(word()); }
        spaces();
        if (!number(day)) { return std::nan(""); }
    }
    spaces();
    if (mon < 0 || !number(year)) { return std::nan(""); }
    spaces();
    if (i < s.size() && is_digit(s[i])) {
        if (!number(hour) || !eat(':') || !number(minute)) { return std::nan(""); }
        if (eat(':') && !number(second)) { return std::nan(""); }
        spaces();
        if (s.substr(i, 3) == "GMT" || s.substr(i, 3) == "UTC") {
            i += 3;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
                const double sign = s[i] == '-' ? -1 : 1;
                ++i;
                double hhmm = 0;
                if (!number(hhmm)) { return std::nan(""); }
                offset = sign * (std::floor(hhmm / 100) * 60 + std::fmod(hhmm, 100)) * 60000.0;
            }
        }
    }
    return time_clip(make_date(year, mon, day, hour, minute, second, 0) - offset);
}

} // namespace

void install_date(context & cx) {
    using detail::method;
    using detail::new_table;

    object_object * date_proto = new_table(cx);

    // thisTimeValue (21.4.4): the slot, or a TypeError for anything that is
    // not a Date. NaN is a valid answer - an invalid date is still a Date.
    const auto this_time = [](context & c, const char * name, double & out) {
        const value self = c.current_this();
        if (self.is_object()) {
            if (const value * held =
                    static_cast<object_object *>(self.as_heap())->find(invalid_date_slot)) {
                out = held->as_number();
                return true;
            }
        }
        c.throw_error("TypeError", std::string{name} + " called on a non-Date");
        return false;
    };
    const auto set_time = [](context & c, double t) {
        static_cast<object_object *>(c.current_this().as_heap())
            ->set(invalid_date_slot, value::number(t));
        return value::number(t);
    };

    // The getters: a field of the split time, NaN for an invalid date. Local
    // and UTC are the same function body under two names.
    const auto getter = [&](std::string name, double fields::* which, double adjust) {
        method(cx, date_proto, name, 0,
               [this_time, which, adjust, name](context & c, std::span<value>) {
                   double t = 0;
                   if (!this_time(c, name.c_str(), t)) { return value::undefined(); }
                   if (std::isnan(t)) { return value::number(t); }
                   return value::number(split(t).*which + adjust);
               });
    };
    for (const char * prefix : {"get", "getUTC"}) {
        const std::string p = prefix;
        getter(p + "FullYear", &fields::year, 0);
        getter(p + "Month", &fields::month, 0);
        getter(p + "Date", &fields::day, 0);
        getter(p + "Day", &fields::weekday, 0);
        getter(p + "Hours", &fields::hour, 0);
        getter(p + "Minutes", &fields::minute, 0);
        getter(p + "Seconds", &fields::second, 0);
        getter(p + "Milliseconds", &fields::ms, 0);
    }
    getter("getYear", &fields::year, -1900); // B.2.3.1
    method(cx, date_proto, "getTime", 0, [this_time](context & c, std::span<value>) {
        double t = 0;
        return this_time(c, "Date.prototype.getTime", t) ? value::number(t) : value::undefined();
    });
    method(cx, date_proto, "valueOf", 0, [this_time](context & c, std::span<value>) {
        double t = 0;
        return this_time(c, "Date.prototype.valueOf", t) ? value::number(t) : value::undefined();
    });
    // No timezone here, so the local getters ARE the UTC ones and say so rather
    // than pretending to a zone this engine does not have.
    method(cx, date_proto, "getTimezoneOffset", 0, [this_time](context & c, std::span<value>) {
        double t = 0;
        if (!this_time(c, "Date.prototype.getTimezoneOffset", t)) { return value::undefined(); }
        return value::number(std::isnan(t) ? t : 0.0);
    });

    // The setters (21.4.4.20-21.4.4.34): the arguments replace `count` fields
    // from `first` on - only as many as were supplied - and the date is
    // rebuilt through MakeDate and TimeClip. Every argument is ToNumber'd
    // BEFORE the time is examined (a valueOf that changes the date runs first).
    const auto setter = [&](std::string name, int first, int count, double arity) {
        method(cx, date_proto, name, arity,
               [this_time, set_time, first, count, name](context & c, std::span<value> a) {
                   double t = 0;
                   if (!this_time(c, name.c_str(), t)) { return value::undefined(); }
                   double parts[7] = {};
                   const int supplied = static_cast<int>(
                       std::min<std::size_t>(a.size(), static_cast<std::size_t>(count)));
                   for (int k = 0; k < std::max(supplied, 1); ++k) {
                       if (!numeric_arg(c, arg_at(a, static_cast<std::size_t>(k)))) {
                           return value::undefined();
                       }
                       parts[k] = c.to_number_value(arg_at(a, static_cast<std::size_t>(k)));
                       if (c.throw_pending()) { return value::undefined(); }
                   }
                   // 21.4.4.21 step 4: setFullYear on an invalid date starts from +0;
                   // every other setter of an invalid date stays NaN.
                   if (std::isnan(t)) {
                       if (first != 0) { return value::number(t); }
                       t = 0;
                   }
                   const fields f = split(t);
                   double all[7] = {f.year, f.month, f.day, f.hour, f.minute, f.second, f.ms};
                   for (int k = 0; k < std::max(supplied, 1); ++k) { all[first + k] = parts[k]; }
                   return set_time(c, time_clip(make_date(all[0], all[1], all[2], all[3], all[4],
                                                          all[5], all[6])));
               });
    };
    for (const char * prefix : {"set", "setUTC"}) {
        const std::string p = prefix;
        setter(p + "FullYear", 0, 3, 3);
        setter(p + "Month", 1, 2, 2);
        setter(p + "Date", 2, 1, 1);
        setter(p + "Hours", 3, 4, 4);
        setter(p + "Minutes", 4, 3, 3);
        setter(p + "Seconds", 5, 2, 2);
        setter(p + "Milliseconds", 6, 1, 1);
    }
    method(cx, date_proto, "setTime", 1, [this_time, set_time](context & c, std::span<value> a) {
        double t = 0;
        if (!this_time(c, "Date.prototype.setTime", t)) { return value::undefined(); }
        if (!numeric_arg(c, arg_at(a, 0))) { return value::undefined(); }
        const double wanted = c.to_number_value(arg_at(a, 0));
        if (c.throw_pending()) { return value::undefined(); }
        return set_time(c, time_clip(wanted));
    });

    // The string forms. toISOString is the one that REFUSES an invalid date
    // (21.4.4.36 step 3); the rest say "Invalid Date".
    const auto stringer = [&](std::string name, std::string (*render)(const fields &)) {
        method(cx, date_proto, name, 0, [this_time, render, name](context & c, std::span<value>) {
            double t = 0;
            if (!this_time(c, name.c_str(), t)) { return value::undefined(); }
            if (std::isnan(t)) { return c.string("Invalid Date"); }
            return c.string(render(split(t)));
        });
    };
    stringer("toString", [](const fields & f) { return date_string(f) + " " + time_string(f); });
    stringer("toDateString", date_string);
    stringer("toTimeString", time_string);
    stringer("toUTCString", utc_string);
    stringer("toLocaleString",
             [](const fields & f) { return date_string(f) + " " + time_string(f); });
    stringer("toLocaleDateString", date_string);
    stringer("toLocaleTimeString", time_string);
    method(cx, date_proto, "toISOString", 0, [this_time](context & c, std::span<value>) {
        double t = 0;
        if (!this_time(c, "Date.prototype.toISOString", t)) { return value::undefined(); }
        if (std::isnan(t)) {
            c.throw_error("RangeError", "Invalid time value");
            return value::undefined();
        }
        return c.string(iso_string(split(t)));
    });
    // 21.4.4.37 toJSON: generic - ToPrimitive(this, number), null for a
    // non-finite one, else Invoke(O, "toISOString").
    method(cx, date_proto, "toJSON", 1, [](context & c, std::span<value>) {
        const value self = c.current_this();
        const value boxed = detail::box_primitive(c, self);
        if (boxed.is_nullish()) {
            c.throw_error("TypeError", "Date.prototype.toJSON called on null or undefined");
            return value::undefined();
        }
        value primitive = value::undefined();
        if (!c.to_primitive_hint(boxed, "number", primitive)) { return value::undefined(); }
        if (primitive.is_number() && !std::isfinite(primitive.as_number())) {
            return value::null();
        }
        const value fn = c.lookup_property(boxed, "toISOString");
        if (!fn.is_callable()) {
            c.throw_error("TypeError", "toISOString is not a function");
            return value::undefined();
        }
        return c.call(fn, std::span<const value>{}, boxed);
    });
    // 21.4.4.45 Date.prototype[@@toPrimitive]: "number" tries valueOf first,
    // "string" and "default" try toString first - which is why `date + ''`
    // is the date string and `date - 0` is the time value.
    {
        auto * exotic =
            detail::method_native(cx, "[Symbol.toPrimitive]", [](context & c, std::span<value> a) {
                const value self = c.current_this();
                if (!self.is_object_like()) {
                    c.throw_error("TypeError",
                                  "Date.prototype[Symbol.toPrimitive] called on non-object");
                    return value::undefined();
                }
                const std::string hint = arg_at(a, 0).is_string() ? c.to_string(a[0]) : "";
                if (hint != "number" && hint != "string" && hint != "default") {
                    c.throw_error("TypeError", "Invalid hint");
                    return value::undefined();
                }
                const bool number_first = hint == "number";
                for (const char * name : {number_first ? "valueOf" : "toString",
                                          number_first ? "toString" : "valueOf"}) {
                    const value fn = c.lookup_property(self, name);
                    if (c.throw_pending()) { return value::undefined(); }
                    if (!fn.is_callable()) { continue; }
                    const value out = c.call(fn, std::span<const value>{}, self);
                    if (c.throw_pending()) { return value::undefined(); }
                    if (!out.is_object_like()) { return out; }
                }
                c.throw_error("TypeError", "Cannot convert object to primitive value");
                return value::undefined();
            });
        detail::install_arity(cx, exotic, 1);
        date_proto->define("@@toPrimitive", value::object(exotic), attr_configurable);
    }

    auto * ctor = cx.allocate<native_object>("Date", [date_proto](context & c, std::span<value> a) {
        value self = c.current_this();
        // 21.4.2.1 step 1: `Date()` without `new` is the current time as a string.
        if (!detail::constructing_this(self)) {
            const fields f = split(c.clock_ms());
            return c.string(date_string(f) + " " + time_string(f));
        }
        auto * made = static_cast<object_object *>(self.as_heap());
        if (!made->prototype.is_object()) { made->prototype = value::object(date_proto); }
        double ms = 0;
        if (a.empty()) {
            // NOW, from the context's clock - see context::set_clock. It used
            // to be the literal epoch, so every page here believed it was 1970.
            ms = c.clock_ms();
        } else if (a.size() == 1) {
            // Step 4: another Date's time value; else ToPrimitive - a string
            // parses, anything else is a Number.
            value v = a[0];
            const value * held =
                v.is_object() ? static_cast<object_object *>(v.as_heap())->find(invalid_date_slot)
                              : nullptr;
            if (held != nullptr) {
                ms = held->as_number();
            } else {
                if (v.is_object_like() && !c.to_primitive_hint(v, "default", v)) {
                    return value::undefined();
                }
                if (v.is_string()) {
                    ms = parse_date(static_cast<string_object *>(v.as_heap())->text);
                } else {
                    if (!numeric_arg(c, v)) { return value::undefined(); }
                    ms = time_clip(c.to_number_value(v));
                }
            }
        } else {
            // (year, monthIndex[, day, hours, minutes, seconds, ms]), each
            // ToNumber'd in order; a year 0-99 is 1900-1999 (step 5.e).
            double parts[7] = {0, 0, 1, 0, 0, 0, 0};
            for (std::size_t i = 0; i < std::min<std::size_t>(a.size(), 7); ++i) {
                if (!numeric_arg(c, a[i])) { return value::undefined(); }
                parts[i] = c.to_number_value(a[i]);
                if (c.throw_pending()) { return value::undefined(); }
            }
            if (std::isfinite(parts[0]) && std::trunc(parts[0]) >= 0 &&
                std::trunc(parts[0]) <= 99) {
                parts[0] = 1900 + std::trunc(parts[0]);
            }
            ms = time_clip(
                make_date(parts[0], parts[1], parts[2], parts[3], parts[4], parts[5], parts[6]));
        }
        // NON-ENUMERABLE, like every other internal slot this engine spells as
        // a property: a Date has no own enumerable property in the
        // specification, and `Object.defineProperties(obj, new Date)` handed
        // this Number over as a descriptor and threw.
        made->define(invalid_date_slot, value::number(ms), attr_builtin);
        return self;
    });
    detail::constant(ctor, "prototype", value::object(date_proto));
    link_constructor(cx, date_proto, "Date", 7, value::object(ctor));
    method(cx, ctor, "now", 0,
           [](context & c, std::span<value>) { return value::number(c.clock_ms()); });
    method(cx, ctor, "parse", 1, [](context & c, std::span<value> a) {
        const std::string text = str_at(c, a, 0);
        if (c.throw_pending()) { return value::undefined(); }
        return value::number(parse_date(text));
    });
    method(cx, ctor, "UTC", 7, [](context & c, std::span<value> a) {
        double parts[7] = {std::nan(""), 0, 1, 0, 0, 0, 0};
        for (std::size_t i = 0; i < std::min<std::size_t>(a.size(), 7); ++i) {
            if (!numeric_arg(c, a[i])) { return value::undefined(); }
            parts[i] = c.to_number_value(a[i]);
            if (c.throw_pending()) { return value::undefined(); }
        }
        if (std::isfinite(parts[0]) && std::trunc(parts[0]) >= 0 && std::trunc(parts[0]) <= 99) {
            parts[0] = 1900 + std::trunc(parts[0]);
        }
        return value::number(time_clip(
            make_date(parts[0], parts[1], parts[2], parts[3], parts[4], parts[5], parts[6])));
    });
    cx.define_global("Date", value::object(ctor));
}

void install_globals(context & cx) {
    using detail::method;
    using detail::new_table;
    // 19.2.5 gives parseInt two parameters and 19.2.4 gives parseFloat one.
    detail::global_fn(cx, "parseInt", 2, [](context & c, std::span<value> a) {
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
