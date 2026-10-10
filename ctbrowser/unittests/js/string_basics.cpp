//===- ctbrowser/unittests/js/string_basics.cpp - String tests ------------===//
//
// String.prototype, the String statics, the URI functions and the relational
// operators on strings, checked against node (V8). Expected values came from
// V8, recorded before the engine was fixed, so each case once failed here.
// Section numbers are ECMA-262's.
//
// Strings are UTF-16 code units over WTF-8 storage (script/utf16.hpp), so
// lengths and indices below match V8, including for non-ASCII text.
//
// Case conversion uses Unicode's default full mapping from a table generated
// off the pinned Unicode release (lib/Core/case_mapping.cpp, which native
// code shares). It is compiled in, so a render cannot depend on the host
// locale.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

int main() {
    // --- the relational operators -------------------------------------------
    // Two strings compare by code unit, not by ToNumber. (Comparing as numbers
    // makes every non-numeric string NaN, so all four operators would answer
    // false and a comparator-based sort would return its input unchanged.)
    js_expect("\"a\" < \"b\"", "true");
    js_expect("\"b\" > \"a\"", "true");
    js_expect("\"a\" <= \"b\"", "true");
    js_expect("\"b\" >= \"a\"", "true");
    js_expect("\"a\" < \"a\"", "false");
    js_expect("\"a\" <= \"a\"", "true");
    js_expect("\"abc\" < \"abd\"", "true");
    js_expect("\"abc\" < \"ab\"", "false"); // A prefix is less, so this is false.
    js_expect("\"ab\" < \"abc\"", "true");
    js_expect("\"\" < \"a\"", "true");
    js_expect("\"A\" < \"a\"", "true"); // By code unit, so capitals sort first.
    // Text, not numbers: "10" < "9" is true because "1" precedes "9".
    js_expect("\"10\" < \"9\"", "true");
    js_expect("[\"b\",\"a\",\"c\"].sort(function(x,y){return x<y?-1:(x>y?1:0)}).join(\"\")", "abc");
    // Numbers still compare as numbers, and a string against a number coerces.
    js_expect("1 < 2", "true");
    js_expect("2 <= 2", "true");
    js_expect("\"5\" < 10", "true");
    js_expect("10 < \"9\"", "false");
    // ToPrimitive first: an array becomes its join, and two of those are
    // strings, so this is a text comparison.
    js_expect("[2] < [3]", "true");
    // NaN makes all four false: that is the `undefined` result of 7.2.13.
    js_expect("NaN < 1", "false");
    js_expect("1 < NaN", "false");
    js_expect("NaN <= NaN", "false");
    js_expect("NaN >= NaN", "false");

    // --- at, and its index coercion ----------------------------------------
    // ToIntegerOrInfinity makes NaN zero. A missing argument is undefined and
    // ToNumber(undefined) is NaN, so `.at()`, `.at(undefined)` and `.at({})` all
    // index 0. (Casting NaN to size_t instead is undefined behaviour.)
    js_expect("\"abc\".at(0)", "a");
    js_expect("\"abc\".at(2)", "c");
    js_expect("\"abc\".at(-1)", "c"); // at() does count from the end.
    js_expect("\"abc\".at(-3)", "a");
    js_expect("\"abc\".at(3)", "undefined");
    js_expect("\"abc\".at(-4)", "undefined");
    js_expect("\"abc\".at()", "a");
    js_expect("\"abc\".at(NaN)", "a");
    js_expect("\"abc\".at(undefined)", "a");
    js_expect("\"abc\".at({})", "a");
    js_expect("\"abc\".at(1.7)", "b"); // Truncates toward zero.
    js_expect("\"\".at(0)", "undefined");
    js_expect("\"\".at(NaN)", "undefined");

    // --- charAt / charCodeAt / codePointAt ----------------------------------
    // A negative position is out of range, not clamped to zero. Clamping would
    // make `"abc".charAt(-1)` answer "a" and `charCodeAt(-1)` answer 97.
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

    // --- the search family honours its position argument -------------------
    // If `"abc".indexOf("a", 1)` answered 0, the idiom for walking every
    // occurrence, `while ((i = s.indexOf(x, i + 1)) !== -1)`, would never advance.
    js_expect("\"abc\".indexOf(\"a\")", "0");
    js_expect("\"abc\".indexOf(\"a\", 1)", "-1");
    js_expect("\"aaa\".indexOf(\"a\", 1)", "1");
    js_expect("\"aaa\".indexOf(\"a\", 99)", "-1");
    js_expect("\"aaa\".indexOf(\"a\", -5)", "0");
    js_expect("\"abc\".indexOf(\"z\")", "-1");
    js_expect("\"abc\".indexOf(\"\")", "0");
    // A missing needle is "undefined", not the empty string.
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
    js_expect("\"abc\".endsWith(\"b\", 2)", "true"); // An end position, not a start.
    js_expect("\"abc\".endsWith(\"b\")", "false");

    // --- slice / substring / substr -----------------------------------------
    // An explicit `undefined` end means "to the end", exactly as an absent one
    // does. Testing the argument count alone would coerce it to 0 and return "".
    js_expect("\"abc\".slice(1)", "bc");
    js_expect("\"abc\".slice(1, undefined)", "bc");
    js_expect("\"abc\".slice(-2)", "bc");
    js_expect("\"abc\".slice(0, -1)", "ab");
    js_expect("\"abc\".slice(2, 1)", "");
    js_expect("\"abc\".slice()", "abc");
    js_expect("\"abc\".substring(1)", "bc");
    js_expect("\"abc\".substring(1, undefined)", "bc");
    js_expect("\"abc\".substring(2, 1)", "b"); // substring swaps a reversed range...
    js_expect("\"abc\".substring(-1)", "abc"); // ...and clamps a negative, unlike slice.
    js_expect("\"abc\".substr(1)", "bc");
    js_expect("\"abc\".substr(1, undefined)", "bc");
    js_expect("\"abc\".substr(-2)", "bc");
    js_expect("\"abc\".substr(NaN)", "abc");
    js_expect("\"abc\".substr(1, 1)", "b");

    // --- split, including its limit ----------------------------------------
    js_expect("\"a-b-c\".split(\"-\").join(\"|\")", "a|b|c");
    js_expect("\"a-b-c\".split(\"-\", 2).join(\"|\")", "a|b");
    js_expect("\"a-b-c\".split(\"-\", 0).length", "0");
    js_expect("\"abc\".split(\"\", 2).join(\"|\")", "a|b");
    js_expect("\"abc\".split(\"\").length", "3");
    js_expect("\"abc\".split(\"-\").length", "1");
    js_expect("\"abc\".split().length", "1");
    js_expect("\"a1b2c\".split(/[0-9]/).join(\"|\")", "a|b|c");

    // --- everyday string operations -----------------------------------------
    js_expect("\"abc\".length", "3");
    js_expect("\"abc\"[1]", "b");
    js_expect("\"abc\".concat(\"d\", \"e\")", "abcde");
    js_expect("\"ab\".repeat(3)", "ababab");
    js_expect("\"ab\".repeat(0)", "");
    js_expect("\"abc\".padStart(5, \"xy\")", "xyabc");
    js_expect("\"abc\".padEnd(5, \"xy\")", "abcxy");
    js_expect("\"abc\".padStart(2)", "abc"); // Already long enough.
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

    // --- RequireObjectCoercible ---------------------------------------------
    // Every String.prototype method (22.1.3) starts with
    // RequireObjectCoercible(this), so a null or undefined receiver throws
    // TypeError rather than being stringified to "null" or "undefined". Tests
    // check `e.name`, not the message, because the message is not specified.
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
    // A receiver that is merely not a string is still fine: the methods are
    // generic over everything ToString accepts, which is what makes
    // `String.prototype.trim.call(false)` answer "false" and not throw.
    js_expect("String.prototype.trim.call(false)", "false");
    js_expect("String.prototype.trim.call(123)", "123");
    js_expect("String.prototype.charAt.call(123, 1)", "2");
    js_expect("String.prototype.indexOf.call({toString: function () { return 'abc'; }}, 'b')", "1");

    // --- toString and valueOf are not generic (22.1.3.28, 22.1.3.35) --------
    // thisStringValue, not ToString: a Number receiver is a TypeError, and it
    // has to be, since `''.concat({toString: String.prototype.toString})` is an
    // infinite regress if this coerces instead of refusing. That last case is
    // not asserted here: the throw happens inside context::to_primitive_string,
    // which reads the native's return value rather than checking for a pending
    // throw, so what a `catch` sees around it is the engine's unwinding and not
    // this method's contract.
    js_expect(throws("String.prototype.toString.call(1)"), "TypeError");
    js_expect(throws("String.prototype.toString.call(false)"), "TypeError");
    js_expect(throws("String.prototype.toString.call({})"), "TypeError");
    js_expect(throws("String.prototype.toString.call(['s'])"), "TypeError");
    js_expect(throws("String.prototype.valueOf.call(1)"), "TypeError");
    // String.prototype's own [[StringData]] is the empty String, which is why
    // this is "" and not a TypeError: the same rule that makes
    // `Number.prototype.toString()` answer "0".
    js_expect("String.prototype.toString()", "");
    js_expect("String.prototype.valueOf()", "");

    // --- argument coercion: observable, once, and in the specified order ----
    // Index arguments must use the re-entrant ToNumber: the static one cannot
    // run a user `valueOf`, so every object index would read NaN and become 0.
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
    // 22.1.3.9 steps 3 and 4: ToString(searchString) before
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

    // --- IsRegExp: includes/startsWith/endsWith refuse a pattern (7.2.8) ----
    // `'a/b'.includes(/b/)` searching for the characters of the pattern's
    // source is a common enough mistake that the specification throws
    // TypeError rather than stringifying the pattern.
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

    // --- trim over the specified whitespace set (12.2 + 12.3) ---------------
    // The whitespace set is the ASCII six plus fourteen non-ASCII code points
    // (U+00A0, U+FEFF, the Zs category and U+2028/U+2029).
    js_expect("\"\\u00a0x\\u00a0\".trim()", "x");
    js_expect("\"\\ufeffx\\ufeff\".trim()", "x");
    js_expect("\"\\u2028\\u2029x\\u3000\".trim()", "x");
    js_expect("\"\\u2000\\u200ax\".trimStart()", "x");
    js_expect("\"x\\u205f\\u202f\".trimEnd()", "x");
    js_expect("\"\\u1680\\u2003x\".trim()", "x");
    js_expect("\"\\u00a0\\u2000\\ufeff\".trim().length", "0");
    // U+180E was a space separator in Unicode 6.2 and stopped being one in 6.3,
    // so it survives untouched.
    js_expect("\"\\u180ex\\u180e\".trim().length", "3");
    // A byte inside a multi-byte sequence is not the character it resembles:
    // é's second UTF-8 byte is A9, beside U+00A0's A0, and the character must
    // not be trimmed away.
    js_expect("\"\\u00e9\".trim().length", "1");

    // --- match / search / matchAll take a pattern, not only a RegExp --------
    // 22.1.3.13, 22.1.3.21 and 22.1.3.14 each RegExpCreate their argument, so a
    // string or number is compiled as a pattern rather than answering "no
    // match" (null or -1), which `if (s.match(x))` would silently read as absent.
    js_expect("\"1234567890\".match(3)[0]", "3");
    js_expect("\"1234567890\".match(3).index", "2");
    js_expect("\"1234567890\".match(3).input", "1234567890");
    js_expect("\"abc\".match(\"b\")[0]", "b");
    js_expect("\"abc\".match(\"z\")", "null");
    js_expect("\"abc\".search(\"b\")", "1");
    js_expect("\"a2c\".search(2)", "1");
    js_expect("\"abc\".search(\"z\")", "-1");
    // matchAll builds its RegExp with `g`, so this finds three matches rather
    // than the first one forever, and it returns an iterator (22.1.3.14 step 5).
    js_expect("[...\"aaa\".matchAll(\"a\")].length", "3");
    js_expect("[...\"abc\".matchAll(\"z\")].length", "0");
    // ...and refuses a RegExp that has no `g`, because the answer would be
    // wrong either way (22.1.3.14 step 2b).
    js_expect(throws("\"aaa\".matchAll(/a/)"), "TypeError");
    js_expect("[...\"aaa\".matchAll(/a/g)].length", "3");
    js_expect(throws("\"aaa\".replaceAll(/a/, \"b\")"), "TypeError");
    js_expect("\"aaa\".replaceAll(/a/g, \"b\")", "bbb");
    // 22.1.3.20 step order: the argument's @@replace and the global-flag
    // refusal come before ToString(this), so a receiver whose toString throws
    // never runs it when the argument answers or is refused.
    // (js_expect takes an expression, hence the IIFEs.)
    js_expect("(function () { var poison = { toString() { throw 'no'; } }; var r; try {"
              " ''.replaceAll.call(poison, /a/, 'b'); } catch (e) { r = e instanceof TypeError; }"
              " return r; })()",
              "true");
    js_expect("(function () { var poison = { toString() { throw 'no'; } };"
              " return ''.replace.call(poison, { [Symbol.replace]() { return 'ok'; } }, 'b'); })()",
              "ok");
    js_expect("(function () { var poison = { toString() { throw 'no'; } }; var r; try {"
              " ''.split.call(poison, 'x'); } catch (e) { r = e; } return r; })()",
              "no");

    // --- normalize checks its form even though it normalises nothing --------
    // normalize() returns its input unchanged: this engine has no Unicode
    // normalization tables. It still validates the form argument and throws
    // RangeError for an unknown one (22.1.3.15 step 4), which is how a page that
    // wrote "NFKC1" finds its typo.
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

    // String.fromCharCode wraps each argument with ToUint16.
    js_expect("String.fromCharCode(-1, 65537, 4294967361) === '\\uffff\\u0001A'", "true");
    js_expect("String.fromCharCode(NaN, Infinity, -Infinity) === '\\u0000\\u0000\\u0000'", "true");
    // A direct numeric TypeError must stop before converting the next argument.
    for (const char * bad :
         {"1n", "Object(1n)", "Object(Symbol())", "{valueOf() { return Symbol(); }}",
          "{[Symbol.toPrimitive]() { return {}; }}"}) {
        js_expect("(function () { var count = 0; var later = {valueOf() { count++;"
                  " throw 'late'; }}; try { String.fromCharCode(65, " +
                      std::string{bad} +
                      ", later); } catch (e) { return e instanceof TypeError && count === 0; }"
                      " return false; })()",
                  "true");
    }
    js_expect(R"((function () {
        var error = {}, count = 0;
        var bad = {valueOf() { throw error; }};
        var later = {valueOf() { count++; return 66; }};
        try { String.fromCharCode.call(null, 65, bad, later); }
        catch (e) { return e === error && count === 0; }
        return false;
    })())",
              "true");
    js_expect(R"(String.fromCharCode({valueOf() {
        try { throw 0; } catch (e) {}
        return 65;
    }}, 66))",
              "AB");

    // String statics stop at the first failed conversion, preserving its exception.
    js_expect(throws("String.fromCodePoint(Symbol())"), "TypeError");
    js_expect(throws("String.fromCodePoint(Object(Symbol()))"), "TypeError");
    js_expect(throws("String.fromCodePoint(1n)"), "TypeError");
    js_expect(R"(String.fromCodePoint({valueOf() {
        try { throw 0; } catch (e) {}
        return 65;
    }}))",
              "A");
    js_expect(R"((function () {
        var error = {}, log = '';
        var first = {valueOf() { log += 'a'; throw error; }};
        var later = {valueOf() { log += 'b'; return 66; }};
        try { String.fromCodePoint(65, first, later); }
        catch (e) { return e === error && log === 'a'; }
        return false;
    })())",
              "true");
    js_expect(throws("String.raw(null)"), "TypeError");
    js_expect(throws("String.raw(undefined)"), "TypeError");
    js_expect(throws("String.raw({raw: {length: Symbol()}})"), "TypeError");
    js_expect(throws("String.raw({raw: [Symbol()]})"), "TypeError");
    js_expect(throws("String.raw({raw: ['a', 'b']}, Symbol())"), "TypeError");
    js_expect("String.raw({raw: 'ab'}, 7)", "a7b");
    js_expect("String.raw({raw: []}, Symbol())", "");
    js_expect("String.raw({raw: ['a']}, Symbol())", "a");
    js_expect(R"(String.raw({get raw() {
        try { throw 0; } catch (e) {}
        return {length: 2, 0: 'a', 1: 'b'};
    }}, {toString() {
        try { throw 0; } catch (e) {}
        return 's';
    }}))",
              "asb");
    js_expect(R"((function () {
        var error = {}, log = '';
        var raw = {
            length: 2,
            0: {toString() { log += 'a'; throw error; }},
            get 1() { log += 'b'; return 'b'; }
        };
        var substitution = {toString() { log += 's'; return 's'; }};
        try { String.raw({raw: raw}, substitution); }
        catch (e) { return e === error && log === 'a'; }
        return false;
    })())",
              "true");
    js_expect(R"((function () {
        var error = {}, log = '';
        var raw = {length: 2, 0: 'a', get 1() { log += 'b'; return 'b'; }};
        var substitution = {toString() { log += 's'; throw error; }};
        try { String.raw({raw: raw}, substitution); }
        catch (e) { return e === error && log === 's'; }
        return false;
    })())",
              "true");

    // ToString rejects Symbol after ToPrimitive, including boxed Symbols and callbacks.
    for (const char * expression :
         {"String(Object(Symbol()))", "'abc'.indexOf(Object(Symbol()))",
          "'abc'.indexOf({toString() { return Symbol(); }})",
          "'abc'.indexOf({[Symbol.toPrimitive]() { return Symbol(); }})",
          "'a'.replaceAll('a', {toString() { return Symbol(); }})",
          "'a'.replaceAll('a', function () { return Object(Symbol()); })",
          "'a'.normalize(Object(Symbol()))", "'a'.split(Object(Symbol()), 0)",
          "String.raw({raw: [Object(Symbol())]})", "encodeURIComponent(Object(Symbol()))"}) {
        js_expect(throws(expression), "TypeError");
    }
    js_expect("String(Symbol('key'))", "Symbol(key)");
    js_expect(R"((function () {
        var sym = Symbol('key'), obj = {}, key = {
            [Symbol.toPrimitive](hint) { return hint === 'string' ? sym : 'wrong'; }
        };
        obj[key] = 17;
        return obj[sym] === 17 && Reflect.get(obj, key) === 17;
    })())",
              "true");
    js_expect(R"((function () {
        var a = [1, 2];
        a.toString = function () { return 'custom'; };
        return String(a) + ':' + ''.concat(a);
    })())",
              "custom:custom");
    js_expect(R"((function () {
        var log = '', object = {
            [Symbol.toPrimitive](hint) { log += hint; return 42; },
            toString() { throw 'wrong conversion'; }
        };
        return ''.concat(object) + ':' + log;
    })())",
              "42:string");
    // An early conversion failure must not execute later user code or replace its error.
    for (const char * expression :
         {"'abc'.indexOf(bad, later)", "'abc'.lastIndexOf(bad, later)",
          "'abc'.includes(bad, later)", "'abc'.startsWith(bad, later)",
          "'abc'.endsWith(bad, later)", "''.concat(bad, later)",
          "''.replaceAll.call(bad, later, later)", "''.replaceAll(bad, later)"}) {
        js_expect("(function () { var count = 0; var bad = {toString() { return Symbol(); }};"
                  " var later = {toString() { count++; throw 'late'; },"
                  " valueOf() { count++; throw 'late'; }}; try {" +
                      std::string{expression} +
                      "; } catch (e) { return e instanceof TypeError && count === 0; }"
                      " return false; })()",
                  "true");
    }
    js_expect(R"((function () {
        var error = {}, count = 0;
        try {
            'aa'.replaceAll('a', function () {
                count++;
                return {toString() { throw error; }};
            });
        } catch (e) { return e === error && count === 1; }
        return false;
    })())",
              "true");
    // An exception caught inside the conversion is a successful return.
    js_expect(R"((function () {
        function text(s) {
            return {get [Symbol.toPrimitive]() {
                try { throw 0; } catch (e) {}
                return function (hint) {
                    try { throw 0; } catch (e) {}
                    return s;
                };
            }};
        }
        return String.prototype.replaceAll.call(text('aba'), text('a'), function () {
            try { throw 0; } catch (e) {}
            return text('x');
        }) + ':' + 'abc'.indexOf(text('b'));
    })())",
              "xbx:1");

    js_expect(R"('abc'.replaceAll({
        [Symbol.match]: true,
        get flags() {
            try { throw 0; } catch (e) {}
            return {toString() {
                try { throw 0; } catch (e) {}
                return 'g';
            }};
        },
        [Symbol.replace]() { return 'ok'; }
    }, 'x'))",
              "ok");
    js_expect(throws("'a'.replaceAll({[Symbol.match]: true, flags: Object(Symbol())}, 'x')"),
              "TypeError");
    // A callback's temporary result survives its own conversion getters and calls.
    {
        using namespace ctbrowser::script;
        const program prog = compiler::compile(R"(
            return 'aa'.replaceAll('a', function () {
                return {text: 'x', get [Symbol.toPrimitive]() {
                    return function () { return this.text; };
                }};
            });
        )");
        context cx;
        install_builtins(cx);
        cx.set_gc_stress(true);
        const run_result result = cx.run(prog);
        CHECK(result.ok);
        CHECK_EQ(cx.to_string(result.returned), std::string{"xx"});
        CHECK(cx.collections() > 0);
    }

    // charCodeAt's position: ToIntegerOrInfinity, NaN out of range, and the
    // receiver converted before the position (22.1.3.2 steps 2-3).
    js_expect(R"([NaN, undefined, -0.9, 1.9, -1, Infinity, -Infinity, 9007199254740991]
        .map(function (i) { return 'zA'.charCodeAt(i); }).join())",
              "122,122,122,65,NaN,NaN,NaN,NaN");
    js_expect("''.charCodeAt()", "NaN");
    // charAt and charCodeAt describe the same character at every index,
    // whatever the representation: a string walked with both (as the WPT
    // results reporter's quote() does) must not see two different strings.
    js_expect(R"((function () {
        var s = 'a\u00e9\u20ac\ud83d\ude00';
        for (var i = 0; i < s.length; i++) {
            if (s.charAt(i).charCodeAt(0) !== s.charCodeAt(i)) { return i; }
        }
        return 'agree';
    })())",
              "agree");
    js_expect(R"((function () {
        var log = '';
        var text = {toString() { log += 's'; return 'zA'; }};
        var index = {valueOf() { log += 'i'; return 1; }};
        return String.prototype.charCodeAt.call(text, index) + ':' + log;
    })())",
              "65:si");
    for (const char * index :
         {"Symbol()", "1n", "Object(Symbol())", "{valueOf() { return Symbol(); }}"}) {
        js_expect(throws("'x'.charCodeAt(" + std::string{index} + ")"), "TypeError");
    }
    js_expect(R"((function () {
        var error = {};
        try { 'z'.charCodeAt({valueOf() { throw error; }}); }
        catch (e) { return e === error; }
        return false;
    })())",
              "true");

    // --- UTF-16 code units --------------------------------------------------
    // Lengths, indices and RegExp positions count UTF-16 code units (strings
    // are WTF-8 storage behind a UTF-16 view, script/utf16.hpp). Expected values
    // are V8's.
    js_expect("\"e\\u0301\".length", "2"); // A combining mark is one unit.
    js_expect("\"\\u00e9\".length", "1");
    js_expect("\"\\u00e9\".charCodeAt(0)", "233");
    js_expect("\"\\ud83d\\ude00\".length", "2"); // A surrogate pair.
    js_expect("\"\\ud83d\\ude00\".charCodeAt(0)", "55357");
    js_expect("\"\\ud83d\\ude00\".charCodeAt(1)", "56832");
    js_expect("\"\\ud83d\\ude00\".codePointAt(0)", "128512");
    js_expect("\"\\ud83d\\ude00\".codePointAt(1)", "56832"); // The trail alone.
    js_expect("\"\\ud83d\\ude00\".split(\"\").length", "2");
    js_expect("[...\"\\ud83d\\ude00\"].length", "1"); // Iteration is by code point.
    js_expect("Array.from(\"a\\u{1F600}b\").length", "3");
    js_expect("\"\\u00e9\".slice(0, 1) === \"\\u00e9\"", "true");
    js_expect("\"a\\u00e9b\".substring(1, 2) === \"\\u00e9\"", "true");
    js_expect("\"a\\u00e9b\".charAt(2)", "b");
    js_expect("\"a\\u00e9b\"[1] === \"\\u00e9\"", "true");
    js_expect("\"abc\"[\"1\"]", "b"); // An index spelled as a string.
    js_expect("\"\\u00e9b\".indexOf(\"b\")", "1");
    js_expect("\"b\\u00e9b\".lastIndexOf(\"b\")", "2");
    js_expect("\"\\u00e9\\u00e9x\".startsWith(\"x\", 2)", "true");
    js_expect("\"x\\u00e9\".endsWith(\"x\", 1)", "true");
    js_expect("\"\\u00e9-\\u00e9\".split(\"-\").join(\"|\") === \"\\u00e9|\\u00e9\"", "true");
    // A slice through a pair leaves each half as its lone surrogate, and the
    // halves join again when they meet.
    js_expect("\"\\u{1F600}\".slice(0, 1) === \"\\ud83d\"", "true");
    js_expect("\"\\u{1F600}\".at(-1) === \"\\ude00\"", "true");
    js_expect("\"\\u{1F600}\".slice(0, 1) + \"\\u{1F600}\".slice(1) === \"\\u{1F600}\"", "true");
    js_expect("\"\\u{1F600}\".indexOf(\"\\ude00\")", "1");
    js_expect("\"abc\".padEnd(6, \"\\u{1F4A9}\") === \"abc\\u{1F4A9}\\ud83d\"", "true");
    js_expect("String.fromCharCode(0xFFFF).charCodeAt(0)", "65535");
    js_expect("String.fromCharCode(0xD83D, 0xDE00).length", "2");
    js_expect("Object.keys(\"\\u00e9x\").join()", "0,1");
    // Code unit order, which is not code point order: a pair sorts below
    // U+FFFD because its first unit is D83D.
    js_expect("\"\\u{1F600}\" < \"\\ufffd\"", "true");
    js_expect("[\"\\ufffd\", \"\\u{1F600}\"].sort()[0] === \"\\u{1F600}\"", "true");
    // RegExp positions are units too.
    js_expect("/b/.exec(\"\\u00e9b\").index", "1");
    js_expect("(function(){var r=/b/g; r.exec(\"\\u00e9b\"); return r.lastIndex;})()", "2");
    js_expect("\"\\u00e9b\".search(/b/)", "1");
    js_expect("\"\\u00e9\\u00e9\".replace(/\\u00e9/g, function(m, i){return i;})", "01");
    js_expect("\"\\u00e9a\\u00e9a\".split(/a/).length", "3");
    js_expect("/(?<x>b)/d.exec(\"\\u{1F600}b\").indices[0].join()", "2,3");
    // A binary string is code units 0-255, one per byte.
    js_expect("btoa(String.fromCharCode(233))", "6Q==");
    js_expect("atob(\"6Q==\").charCodeAt(0)", "233");
    js_expect("atob(\"6Q==\").length", "1");
    js_expect("btoa(\"\\u0100\")", "THREW");
    js_expect("atob(\"a\")", "THREW"); // A length leaving a remainder of 1.

    // --- Unicode's default case conversion ----------------------------------
    // SpecialCasing's unconditional entries, Final_Sigma, and the supplementary
    // planes, as V8 answers them.
    js_expect("\"Stra\\u00dfe\".toUpperCase()", "STRASSE");
    js_expect("\"\\u0130\".toLowerCase()", "i\u0307");
    js_expect("\"\\u03a3\\u0391\\u03a3\".toLowerCase()", "\u03c3\u03b1\u03c2");
    js_expect("\"\\ud801\\udc00\".toLowerCase() === \"\\ud801\\udc28\"", "true");
    js_expect("\"\\u00e9t\\u00e9\".toLocaleUpperCase()", "\u00c9T\u00c9");

    // --- the well-known-symbol protocol: match/replace/search/split/matchAll
    // ask their argument first (22.1.3.13 step 2 and its siblings) --------
    js_expect("'abc'.match({[Symbol.match](s){return 'got:'+s;}})", "got:abc");
    js_expect("'abc'.replace({[Symbol.replace](s, r){return s+'/'+r;}}, 'x')", "abc/x");
    js_expect("'abc'.search({[Symbol.search](){return 7;}})", "7");
    js_expect("'abc'.split({[Symbol.split](s, l){return [s, l];}}, 2).join()", "abc,2");
    js_expect("'abc'.matchAll({[Symbol.matchAll](s){return s.length;}})", "3");
    js_expect("'abc'.match({get [Symbol.match]() { throw new Error('poison'); }})", "THREW");
    js_expect("'abc'.match({[Symbol.match]: 1})", "THREW"); // GetMethod: not callable.
    // GetMethod: null is absent, so RegExpCreate(ToString(obj)) runs; a class
    // of the characters in "[object Object]" matches the b.
    js_expect("'abc'.match({[Symbol.match]: null})[0]", "b");
    js_expect("'abc'.match(/b/)[0]", "b"); // A real RegExp is unchanged.
    js_expect("'a-b'.split('-').length", "2");

    // --- 19.2.6 the URI functions (node's answers) ---------------------------
    js_expect("encodeURIComponent('a b&c/d?é')", "a%20b%26c%2Fd%3F%C3%A9");
    js_expect("encodeURI('http://x/a b?q=1&r=é#f')", "http://x/a%20b?q=1&r=%C3%A9#f");
    js_expect("encodeURIComponent(\"-_.!~*'()\")", "-_.!~*'()");
    js_expect("decodeURIComponent('a%20b%26c%2F%C3%A9')", "a b&c/é");
    js_expect("decodeURI('a%20b%26c%2F%C3%A9')", "a b%26c%2Fé");
    js_expect("decodeURIComponent('%E2%82%AC')", "€");
    js_expect("decodeURIComponent('%')", "THREW");
    js_expect("decodeURIComponent('%zz')", "THREW");
    js_expect("decodeURIComponent('%C0%80')", "THREW");    // Overlong.
    js_expect("decodeURIComponent('%ED%A0%80')", "THREW"); // A surrogate.
    js_expect("decodeURIComponent('%E2%82')", "THREW");    // Truncated.
    js_expect("encodeURIComponent()", "undefined");
    js_expect("[encodeURI.length, decodeURIComponent.name].join()", "1,decodeURIComponent");

    // --- Annex B.2.1 escape/unescape and B.2.2 the String extras -------------
    js_expect("escape('a b+@\\u00e9\\u{10401}')", "a%20b+@%E9%uD801%uDC01");
    js_expect("unescape('%u0041%41%%41%uD801%uDC01%zz%u12')", "AA%A\U00010401%zz%u12");
    js_expect("unescape(escape('\\u{1F600}x')) === '\\u{1F600}x'", "true");
    js_expect("'<'.anchor('\"a\"') + '_'.bold() + 'x'.link(1)",
              "<a name=\"&quot;a&quot;\"><</a><b>_</b><a href=\"1\">x</a>");
    js_expect("String.prototype.trimLeft === String.prototype.trimStart && "
              "String.prototype.trimRight.name",
              "trimEnd");

    // A template reads a raw CRLF or CR as LF, cooked and raw alike; a
    // backslash before U+2028 is a line continuation in a string too.
    js_expect("`a\r\nb\rc` === 'a\\nb\\nc'", "true");
    js_expect("(function (s) { return s.raw[0] === 'a\\nb'; })`a\r\nb`", "true");
    js_expect("'a\\ b'", "ab");

    REPORT("string_basics");
}
