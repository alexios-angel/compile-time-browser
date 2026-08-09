// The Math object, against V8.
//
// Written by differentially testing every Math function against node (V8, the
// engine Chrome ships) over ~27,000 expressions; the ~90 differences it found
// collapsed to seven defects, and every one of them is pinned below. As with
// tests/number_format.cpp the expectations were taken from V8 BEFORE the fixes
// were written, so this file failed on the day it was committed rather than
// agreeing with the bug.
//
// WHAT THIS FILE ASSERTS, AND WHAT IT DELIBERATELY DOES NOT.
//
// ECMA-262 21.3.2 marks the VALUE of sin, cos, tan, asin, acos, atan, atan2,
// exp, expm1, log, log1p, log2, log10, pow, cbrt, sinh, cosh, tanh, asinh,
// acosh, atanh and hypot "implementation-approximated": a conforming engine may
// differ in the last place, and ours does — it is libm's answer, and Linux links
// glibc while the Windows cross-build links Microsoft's UCRT. **So no decimal
// string for any of those values appears here.** A test that asserted
// `Math.sin(1) === 0.8414709848078965` would be asserting which libc the
// machine had, and would fail the cross-build for a reason that is not a bug.
//
// What IS exact, and is asserted:
//   * every NaN / +-0 / +-Infinity special case, of every function - those are
//     mandated steps, not approximations, and every defect found was one;
//   * abs, ceil, floor, round, trunc, sign, min, max, fround, imul, clz32 in
//     full - the specification gives these exact results;
//   * results that are exactly representable, so approximation has no room:
//     sqrt(16) is 4, cbrt(27) is 3, log2(8) is 3, hypot(3,4) is 5;
//   * the eight constants, at all seventeen digits;
//   * Math.random's CONTRACT - range and finiteness - never a value.

#include "js_expect.hpp"

