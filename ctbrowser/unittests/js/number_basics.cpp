// The `number` TYPE, against V8.
//
// One IEEE-754 double for every number in the language: no integers, no
// separate float, and every surprise that follows from that. Differentially
// tested against node (V8); 3 differences, pinned at the bottom.
//
// This is the type's shape - limits, the two zeros, NaN, the statics and the
// prototype. How a number turns into TEXT is a different question with its own
// suite, unittests/js/number_format.cpp, which pins Number::toString, toFixed,
// toExponential and toPrecision against V8 in far more detail than here.

#include "js_expect.hpp"

int main() {
    // --- the type ------------------------------------------------------------
    js_expect("typeof 1", "number");
    js_expect("typeof NaN", "number"); // NaN is a number, which is the joke
    js_expect("typeof Infinity", "number");
    js_expect("typeof Number", "function");
    js_expect("typeof (1).toFixed", "function");
    js_expect("(1).valueOf()", "1");

    // --- it is a double, so ---------------------------------------------------
    js_expect("0.1 + 0.2", "0.30000000000000004");
    js_expect("0.1 + 0.2 === 0.3", "false");
    js_expect("1/3", "0.3333333333333333");
    js_expect("2**53", "9007199254740992");
    js_expect("2**53 === 2**53+1", "true"); // the first integer that is not exact
    js_expect("Number.MAX_SAFE_INTEGER", "9007199254740991");
    js_expect("Number.MIN_SAFE_INTEGER", "-9007199254740991");
    js_expect("Number.MAX_VALUE", "1.7976931348623157e+308");
    js_expect("Number.MIN_VALUE", "5e-324"); // the smallest DENORMAL, not the most negative
    js_expect("Number.EPSILON", "2.220446049250313e-16");
    js_expect("Number.POSITIVE_INFINITY", "Infinity");
    js_expect("Number.NEGATIVE_INFINITY", "-Infinity");

    // --- the two zeros --------------------------------------------------------
    // They compare equal under both == and ===, and the only ordinary way to
    // tell them apart is to divide.
    js_expect("0 === -0", "true");
    js_expect("1/0", "Infinity");
    js_expect("1/-0", "-Infinity");
    js_expect_negative_zero("-0", true);
    js_expect_negative_zero("0", false);
    js_expect_negative_zero("0 * -1", true);

    // --- NaN ------------------------------------------------------------------
    js_expect("0/0", "NaN");
    js_expect("NaN === NaN", "false"); // the only value not equal to itself
    js_expect("NaN !== NaN", "true");
    js_expect("NaN < 1", "false");
    js_expect("NaN > 1", "false");
    js_expect("NaN == NaN", "false");
    // Number.isNaN tests the VALUE; the global isNaN coerces first, which is
    // why it says true for a string that is merely not numeric.
    js_expect("Number.isNaN(NaN)", "true");
    js_expect("Number.isNaN(\"NaN\")", "false");
    js_expect("isNaN(\"NaN\")", "true");

    // --- the statics ----------------------------------------------------------
    js_expect("Number.isInteger(1)", "true");
    js_expect("Number.isInteger(1.5)", "false");
    js_expect("Number.isInteger(\"1\")", "false"); // no coercion
    js_expect("Number.isFinite(1)", "true");
    js_expect("Number.isFinite(Infinity)", "false");
    js_expect("Number.isFinite(\"1\")", "false");
    js_expect("isFinite(\"1\")", "true"); // the global one DOES coerce
    js_expect("Number.isSafeInteger(2**53-1)", "true");
    js_expect("Number.isSafeInteger(2**53)", "false");

    // --- parsing --------------------------------------------------------------
    js_expect("Number(\"\")", "0");
    js_expect("Number(\" 12 \")", "12");
    js_expect("Number(\"12a\")", "NaN"); // whole-string, unlike parseInt
    js_expect("+\"3\"", "3");
    js_expect("parseInt(\"42px\")", "42"); // a PREFIX parse
    js_expect("parseFloat(\"3.14abc\")", "3.14");
    js_expect("parseInt(\"0xFF\")", "255");

    // --- arithmetic edges -----------------------------------------------------
    js_expect("5 % 3", "2");
    js_expect("-5 % 3", "-2"); // the sign follows the DIVIDEND, unlike a modulus
    js_expect("5/0", "Infinity");
    js_expect("-5/0", "-Infinity");
    js_expect("~~4.9", "4"); // truncation toward zero
    js_expect("~~-4.9", "-4");
    js_expect("1e21", "1e+21");
    js_expect("1e-7", "1e-7");

    // --- radix and the prototype ----------------------------------------------
    js_expect("(255).toString(16)", "ff");
    js_expect("(255).toString(2)", "11111111");
    js_expect("(1.5).toFixed(2)", "1.50");
    js_expect("(1234.5).toPrecision(2)", "1.2e+3");
    js_expect("(1.5).toExponential(1)", "1.5e+0");

    // --- JSON, where non-finite numbers become null ---------------------------
    // NaN and the infinities are not representable in JSON, so 25.5.2
    // serialises them as null. Emitting the bare words produced output no JSON
    // parser would read back - a page round-tripping its own data through
    // JSON.parse got a SyntaxError from bytes this engine wrote.
    js_expect("JSON.stringify(NaN)", "null");
    js_expect("JSON.stringify(Infinity)", "null");
    js_expect("JSON.stringify(-Infinity)", "null");
    js_expect("JSON.stringify([NaN,Infinity])", "[null,null]");
    js_expect("JSON.stringify({a:NaN})", "{\"a\":null}");

    // --- Object.is, which is === plus the two questions it cannot answer ------
    js_expect("typeof Object.is", "function");
    js_expect("Object.is(0,-0)", "false"); // === says true
    js_expect("Object.is(-0,-0)", "true");
    js_expect("Object.is(NaN,NaN)", "true"); // === says false
    js_expect("Object.is(1,1)", "true");
    js_expect("Object.is({},{})", "false");

    REPORT("number_basics");
}
