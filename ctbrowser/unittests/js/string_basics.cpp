// Strings, against V8.
//
// Written by differentially testing String.prototype and the relational
// operators against node (V8, the engine Chrome ships) over ~1,550 expressions.
// As with unittests/js/math_basics.cpp the expectations came out of V8 BEFORE the
// fixes were written, so this file failed on the day it was committed rather
// than agreeing with the bug. The sweep found 233 differences; the fixes below
// took that to 60, of which 8 are deliberate (see the case-folding note).
//
// TWO THINGS THIS FILE DELIBERATELY DOES NOT ASSERT.
//
// 1. NON-ASCII LENGTHS AND INDICES. This engine stores a JS string as UTF-8
//    BYTES; ECMA-262 specifies a sequence of UTF-16 CODE UNITS. So `"é".length`
//    is 2 here and 1 in V8, and a non-BMP emoji is 4 here and 2 there. That is a
//    representation difference, not a bug list, and closing it means changing
//    `string_object` engine-wide. The `-- the UTF-16 gap --` section at the
//    bottom PINS THE CURRENT (WRONG) ANSWERS on purpose, labelled, so that the
//    day someone migrates the representation the test fails and hands them a
//    ready-made acceptance list.
//
// 2. UNICODE CASE FOLDING. `"Straße".toUpperCase()` is "STRASSE" in V8 and
//    "STRAßE" here, and `"İ".toLowerCase()` differs likewise. That is BY
//    DESIGN: `core/algorithms.hpp` folds ASCII only, on the stated grounds that
//    this repository byte-compares rendered output across Linux and Windows and
//    a locale- or table-driven fold would make a render depend on the host. The
//    ASCII behaviour is asserted; the rest is left alone.

#include "js_expect.hpp"

