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

// `e.name` for whatever the expression threw, or "no" when it did not throw at
// all. Every RangeError case below is one of these: what is asserted is WHICH
// error, and js_expect's flattened "THREW" cannot say.
[[nodiscard]] static std::string threw(std::string_view source) {
    return "(function () { try { " + std::string{source} +
           "; return 'no'; } catch (e) { return e.name; } })()";
}

int main() {
    // --- the type ------------------------------------------------------------
    js_expect("typeof 1", "number");
    js_expect("typeof NaN", "number"); // NaN is a number, which is the joke
    // AND A NaN WITH A PAYLOAD IS STILL A NUMBER, after arithmetic. The engine
    // NaN-boxes: a boxed tag is a NaN with bits 51 AND 50 set. A Float64Array
    // lets a script write 0x7FF4000000000003 - bit 50 set, bit 51 clear, so it
    // passes is_number() - and the first `- 1` quiets bit 51 into exactly
    // tag_true. `typeof (x - 1)` answered "boolean". view_get canonicalises
    // every NaN it reads, and these four payloads are the four tags.
    js_expect("(function(){var b=new ArrayBuffer(8),u=new Uint8Array(b),f=new Float64Array(b);"
              "u[6]=0xF4;u[7]=0x7F;var r=[];for(var p=0;p<4;p++){u[0]=p;r.push(typeof (f[0]-1),"
              "typeof (f[0]*1),typeof (f[0]/2),f[0]-1===true,Object.is(f[0]-1,NaN));}return "
              "r.join();})()",
              "number,number,number,false,true,number,number,number,false,true,"
              "number,number,number,false,true,number,number,number,false,true");
    // A Float32Array payload widens bit 21 into bit 50 and takes the same road.
    js_expect("(function(){var b=new ArrayBuffer(4),u=new Uint8Array(b),f=new Float32Array(b);"
              "u[3]=0x7F;u[2]=0xA0;u[0]=3;return typeof (f[0]-1)+\",\"+Object.is(f[0]-1,NaN);})()",
              "number,true");
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

    // --- A DIGIT COUNT OUT OF RANGE IS A RangeError, NOT A CLAMP --------------
    // 21.1.3.3 step 4, 21.1.3.2 step 5 and 21.1.3.5 step 5. All three clamped,
    // which is wrong in both directions: a negative count answered a plausible
    // string instead of throwing, and the ceiling was 20 where the
    // specification's is 100, so `(3).toFixed(50)` was silently 20 places.
    js_expect("(3).toFixed(-0)", "3");
    js_expect(threw("(3).toFixed(-1)"), "RangeError");
    js_expect(threw("(3).toFixed(101)"), "RangeError");
    js_expect("(3).toFixed(100).length", "102"); // "3." and a hundred zeros
    js_expect(threw("(3).toExponential(-1)"), "RangeError");
    js_expect(threw("(3).toExponential(101)"), "RangeError");
    js_expect("(3).toExponential(100).length", "105"); // "3.", 100 zeros, "e+0"
    js_expect(threw("(3).toPrecision(0)"), "RangeError");
    js_expect(threw("(3).toPrecision(-10)"), "RangeError");
    js_expect(threw("(3).toPrecision(101)"), "RangeError");
    js_expect("(3).toPrecision(100).length", "101"); // "3." and 99 more digits
    // An INFINITE count is out of range rather than a very large one, which is
    // why the coercion preserves infinity instead of clamping it.
    js_expect(threw("(3).toFixed(Infinity)"), "RangeError");
    // The count coerces through the receiver's own valueOf - it used to go
    // through the STATIC ToNumber, which answers NaN for every object, and a
    // NaN then clamped to zero.
    js_expect("(1.567).toFixed({valueOf: function () { return 2; }})", "1.57");
    // A NON-FINITE RECEIVER answers before the range check in two of the three
    // and after it in toFixed - the specification orders those steps
    // differently, and the difference is asserted rather than smoothed over.
    js_expect("NaN.toExponential(Infinity)", "NaN");
    js_expect("Infinity.toPrecision(1000)", "Infinity");
    js_expect(threw("NaN.toFixed(101)"), "RangeError");
    // ...and the argument is still coerced first in every one of them.
    js_expect("(function () { var n = 0;"
              " NaN.toPrecision({valueOf: function () { n++; return Infinity; }});"
              " return n; })()",
              "1");

    // --- toString's radix goes through ToIntegerOrInfinity --------------------
    // `static_cast<int>` of a NaN is undefined behaviour, and that is what this
    // was: 7.1.5 makes NaN zero, and zero is out of range.
    js_expect(threw("(1).toString(NaN)"), "RangeError");
    js_expect(threw("(1).toString(Infinity)"), "RangeError");
    js_expect(threw("(1).toString(1)"), "RangeError");
    js_expect(threw("(1).toString(37)"), "RangeError");
    js_expect("(255).toString(16.9)", "ff"); // truncates toward zero
    js_expect("(255).toString(undefined)", "255");
    // The radix is coerced even when the receiver cannot use one - NaN is
    // answered AFTER the radix, not instead of it.
    js_expect("(function () { var n = 0;"
              " NaN.toString({valueOf: function () { n++; return 16; }}); return n; })()",
              "1");

    // --- toLocaleString, which was simply absent ------------------------------
    // 21.1.3.4 with no ECMA-402 present is ToString of the number, and the
    // difference from not having it at all is "1000" against "not a function".
    js_expect("(1000).toLocaleString()", "1000");
    js_expect("(1.5).toLocaleString()", "1.5");
    js_expect("typeof Number.prototype.toLocaleString", "function");

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
