// `bigint`, against V8.
//
// THIS FILE USED TO RECORD AN ABSENCE. Every line asserted that BigInt did not
// exist, with V8's answer in the comment, and it said outright that the day the
// type was implemented it should fail everywhere at once and be rewritten as a
// conformance suite. That is what happened - 17 of its assertions failed
// together - and this is the rewrite.
//
// Differentially tested against node (V8) over 61 expressions covering
// arithmetic, comparison, conversion and every error the specification names;
// no differences.
//
// THE POINT OF THE TYPE is exactness past 2^53, where a double starts rounding
// - an id, a nanosecond timestamp, a 64-bit hash. So the assertions that matter
// most are not the arithmetic, which is boring and correct, but the REFUSALS:
// a BigInt will not silently become a Number, because doing so would lose
// exactly the thing it was reached for.
//
// Represented by `boost::multiprecision::cpp_int` in `bigint_object`. Boost was
// turned down for `Math` earlier in this repository and is right here, which is
// not a contradiction: the objections there were speed against hardware and
// disagreement with V8's fdlibm, and arbitrary precision has no hardware
// alternative and no rounding to disagree about.

#include "js_expect.hpp"

int main() {
    // --- literals, in every radix --------------------------------------------
    js_expect("1n", "1");
    js_expect("0n", "0");
    js_expect("typeof 1n", "bigint");
    js_expect("0xFFn", "255");
    js_expect("0b101n", "5");
    js_expect("0o17n", "15");
    js_expect("1_000n", "1000");
    // THE WHOLE REASON THE TYPE EXISTS: this integer is not representable as a
    // double, and the Number beside it rounds to something else.
    js_expect("9007199254740993n", "9007199254740993");
    js_expect("9007199254740993", "9007199254740992"); // the Number, rounded
    js_expect("9007199254740993n + 1n", "9007199254740994");

    // --- arithmetic ------------------------------------------------------------
    js_expect("1n + 2n", "3");
    js_expect("10n - 3n", "7");
    js_expect("6n * 7n", "42");
    js_expect("7n / 2n", "3"); // TRUNCATES toward zero - there are no fractions
    js_expect("-7n / 2n", "-3");
    js_expect("7n % 3n", "1");
    js_expect("-7n % 3n", "-1"); // the sign follows the dividend, as for Number
    js_expect("2n ** 10n", "1024");
    js_expect("2n ** 64n", "18446744073709551616"); // past what a double can hold
    js_expect("2n ** 100n", "1267650600228229401496703205376");
    js_expect("-5n", "-5");
    js_expect("-(-5n)", "5");

    // --- bitwise, on an unbounded two's-complement value ----------------------
    js_expect("~5n", "-6");
    js_expect("5n & 3n", "1");
    js_expect("5n | 3n", "7");
    js_expect("5n ^ 3n", "6");
    js_expect("1n << 10n", "1024");
    js_expect("1024n >> 3n", "128");

    // --- comparison DOES cross to Number, and exactly -------------------------
    // Arithmetic between the two types is refused, but comparison is not - and
    // the comparison is exact rather than going through a double, or the two
    // values below would compare equal.
    js_expect("1n == 1", "true");
    js_expect("1n === 1", "false"); // === never crosses types
    js_expect("1n != 1", "false");
    js_expect("0n == 0", "true");
    js_expect("0n === 0", "false");
    js_expect("1n < 2", "true");
    js_expect("1n < 1.5", "true");
    js_expect("2n > 1.5", "true");
    js_expect("1n <= 1", "true");
    js_expect("1n >= 2", "false");
    js_expect("2n > 1n", "true");
    js_expect("1n < NaN", "false"); // as every comparison against NaN is
    js_expect("9007199254740993n == 9007199254740992", "false"); // NOT rounded together
    js_expect("1n == \"1\"", "true");                            // a string converts to BigInt here
    js_expect("1n === 1n", "true");                              // by VALUE, not by allocation
    js_expect("2n === 1n", "false");

    // --- conversion ------------------------------------------------------------
    js_expect("String(1n)", "1"); // the digits, with no trailing `n`
    js_expect("`${5n}`", "5");
    js_expect("1n + \"a\"", "1a"); // concatenation is allowed; addition is not
    js_expect("\"a\" + 1n", "a1");
    js_expect("[1n, 2n].join(\",\")", "1,2");
    js_expect("!!0n", "false"); // 0n is the only falsy bigint
    js_expect("!!1n", "true");
    js_expect("(255n).toString(16)", "ff");
    js_expect("(255n).toString(2)", "11111111");
    js_expect("BigInt(5)", "5");
    js_expect("BigInt(\"42\")", "42");
    js_expect("BigInt(\"0x10\")", "16");
    js_expect("BigInt(true)", "1");
    js_expect("typeof BigInt(5)", "bigint");
    js_expect("typeof BigInt", "function");
    js_expect("Number(1n)", "1"); // the EXPLICIT conversion, which is permitted

    // --- the refusals, which are the safety property --------------------------
    // Mixing a BigInt with a Number in arithmetic is a TypeError. An engine
    // that coerced instead would round at exactly the point the type was
    // reached for, so the throw IS the feature.
    const auto throws = [](const char * expression, const char * error) {
        js_expect(std::string{"(function(){try{return "} + expression +
                      "}catch(e){return \"THROWS \"+e.name}})()",
                  std::string{"THROWS "} + error);
    };
    throws("1n+1", "TypeError");
    throws("1+1n", "TypeError");
    throws("1n-2", "TypeError");
    throws("1n*2", "TypeError");
    throws("1n/2", "TypeError");
    // Unary plus is ToNumber, so it is refused too - `+1n` does not convert.
    throws("+1n", "TypeError");
    // Anything that reaches ToNumber implicitly refuses the same way.
    throws("Math.abs(1n)", "TypeError");
    // There is no lossless JSON spelling for a BigInt, so it throws rather than
    // picking between a string and a rounded number.
    throws("JSON.stringify(1n)", "TypeError");
    // `>>>` needs a WIDTH to fill from and a BigInt has none.
    throws("1n>>>1n", "TypeError");
    // No BigInt infinity, so division by zero has no value to give.
    throws("1n/0n", "RangeError");
    throws("1n%0n", "RangeError");
    // No fractions, so a negative exponent has none either.
    throws("2n**-1n", "RangeError");
    // BigInt() refuses what it cannot represent rather than truncating.
    throws("BigInt(1.5)", "RangeError");
    throws("BigInt(NaN)", "RangeError");
    throws("BigInt(\"zz\")", "SyntaxError");

    REPORT("bigint_basics");
}