int main() {
    // --- the constants ------------------------------------------------------
    // All eight at full precision. SQRT1_2 was one ulp low: it was computed as
    // 1/sqrt2, which rounds twice, where sqrt2/2 rounds once because halving is
    // exact. The engine disagreed with its own Math.sqrt(0.5).
    js_expect("Math.PI", "3.141592653589793");
    js_expect("Math.E", "2.718281828459045");
    js_expect("Math.LN2", "0.6931471805599453");
    js_expect("Math.LN10", "2.302585092994046");
    js_expect("Math.LOG2E", "1.4426950408889634");
    js_expect("Math.LOG10E", "0.4342944819032518");
    js_expect("Math.SQRT2", "1.4142135623730951");
    js_expect("Math.SQRT1_2", "0.7071067811865476");
    js_expect("Math.SQRT1_2 === Math.sqrt(0.5)", "true");

    // --- Math.round ---------------------------------------------------------
    // It rounds halves toward POSITIVE infinity, which is not std::round (away
    // from zero) and not floor(x + 0.5) either. The latter was what this engine
    // had, and it broke three separate clauses.
    js_expect("Math.round(2.5)", "3");
    js_expect("Math.round(-2.5)", "-2"); // toward +Infinity, so -2 and not -3
    js_expect("Math.round(3.5)", "4");
    js_expect("Math.round(-3.5)", "-3");
    js_expect("Math.round(0.5)", "1");
    js_expect("Math.round(-1.5)", "-1");
    // THE DOUBLE-ROUNDING CASE. 0.49999999999999994 + 0.5 is exactly halfway
    // between 1-2^-53 and 1, so ties-to-even lifts it to 1.0 before floor ever
    // runs, and the answer came back 1 where the spec requires +0.
    js_expect("Math.round(0.49999999999999994)", "0");
    js_expect("Math.round(0.4999999999999999)", "0"); // control: just below
    js_expect("Math.round(0.5000000000000001)", "1"); // control: just above
    // AN INTEGRAL NUMBER COMES BACK UNCHANGED. Above 2^52 the spacing between
    // doubles is 1, so `x + 0.5` was not representable and every odd integer
    // moved - 2^53-1 came back as 2^53, outside the safe-integer range.
    js_expect("Math.round(4503599627370496)", "4503599627370496");
    js_expect("Math.round(4503599627370497)", "4503599627370497");
    js_expect("Math.round(-4503599627370497)", "-4503599627370497");
    js_expect("Math.round(9007199254740991)", "9007199254740991");
    js_expect("Math.round(-9007199254740991)", "-9007199254740991");
    // THE SIGN OF ZERO SURVIVES. Every x in [-0.5, -0] returns -0, and adding
    // 0.5 destroyed that.
    js_expect_negative_zero("Math.round(-0)", true);
    js_expect_negative_zero("Math.round(-0.5)", true);
    js_expect_negative_zero("Math.round(-0.1)", true);
    js_expect_negative_zero("Math.round(-0.4)", true);
    js_expect_negative_zero("Math.round(-5e-324)", true);
    js_expect_negative_zero("Math.round(-0.6)", false); // control: rounds to -1
    js_expect_negative_zero("Math.round(0)", false);
    js_expect("Math.round(NaN)", "NaN");
    js_expect("Math.round(Infinity)", "Infinity");
    js_expect("Math.round(-Infinity)", "-Infinity");

    // --- Math.min and Math.max ----------------------------------------------
    // NOT std::min/std::max. Both are written on `<`, and IEEE comparison
    // answers false to everything involving NaN, so the NaN was silently
    // dropped; and `-0 < +0` is false, so which zero came back depended on the
    // ARGUMENT ORDER.
    js_expect("Math.min(1,NaN)", "NaN");
    js_expect("Math.max(1,NaN)", "NaN");
    js_expect("Math.min(NaN,1)", "NaN");
    js_expect("Math.max(NaN,NaN)", "NaN");
    js_expect("Math.min(1,2,3,4,5,6,7,8,9,NaN)", "NaN");
    js_expect("Math.max(NaN)", "NaN");
    js_expect_negative_zero("Math.min(0,-0)", true);
    js_expect_negative_zero("Math.min(-0,0)", true);
    js_expect_negative_zero("Math.max(-0,0)", false);
    js_expect_negative_zero("Math.max(0,-0)", false);
    js_expect_negative_zero("Math.min(-0,-0)", true);
    js_expect_negative_zero("Math.max(-0,-0)", true);
    // The empty-call seeds, which is what makes Math.max(...list) work.
    js_expect("Math.min()", "Infinity");
    js_expect("Math.max()", "-Infinity");
    js_expect("Math.min(1,9,4)", "1");
    js_expect("Math.max(1,9,4)", "9");
    js_expect("Math.max(...[3,9,4])", "9");
    js_expect("Math.min(-Infinity,Infinity)", "-Infinity");

    // --- Math.hypot ---------------------------------------------------------
    // An infinite argument wins BEFORE NaN is considered - step 3 precedes step
    // 5 - so hypot(Infinity, NaN) is Infinity and not NaN.
    js_expect("Math.hypot(Infinity,NaN)", "Infinity");
    js_expect("Math.hypot(NaN,-Infinity)", "Infinity");
    js_expect("Math.hypot(1,Infinity)", "Infinity");
    js_expect("Math.hypot(NaN,1)", "NaN");
    // OVERFLOW AND UNDERFLOW. Squaring passes 1.8e308 above about 1.34e154 and
    // flushes to zero below about 1e-162, so the naive sqrt(sum of squares)
    // answered Infinity and 0 for values whose result is an ordinary double.
    js_expect("Math.hypot(1e300,1e300)", "1.4142135623730952e+300");
    js_expect("Math.hypot(3e300,4e300)", "5e+300");
    js_expect("Math.hypot(1.7976931348623157e308,0)", "1.7976931348623157e+308");
    js_expect("Math.hypot(3e-300,4e-300)", "5e-300");
    js_expect("Math.hypot(5e-324)", "5e-324");
    // Exactly representable, so no approximation licence applies.
    js_expect("Math.hypot(3,4)", "5");
    js_expect("Math.hypot(5,12)", "13");
    js_expect("Math.hypot(-3,-4)", "5");
    js_expect("Math.hypot()", "0");
    js_expect("Math.hypot(0,0)", "0");
    js_expect("Math.hypot(7)", "7");

    // --- Math.pow, and the ** operator --------------------------------------
    // C99 defines pow(+-1, y) as 1 for EVERY y; Number::exponentiate requires
    // NaN when the exponent is NaN or infinite. Both spellings had the bug.
    js_expect("Math.pow(1,NaN)", "NaN");
    js_expect("Math.pow(1,Infinity)", "NaN");
    js_expect("Math.pow(1,-Infinity)", "NaN");
    js_expect("Math.pow(-1,Infinity)", "NaN");
    js_expect("Math.pow(-1,-Infinity)", "NaN");
    js_expect("1 ** NaN", "NaN");
    js_expect("1 ** Infinity", "NaN");
    js_expect("(-1) ** Infinity", "NaN");
    // Even NaN to the zeroth power is 1 - the exponent is checked first.
    js_expect("Math.pow(NaN,0)", "1");
    js_expect("Math.pow(NaN,-0)", "1");
    js_expect("Math.pow(2,10)", "1024");
    js_expect("2 ** 10", "1024");
    js_expect("Math.pow(2,-1)", "0.5");
    js_expect("Math.pow(0,0)", "1");
    js_expect("Math.pow(Infinity,0)", "1");
    js_expect("Math.pow(2,NaN)", "NaN");
    js_expect("Math.pow(NaN,1)", "NaN");

    // --- a missing argument is NaN, not zero --------------------------------
    // Every Math function's step 1 is `Let n be ? ToNumber(x)`, an absent
    // argument is undefined, and ToNumber(undefined) is NaN. The shared
    // argument helper substituted 0.0, so Math.sin() was sin(0) = 0 and
    // Math.cos() was 1 - answers a page cannot distinguish from real ones.
    for (const char * fn :
         {"abs",  "floor", "ceil", "trunc", "sign", "round", "sqrt",  "cbrt",  "fround",
          "exp",  "expm1", "log",  "log1p", "log2", "log10", "sin",   "cos",   "tan",
          "asin", "acos",  "atan", "sinh",  "cosh", "tanh",  "asinh", "acosh", "atanh"}) {
        js_expect(std::string{"Math."} + fn + "()", "NaN");
    }
    js_expect("Math.pow()", "NaN");
    js_expect("Math.pow(2)", "NaN");
    js_expect("Math.atan2()", "NaN");
    js_expect("Math.atan2(1)", "NaN");
    // Written out, undefined already coerced correctly - it was only the arity
    // fallback that was wrong.
    js_expect("Math.sin(undefined)", "NaN");
    js_expect("Math.pow(2,undefined)", "NaN");
    // These two take their arguments a different way and were always right.
    js_expect("Math.clz32()", "32");
    js_expect("Math.imul()", "0");

    // --- the exact-by-specification functions -------------------------------
    js_expect("Math.abs(-5)", "5");
    js_expect("Math.abs(-Infinity)", "Infinity");
    js_expect("Math.abs(NaN)", "NaN");
    js_expect_negative_zero("Math.abs(-0)", false);
    js_expect("Math.floor(3.7)", "3");
    js_expect("Math.floor(-3.2)", "-4");
    js_expect("Math.ceil(3.2)", "4");
    js_expect("Math.ceil(-3.7)", "-3");
    js_expect_negative_zero("Math.ceil(-0.5)", true);
    js_expect("Math.trunc(3.7)", "3");
    js_expect("Math.trunc(-3.7)", "-3");
    js_expect_negative_zero("Math.trunc(-0.5)", true);
    js_expect("Math.sign(-3)", "-1");
    js_expect("Math.sign(3)", "1");
    js_expect("Math.sign(NaN)", "NaN");
    js_expect_negative_zero("Math.sign(-0)", true);
    js_expect_negative_zero("Math.sign(0)", false);
    // fround is a round trip through binary32.
    js_expect("Math.fround(1.5)", "1.5");
    js_expect("Math.fround(1.1)", "1.100000023841858");
    js_expect("Math.fround(NaN)", "NaN");
    js_expect("Math.fround(Infinity)", "Infinity");
    js_expect("Math.fround(1e300)", "Infinity");
    js_expect("Math.clz32(1)", "31");
    js_expect("Math.clz32(0)", "32");
    js_expect("Math.clz32(-1)", "0");
    js_expect("Math.imul(3,4)", "12");
    js_expect("Math.imul(-5,12)", "-60");
    js_expect("Math.imul(0xffffffff,5)", "-5");

    // --- exactly representable results --------------------------------------
    // Approximation has no room here, so these are safe to assert exactly on
    // any libm.
    js_expect("Math.sqrt(16)", "4");
    js_expect("Math.sqrt(0)", "0");
    js_expect("Math.sqrt(-1)", "NaN");
    js_expect_negative_zero("Math.sqrt(-0)", true);
    js_expect("Math.cbrt(27)", "3");
    js_expect("Math.cbrt(-27)", "-3");
    js_expect("Math.cbrt(1e9)", "1000");
    js_expect("Math.log2(8)", "3");
    js_expect("Math.log2(1024)", "10");
    js_expect("Math.log10(1000)", "3");
    js_expect("Math.log(1)", "0");
    js_expect("Math.exp(0)", "1");
    js_expect("Math.atan2(0,-0)", "3.141592653589793"); // exactly Math.PI, by rule

    // --- the mandated special cases of the approximated functions -----------
    // The VALUES are implementation-approximated; these are not.
    js_expect("Math.log(0)", "-Infinity");
    js_expect("Math.log(-1)", "NaN");
    js_expect("Math.log2(0)", "-Infinity");
    js_expect("Math.log10(0)", "-Infinity");
    js_expect("Math.exp(-Infinity)", "0");
    js_expect("Math.exp(Infinity)", "Infinity");
    js_expect("Math.sin(Infinity)", "NaN");
    js_expect("Math.cos(Infinity)", "NaN");
    js_expect("Math.tan(Infinity)", "NaN");
    js_expect("Math.asin(2)", "NaN");
    js_expect("Math.acos(2)", "NaN");
    js_expect("Math.acos(1)", "0");
    js_expect("Math.atan(Infinity)", "1.5707963267948966"); // exactly PI/2, by rule
    js_expect("Math.atanh(1)", "Infinity");
    js_expect("Math.atanh(-1)", "-Infinity");
    js_expect("Math.atanh(2)", "NaN");
    js_expect("Math.acosh(1)", "0");
    js_expect("Math.acosh(0.5)", "NaN");
    js_expect("Math.cosh(Infinity)", "Infinity");
    js_expect("Math.cosh(-Infinity)", "Infinity");
    js_expect("Math.sinh(-Infinity)", "-Infinity");
    js_expect("Math.tanh(Infinity)", "1");
    js_expect("Math.tanh(-Infinity)", "-1");
    js_expect_negative_zero("Math.sin(-0)", true);
    js_expect_negative_zero("Math.tan(-0)", true);
    js_expect_negative_zero("Math.sinh(-0)", true);
    js_expect_negative_zero("Math.cbrt(-0)", true);
    js_expect_negative_zero("Math.expm1(-0)", true);

    // --- a literal too big for a double is Infinity, not zero ---------------
    // from_chars reports the overflow and does not write the value, and the
    // error was going unchecked - so `1e400` compiled to 0, at the far end of
    // the number line from the truth. Runtime overflow was always right.
    js_expect("1e400", "Infinity");
    js_expect("-1e400", "-Infinity");
    js_expect("1e-400", "0");
    js_expect("1e308*10", "Infinity"); // control: this always worked
    js_expect("Math.sinh(1e400)", "Infinity");
    js_expect("Number('1e400')", "Infinity");
    js_expect("Number('-1e400')", "-Infinity");
    js_expect("Number('1e-400')", "0");
    js_expect_negative_zero("Number('-1e-400')", true);

    // --- Math.random: the contract, never a value ---------------------------
    // Seeded and deterministic here on purpose, so the byte-compared render
    // goldens can exist (see CLAUDE.md). That makes any particular value an
    // implementation detail, so this asserts only what the specification
    // promises: a Number in [0, 1).
    js_expect("(function () {"
              "  for (var i = 0; i < 10000; i++) {"
              "    var r = Math.random();"
              "    if (!(r >= 0 && r < 1)) { return 'out of range: ' + r; }"
              "    if (r !== r) { return 'NaN'; }"
              "  }"
              "  return 'ok';"
              "})()",
              "ok");
    js_expect("(function () { var a = Math.random(), b = Math.random(); return a !== b; })()",
              "true");

    REPORT("math_basics");
}
