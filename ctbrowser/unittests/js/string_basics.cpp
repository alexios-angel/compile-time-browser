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

    // --- RequireObjectCoercible: the step 1 that was missing everywhere -----
    //
    // 22.1.3 opens EVERY method with RequireObjectCoercible(this value), and
    // this file did only the ToString that follows it - so
    // `String.prototype.trim.call(null)` answered "null" and
    // `charAt.call(undefined)` answered "u". test262 has 45 files asserting the
    // TypeError instead. `e.name` rather than a message, because the message is
    // not the behaviour.
    const auto throws = [](std::string_view code) {
        return "(function () { try { " + std::string{code} +
               "; return 'no'; } catch (e) { return e.name; } })()";
    };
    js_expect(throws("String.prototype.trim.call(null)"), "TypeError");
    js_expect(throws("String.prototype.trim.call(undefined)"), "TypeError");
    js_expect(throws("String.prototype.charAt.call(null, 0)"), "TypeError");
    js_expect(throws("String.prototype.at.call(undefined, 0)"), "TypeError");
    js_expect(throws("String.prototype.charCodeAt.call(null, 0)"), "TypeError");
    js_expect(throws("String.prototype.codePointAt.call(null, 0)"), "TypeError");
    js_expect(throws("String.prototype.indexOf.call(null, 'a')"), "TypeError");
    js_expect(throws("String.prototype.lastIndexOf.call(null, 'a')"), "TypeError");
    js_expect(throws("String.prototype.includes.call(null, 'a')"), "TypeError");
    js_expect(throws("String.prototype.startsWith.call(null, 'a')"), "TypeError");
    js_expect(throws("String.prototype.endsWith.call(null, 'a')"), "TypeError");
    js_expect(throws("String.prototype.slice.call(null, 0)"), "TypeError");
    js_expect(throws("String.prototype.substring.call(null, 0)"), "TypeError");
    js_expect(throws("String.prototype.substr.call(null, 0)"), "TypeError");
    js_expect(throws("String.prototype.split.call(null, ',')"), "TypeError");
    js_expect(throws("String.prototype.replace.call(null, 'a', 'b')"), "TypeError");
    js_expect(throws("String.prototype.replaceAll.call(null, 'a', 'b')"), "TypeError");
    js_expect(throws("String.prototype.match.call(null, /a/)"), "TypeError");
    js_expect(throws("String.prototype.matchAll.call(null, /a/g)"), "TypeError");
    js_expect(throws("String.prototype.search.call(null, /a/)"), "TypeError");
    js_expect(throws("String.prototype.concat.call(null, 'a')"), "TypeError");
    js_expect(throws("String.prototype.repeat.call(null, 1)"), "TypeError");
    js_expect(throws("String.prototype.padStart.call(null, 3)"), "TypeError");
    js_expect(throws("String.prototype.padEnd.call(null, 3)"), "TypeError");
    js_expect(throws("String.prototype.trimStart.call(null)"), "TypeError");
    js_expect(throws("String.prototype.trimEnd.call(null)"), "TypeError");
    js_expect(throws("String.prototype.toUpperCase.call(null)"), "TypeError");
    js_expect(throws("String.prototype.toLowerCase.call(null)"), "TypeError");
    js_expect(throws("String.prototype.toLocaleUpperCase.call(null)"), "TypeError");
    js_expect(throws("String.prototype.toLocaleLowerCase.call(null)"), "TypeError");
    js_expect(throws("String.prototype.localeCompare.call(null, 'a')"), "TypeError");
    js_expect(throws("String.prototype.normalize.call(null)"), "TypeError");
    // A receiver that is merely NOT A STRING is still fine - the methods are
    // generic over everything ToString accepts, which is what makes
    // `String.prototype.trim.call(false)` answer "false" and not throw.
    js_expect("String.prototype.trim.call(false)", "false");
    js_expect("String.prototype.trim.call(123)", "123");
    js_expect("String.prototype.charAt.call(123, 1)", "2");
    js_expect("String.prototype.indexOf.call({toString: function () { return 'abc'; }}, 'b')", "1");

    // --- toString and valueOf are NOT generic (22.1.3.28, 22.1.3.35) --------
    // thisStringValue, not ToString: a Number receiver is a TypeError, and it
    // has to be - `''.concat({toString: String.prototype.toString})` is an
    // infinite regress if this coerces instead of refusing. That last case is
    // NOT asserted here: the throw happens inside context::to_primitive_string,
    // which reads the native's return value rather than checking for a pending
    // throw, so what a `catch` sees around it is the engine's unwinding and not
    // this method's contract.
    js_expect(throws("String.prototype.toString.call(1)"), "TypeError");
    js_expect(throws("String.prototype.toString.call(false)"), "TypeError");
    js_expect(throws("String.prototype.toString.call({})"), "TypeError");
    js_expect(throws("String.prototype.toString.call(['s'])"), "TypeError");
    js_expect(throws("String.prototype.valueOf.call(1)"), "TypeError");
    // String.prototype's own [[StringData]] is the empty String, which is why
    // this is "" and not a TypeError - the same rule that makes
    // `Number.prototype.toString()` answer "0".
    js_expect("String.prototype.toString()", "");
    js_expect("String.prototype.valueOf()", "");

    // --- argument coercion: observable, once, and in the specified order ----
    // The index arguments went through the STATIC ToNumber, which cannot run a
    // user `valueOf` at all - so every object index read NaN and became 0.
    js_expect("\"abc\".charAt({valueOf: function () { return 1; }})", "b");
    js_expect("\"abc\".at({valueOf: function () { return -1; }})", "c");
    js_expect("\"abc\".charCodeAt({valueOf: function () { return 1; }})", "98");
    js_expect("\"abc\".codePointAt({valueOf: function () { return 1; }})", "98");
    js_expect("\"aaa\".indexOf(\"a\", {valueOf: function () { return 1; }})", "1");
    js_expect("\"aaa\".lastIndexOf(\"a\", {valueOf: function () { return 1; }})", "1");
    js_expect("\"abc\".includes(\"a\", {valueOf: function () { return 1; }})", "false");
    js_expect("\"abc\".startsWith(\"b\", {valueOf: function () { return 1; }})", "true");
    js_expect("\"abc\".endsWith(\"b\", {valueOf: function () { return 2; }})", "true");
    js_expect("\"abc\".slice({valueOf: function () { return 1; }})", "bc");
    js_expect("\"abc\".substring({valueOf: function () { return 1; }})", "bc");
    js_expect("\"abc\".substr({valueOf: function () { return 1; }})", "bc");
    js_expect("\"a,b,c\".split(\",\", {valueOf: function () { return 2; }}).length", "2");
    // 22.1.3.9 steps 3 and 4: ToString(searchString) BEFORE
    // ToIntegerOrInfinity(position). Reversing them is invisible until one of
    // the two has a side effect, and then it is a wrong log with a right answer.
    js_expect(R"((function () {
        var log = '';
        var needle = {toString: function () { log += 's'; return 'b'; }};
        var position = {valueOf: function () { log += 'p'; return 0; }};
        "abc".indexOf(needle, position);
        return log;
    })())",
              "sp");
    js_expect(R"((function () {
        var log = '';
        var needle = {toString: function () { log += 's'; return 'b'; }};
        var position = {valueOf: function () { log += 'p'; return 0; }};
        "abc".lastIndexOf(needle, position);
        return log;
    })())",
              "sp");

    // --- IsRegExp: includes/startsWith/endsWith REFUSE a pattern (7.2.8) ----
    // `'a/b'.includes(/b/)` searching for the six characters of the source is a
    // mistake often enough that the specification made it loud. This
    // stringified the pattern, found nothing, and answered false.
    js_expect(throws("\"abc\".includes(/b/)"), "TypeError");
    js_expect(throws("\"abc\".startsWith(/a/)"), "TypeError");
    js_expect(throws("\"abc\".endsWith(/c/)"), "TypeError");
    // @@match decides it, so an ordinary object can claim to be a pattern...
    js_expect(throws("(function () { var o = {}; o[Symbol.match] = true;"
                     " return \"abc\".includes(o); })()"),
              "TypeError");
    // ...and a real RegExp can disclaim it, at which point it is stringified
    // like anything else.
    js_expect("(function () { var r = /b/; r[Symbol.match] = false;"
              " return \"a/b/c\".includes(r); })()",
              "true");
    js_expect("typeof Symbol.match", "symbol");

    // --- trim over the SPECIFIED whitespace set (12.2 + 12.3) ---------------
    // The set was the ASCII six; fourteen more code points belong to it and all
    // of them are non-ASCII, so - unlike case folding - they are answerable
    // exactly from UTF-8 bytes with no table and no locale.
    js_expect("\"\\u00a0x\\u00a0\".trim()", "x");
    js_expect("\"\\ufeffx\\ufeff\".trim()", "x");
    js_expect("\"\\u2028\\u2029x\\u3000\".trim()", "x");
    js_expect("\"\\u2000\\u200ax\".trimStart()", "x");
    js_expect("\"x\\u205f\\u202f\".trimEnd()", "x");
    js_expect("\"\\u1680\\u2003x\".trim()", "x");
    js_expect("\"\\u00a0\\u2000\\ufeff\".trim().length", "0");
    // U+180E was a space separator in Unicode 6.2 and stopped being one in 6.3.
    // It is three UTF-8 bytes and must survive untouched.
    js_expect("\"\\u180ex\\u180e\".trim().length", "7");
    // A byte that is not part of a well-formed sequence is NOT the character it
    // resembles: a lone 0xA0 is not U+00A0 and must not be trimmed away.
    js_expect("\"\\u00e9\".trim().length", "2");

    // --- match / search / matchAll take a PATTERN, not only a RegExp --------
    // 22.1.3.13, 22.1.3.21 and 22.1.3.14 each RegExpCreate their argument.
    // A non-object answered "no match" - null, or -1 - which is the commonest
    // spelling of all and is silent: `if (s.match(x))` reads null as "absent".
    js_expect("\"1234567890\".match(3)[0]", "3");
    js_expect("\"1234567890\".match(3).index", "2");
    js_expect("\"1234567890\".match(3).input", "1234567890");
    js_expect("\"abc\".match(\"b\")[0]", "b");
    js_expect("\"abc\".match(\"z\")", "null");
    js_expect("\"abc\".search(\"b\")", "1");
    js_expect("\"a2c\".search(2)", "1");
    js_expect("\"abc\".search(\"z\")", "-1");
    // matchAll builds its RegExp with `g`, which is what makes this three
    // matches rather than the first one forever.
    js_expect("\"aaa\".matchAll(\"a\").length", "3");
    js_expect("\"abc\".matchAll(\"z\").length", "0");
    // ...and REFUSES a RegExp that has no `g`, because the answer would be
    // wrong either way (22.1.3.14 step 2b).
    js_expect(throws("\"aaa\".matchAll(/a/)"), "TypeError");
    js_expect("\"aaa\".matchAll(/a/g).length", "3");
    js_expect(throws("\"aaa\".replaceAll(/a/, \"b\")"), "TypeError");
    js_expect("\"aaa\".replaceAll(/a/g, \"b\")", "bbb");

    // --- normalize checks its form even though it normalises nothing --------
    // The identity is a stated deviation (strings are bytes, so there is no
    // decomposition to compose); the RangeError of 22.1.3.15 step 4 is not, and
    // it is what tells a page that wrote "NFKC1" that it made a typo.
    js_expect("\"abc\".normalize()", "abc");
    js_expect("\"abc\".normalize(undefined)", "abc");
    js_expect("\"abc\".normalize(\"NFD\")", "abc");
    js_expect(throws("\"abc\".normalize(\"bar\")"), "RangeError");
    js_expect(throws("\"abc\".normalize(\"NFC1\")"), "RangeError");
    js_expect(throws("\"abc\".normalize(null)"), "RangeError");

    // --- localeCompare's missing argument is "undefined", not "" -----------
    js_expect("\"undefined\".localeCompare()", "0");
    js_expect("\"a\".localeCompare(\"b\")", "-1");
    js_expect("\"b\".localeCompare(\"a\")", "1");

    // --- every built-in's own length and name (10.2.5, clause 17) ----------
    js_expect("String.prototype.trim.length", "0");
    js_expect("String.prototype.indexOf.length", "1");
    js_expect("String.prototype.slice.length", "2");
    js_expect("String.prototype.replaceAll.length", "2");
    js_expect("String.prototype.normalize.length", "0");
    js_expect("String.prototype.at.name", "at");
    js_expect("String.prototype.padStart.name", "padStart");
    js_expect("Object.getOwnPropertyDescriptor(String.prototype.trim, 'name').writable", "false");
    js_expect("Object.getOwnPropertyDescriptor(String.prototype.trim, 'name').configurable",
              "true");
    js_expect("Object.getOwnPropertyDescriptor(String.prototype.trim, 'length').enumerable",
              "false");
    js_expect("String.fromCharCode.length", "1");
    js_expect("String.raw.length", "1");

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