int main() {
    // --- the relational operators -------------------------------------------
    // THE BIGGEST THING THIS SWEEP FOUND. All four opcodes were
    // `to_number(a) < to_number(b)`, and ToNumber of a non-numeric string is
    // NaN, so every comparison between two strings was FALSE - `<`, `>`, `<=`
    // and `>=` alike. A page ordering names or keys with
    // `(x, y) => x < y ? -1 : 1` got its input back untouched, with no error.
    js_expect("\"a\" < \"b\"", "true");
    js_expect("\"b\" > \"a\"", "true");
    js_expect("\"a\" <= \"b\"", "true");
    js_expect("\"b\" >= \"a\"", "true");
    js_expect("\"a\" < \"a\"", "false");
    js_expect("\"a\" <= \"a\"", "true");
    js_expect("\"abc\" < \"abd\"", "true");
    js_expect("\"abc\" < \"ab\"", "false"); // a prefix is LESS, so this is false
    js_expect("\"ab\" < \"abc\"", "true");
    js_expect("\"\" < \"a\"", "true");
    js_expect("\"A\" < \"a\"", "true"); // by code unit, so capitals sort first
    // Text, NOT numbers: "10" < "9" is true because "1" precedes "9".
    js_expect("\"10\" < \"9\"", "true");
    js_expect("[\"b\",\"a\",\"c\"].sort(function(x,y){return x<y?-1:(x>y?1:0)}).join(\"\")", "abc");
    // Numbers still compare as numbers, and a string against a number coerces.
    js_expect("1 < 2", "true");
    js_expect("2 <= 2", "true");
    js_expect("\"5\" < 10", "true");
    js_expect("10 < \"9\"", "false");
    // ToPrimitive first: an array becomes its join, and two of those are
    // strings, so this is a TEXT comparison.
    js_expect("[2] < [3]", "true");
    // NaN makes all four false - that is the `undefined` result of 7.2.13.
    js_expect("NaN < 1", "false");
    js_expect("1 < NaN", "false");
    js_expect("NaN <= NaN", "false");
    js_expect("NaN >= NaN", "false");

    // --- at, and the index coercion that hung the engine --------------------
    // `"abc".at(NaN)` cast NaN to size_t - undefined behaviour - and the engine
    // HUNG. A missing argument is undefined and ToNumber(undefined) is NaN, so
    // `.at()`, `.at(undefined)` and `.at({})` all reached it. ToIntegerOrInfinity
    // makes NaN zero, which is both the fix and the specification.
    js_expect("\"abc\".at(0)", "a");
    js_expect("\"abc\".at(2)", "c");
    js_expect("\"abc\".at(-1)", "c"); // at DOES count from the end
    js_expect("\"abc\".at(-3)", "a");
    js_expect("\"abc\".at(3)", "undefined");
    js_expect("\"abc\".at(-4)", "undefined");
    js_expect("\"abc\".at()", "a");
    js_expect("\"abc\".at(NaN)", "a");
    js_expect("\"abc\".at(undefined)", "a");
    js_expect("\"abc\".at({})", "a");
    js_expect("\"abc\".at(1.7)", "b"); // truncates toward zero
    js_expect("\"\".at(0)", "undefined");
    js_expect("\"\".at(NaN)", "undefined");

    // --- charAt / charCodeAt / codePointAt ----------------------------------
    // A NEGATIVE POSITION IS OUT OF RANGE, not clamped to zero. Clamping made
    // `"abc".charAt(-1)` answer "a" and `charCodeAt(-1)` answer 97.
    js_expect("\"abc\".charAt(1)", "b");
    js_expect("\"abc\".charAt(-1)", "");
    js_expect("\"abc\".charAt(3)", "");
    js_expect("\"abc\".charAt()", "a");
    js_expect("\"abc\".charAt(NaN)", "a");
    js_expect("\"abc\".charCodeAt(0)", "97");
    js_expect("\"abc\".charCodeAt(-1)", "NaN");
    js_expect("\"abc\".charCodeAt(3)", "NaN");
    js_expect("\"abc\".charCodeAt()", "97");
    js_expect("\"abc\".codePointAt(0)", "97");
    js_expect("\"abc\".codePointAt(-1)", "undefined");
    js_expect("\"abc\".codePointAt(3)", "undefined");

    // --- the search family, which all ignored their position ----------------
    // `"abc".indexOf("a", 1)` answered 0, so the idiom for walking every
    // occurrence - `while ((i = s.indexOf(x, i + 1)) !== -1)` - never advanced.
    js_expect("\"abc\".indexOf(\"a\")", "0");
    js_expect("\"abc\".indexOf(\"a\", 1)", "-1");
    js_expect("\"aaa\".indexOf(\"a\", 1)", "1");
    js_expect("\"aaa\".indexOf(\"a\", 99)", "-1");
    js_expect("\"aaa\".indexOf(\"a\", -5)", "0");
    js_expect("\"abc\".indexOf(\"z\")", "-1");
    js_expect("\"abc\".indexOf(\"\")", "0");
    // A MISSING NEEDLE IS "undefined", not the empty string.
    js_expect("\"abc\".indexOf()", "-1");
    js_expect("\"undefined\".indexOf()", "0");
    js_expect("\"aaa\".lastIndexOf(\"a\")", "2");
    js_expect("\"aaa\".lastIndexOf(\"a\", 1)", "1");
    js_expect("\"aaa\".lastIndexOf(\"a\", 0)", "0");
    js_expect("\"abc\".includes(\"bc\")", "true");
    js_expect("\"abc\".includes(\"a\", 1)", "false");
    js_expect("\"abc\".includes(\"b\", 1)", "true");
    js_expect("\"abc\".startsWith(\"a\")", "true");
    js_expect("\"abc\".startsWith(\"b\", 1)", "true");
    js_expect("\"abc\".startsWith(\"b\")", "false");
    js_expect("\"abc\".endsWith(\"c\")", "true");
    js_expect("\"abc\".endsWith(\"b\", 2)", "true"); // an END position, not a start
    js_expect("\"abc\".endsWith(\"b\")", "false");

    // --- slice / substring / substr -----------------------------------------
    // An explicit `undefined` end means "to the end", exactly as an ABSENT one
    // does. Testing the argument count alone coerced it to 0 and returned "".
    js_expect("\"abc\".slice(1)", "bc");
    js_expect("\"abc\".slice(1, undefined)", "bc");
    js_expect("\"abc\".slice(-2)", "bc");
    js_expect("\"abc\".slice(0, -1)", "ab");
    js_expect("\"abc\".slice(2, 1)", "");
    js_expect("\"abc\".slice()", "abc");
    js_expect("\"abc\".substring(1)", "bc");
    js_expect("\"abc\".substring(1, undefined)", "bc");
    js_expect("\"abc\".substring(2, 1)", "b"); // substring SWAPS a reversed range
    js_expect("\"abc\".substring(-1)", "abc"); // and clamps a negative, unlike slice
    js_expect("\"abc\".substr(1)", "bc");
    js_expect("\"abc\".substr(1, undefined)", "bc");
    js_expect("\"abc\".substr(-2)", "bc");
    js_expect("\"abc\".substr(NaN)", "abc");
    js_expect("\"abc\".substr(1, 1)", "b");

    // --- split, whose limit was ignored -------------------------------------
    js_expect("\"a-b-c\".split(\"-\").join(\"|\")", "a|b|c");
    js_expect("\"a-b-c\".split(\"-\", 2).join(\"|\")", "a|b");
    js_expect("\"a-b-c\".split(\"-\", 0).length", "0");
    js_expect("\"abc\".split(\"\", 2).join(\"|\")", "a|b");
    js_expect("\"abc\".split(\"\").length", "3");
    js_expect("\"abc\".split(\"-\").length", "1");
    js_expect("\"abc\".split().length", "1");
    js_expect("\"a1b2c\".split(/[0-9]/).join(\"|\")", "a|b|c");

    // --- the parts that already worked, pinned so they keep working ---------
    js_expect("\"abc\".length", "3");
    js_expect("\"abc\"[1]", "b");
    js_expect("\"abc\".concat(\"d\", \"e\")", "abcde");
    js_expect("\"ab\".repeat(3)", "ababab");
    js_expect("\"ab\".repeat(0)", "");
    js_expect("\"abc\".padStart(5, \"xy\")", "xyabc");
    js_expect("\"abc\".padEnd(5, \"xy\")", "abcxy");
    js_expect("\"abc\".padStart(2)", "abc"); // already long enough
    js_expect("\"  x  \".trim()", "x");
    js_expect("\"  x  \".trimStart()", "x  ");
    js_expect("\"  x  \".trimEnd()", "  x");
    js_expect("\"AbC\".toUpperCase()", "ABC");
    js_expect("\"AbC\".toLowerCase()", "abc");
    js_expect("\"abc\".replace(\"b\", \"X\")", "aXc");
    js_expect("\"aaa\".replaceAll(\"a\", \"b\")", "bbb");
    js_expect("\"abc\".match(/b/)[0]", "b");
    js_expect("\"a\" + 1", "a1");
    js_expect("`x${1 + 1}y`", "x2y");
    js_expect("String(null)", "null");
    js_expect("String(undefined)", "undefined");
    js_expect("String(1)", "1");
    js_expect("String.fromCharCode(97, 98)", "ab");
    js_expect("typeof \"abc\"", "string");
    js_expect("\"abc\".toString()", "abc");
    js_expect("\"abc\".valueOf()", "abc");
    js_expect("\"a\" === \"a\"", "true");
    js_expect("\"a\" !== \"b\"", "true");

    // --- the UTF-16 gap: CURRENT BEHAVIOUR, KNOWN WRONG ---------------------
    //
    // Everything in this block disagrees with V8, and every line says what V8
    // answers. They are pinned because a silent gap is worse than a recorded
    // one: this is the acceptance list for a UTF-16 migration, and the day the
    // representation changes these lines fail and say exactly what to update.
    js_expect("\"e\\u0301\".length", "3");         // V8: 2 - a combining mark is 2 UTF-8 bytes
    js_expect("\"\\u00e9\".length", "2");          // V8: 1 - one code unit, two UTF-8 bytes
    js_expect("\"\\u00e9\".charCodeAt(0)", "195"); // V8: 233
    js_expect("\"\\ud83d\\ude00\".length", "4");   // V8: 2 - a surrogate pair
    js_expect("\"\\ud83d\\ude00\".charCodeAt(0)", "240");    // V8: 55357
    js_expect("\"\\ud83d\\ude00\".split(\"\").length", "4"); // V8: 2
    js_expect("[...\"\\ud83d\\ude00\"].length", "4");        // V8: 1 - iteration is by code POINT
    js_expect("\"\\u00e9\".slice(0, 1).length", "1");        // V8: 1, but a DIFFERENT character

    // --- ASCII-only case folding: BY DESIGN, not a defect -------------------
    // core/algorithms.hpp folds A-Z and nothing else, so a golden cannot depend
    // on the host's locale or Unicode tables. V8's answers are in the comments.
    js_expect("\"Stra\\u00dfe\".toUpperCase()", "STRA\u00dfE"); // V8: STRASSE
    js_expect("\"\\u0130\".toLowerCase()", "\u0130");           // V8: i followed by U+0307

    REPORT("string_basics");
}
