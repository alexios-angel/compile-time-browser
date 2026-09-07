// JSON, against V8 - the whole of 25.5 rather than the half of it that was here.
//
// `JSON.stringify` was one free function of (value, string &). It had no
// replacer, no `space`, no `toJSON`, and - the one that is not a missing
// feature but a crash - no stack, so `a = []; a[0] = a; JSON.stringify(a)`
// recursed until the C++ stack ran out. `JSON.parse` accepted `+1`, `01`, `1.`,
// `.5`, a raw control character inside a string and anything at all AFTER the
// document, and answered `undefined` for source it could not read instead of
// throwing the SyntaxError every page's try/catch is written for.
//
// THIS FILE IS THE REGRESSION NET, not test262: that suite is opt-in
// (-DCTBROWSER_TEST262=ON), needs a 273 MB corpus that is deliberately not in
// the repository, and takes five minutes. The expected values are node's.
//
// Two deviations are asserted as they ARE rather than as the specification has
// them, because each belongs to this engine's value model rather than to JSON:
//
//   * a string is UTF-8 BYTES, so `.length` counts bytes and a lone surrogate
//     cannot be represented - `"\ud800"` parses to U+FFFD. A surrogate PAIR is
//     joined into one code point, which is the half that was wrong.
//   * an array is walked to `items.size()` and not to its `length` property,
//     the same deviation every array built-in has (see array_object::sparse).

#include "js_expect.hpp"

// `e.name` for whatever the expression threw, or "no" when it did not throw at
// all. Every negative case below is one of these: what is being asserted is
// WHICH error, and "THREW" from js_expect cannot say.
[[nodiscard]] static std::string threw(std::string_view source) {
    return "(function () { try { " + std::string{source} +
           "; return 'no'; } catch (e) { return e.name; } })()";
}

int main() {
    // ================================================================
    // 1. JSON.stringify - the shapes that always worked, first
    // ================================================================
    js_expect("JSON.stringify({a: 1, b: 'x'})", "{\"a\":1,\"b\":\"x\"}");
    js_expect("JSON.stringify([1, 2, 3])", "[1,2,3]");
    js_expect("JSON.stringify([])", "[]");
    js_expect("JSON.stringify({})", "{}");
    js_expect("JSON.stringify(null)", "null");
    js_expect("JSON.stringify(true)", "true");
    js_expect("JSON.stringify('a')", "\"a\"");
    js_expect("JSON.stringify(1.5)", "1.5");
    // 25.5.2 step 12: an unserialisable value at the TOP LEVEL is undefined,
    // and inside an array it is null. The two are different answers on purpose.
    js_expect("JSON.stringify(undefined)", "undefined");
    js_expect("JSON.stringify(function () {})", "undefined");
    js_expect("JSON.stringify([undefined, function () {}])", "[null,null]");
    js_expect("JSON.stringify({a: undefined, b: 1})", "{\"b\":1}");
    // Non-finite numbers are null - they are not JSON, and writing the bare
    // words produces output no parser will read back.
    js_expect("JSON.stringify(NaN)", "null");
    js_expect("JSON.stringify([Infinity, -Infinity])", "[null,null]");
    // -0 serialises as "0"; there is no "-0" in JSON.
    js_expect("JSON.stringify(-0)", "0");
    js_expect("JSON.stringify([-0])", "[0]");

    // --- QuoteJSONString, 25.5.2.3 ------------------------------------------
    // \b and \f were the two rows of the table that were missing: both came out
    // as the six-character \u0008 and \u000c forms.
    js_expect("JSON.stringify(String.fromCharCode(8))", "\"\\b\"");
    js_expect("JSON.stringify(String.fromCharCode(12))", "\"\\f\"");
    js_expect("JSON.stringify(String.fromCharCode(10))", "\"\\n\"");
    js_expect("JSON.stringify(String.fromCharCode(13))", "\"\\r\"");
    js_expect("JSON.stringify(String.fromCharCode(9))", "\"\\t\"");
    js_expect("JSON.stringify(String.fromCharCode(0))", "\"\\u0000\"");
    js_expect("JSON.stringify(String.fromCharCode(31))", "\"\\u001f\"");
    js_expect("JSON.stringify('a\"b')", "\"a\\\"b\"");

    // ================================================================
    // 2. toJSON - 25.5.2.2 steps 2 and 3
    // ================================================================
    js_expect("JSON.stringify({toJSON: function () { return 42; }})", "42");
    js_expect("JSON.stringify({a: {toJSON: function () { return [1]; }}})", "{\"a\":[1]}");
    // It is handed the KEY it is being serialised under, which is what lets one
    // toJSON answer differently in two places.
    js_expect("JSON.stringify({a: {toJSON: function (k) { return k; }}})", "{\"a\":\"a\"}");
    js_expect("JSON.stringify([{toJSON: function (k) { return k; }}])", "[\"0\"]");
    // The top level's key is the empty string, because the value is serialised
    // as a member of a wrapper object.
    js_expect("JSON.stringify({toJSON: function (k) { return '[' + k + ']'; }})", "\"[]\"");
    // A `toJSON` that is not callable is ignored rather than being an error.
    js_expect("JSON.stringify({toJSON: 5, a: 1})", "{\"toJSON\":5,\"a\":1}");

    // ================================================================
    // 3. The replacer - a function, and an array
    // ================================================================
    js_expect("JSON.stringify({a: 1, b: 2},"
              " function (k, v) { return typeof v === 'number' ? v * 2 : v; })",
              "{\"a\":2,\"b\":4}");
    // It sees the root under the key "" before it sees anything else.
    js_expect("JSON.stringify(1, function (k, v) { return k === '' ? v + 1 : v; })", "2");
    // Returning undefined OMITS the member.
    js_expect("JSON.stringify({a: 1, b: 2},"
              " function (k, v) { return k === 'a' ? undefined : v; })",
              "{\"b\":2}");
    // Its receiver is the HOLDER, so a replacer can see which object a value
    // came out of.
    js_expect("JSON.stringify({a: 1}, function (k, v) { return k === 'a' ? this.a + 10 : v; })",
              "{\"a\":11}");
    // A replacer ARRAY is a property list, applied to objects and not to
    // arrays, keeping ITS order rather than the object's.
    js_expect("JSON.stringify({a: 1, b: 2, c: 3}, ['b', 'a'])", "{\"b\":2,\"a\":1}");
    js_expect("JSON.stringify({a: 1, b: 2}, ['b'])", "{\"b\":2}");
    // A duplicate name appears once - the list is a set, in insertion order.
    js_expect("JSON.stringify({a: 1}, ['a', 'a'])", "{\"a\":1}");
    // A number in the list is ToString'd; anything else contributes nothing.
    js_expect("JSON.stringify({1: 'x', a: 'y'}, [1])", "{\"1\":\"x\"}");
    js_expect("JSON.stringify({a: 1}, [true, {}, null])", "{}");
    js_expect("JSON.stringify([1, 2], ['0'])", "[1,2]");
    // A name in the list that the object does not have is skipped.
    js_expect("JSON.stringify({a: 1}, ['a', 'zz'])", "{\"a\":1}");
    // A replacer that is neither callable nor an array is ignored.
    js_expect("JSON.stringify({a: 1}, 'nonsense')", "{\"a\":1}");
    js_expect("JSON.stringify({a: 1}, null)", "{\"a\":1}");

    // ================================================================
    // 4. `space` - 25.5.2 steps 5 through 8
    // ================================================================
    js_expect("JSON.stringify({a: 1}, null, 2)", "{\n  \"a\": 1\n}");
    js_expect("JSON.stringify([1, 2], null, 1)", "[\n 1,\n 2\n]");
    js_expect("JSON.stringify({a: {b: 1}}, null, 1)", "{\n \"a\": {\n  \"b\": 1\n }\n}");
    js_expect("JSON.stringify([[1]], null, 1)", "[\n [\n  1\n ]\n]");
    // An EMPTY object or array is written flat even with a gap: there are no
    // members, so there is nothing to put on a line.
    js_expect("JSON.stringify({a: {}, b: []}, null, 1)", "{\n \"a\": {},\n \"b\": []\n}");
    // A string gap is used as it is; a number gap is that many spaces.
    js_expect("JSON.stringify({a: 1}, null, '..')", "{\n..\"a\": 1\n}");
    // Ten is the ceiling for both forms, and a fractional count truncates.
    js_expect("JSON.stringify([1], null, 20) === JSON.stringify([1], null, 10)", "true");
    js_expect("JSON.stringify([1], null, 1.9) === JSON.stringify([1], null, 1)", "true");
    js_expect("JSON.stringify([1], null, 'abcdefghijklmno') ==="
              " JSON.stringify([1], null, 'abcdefghij')",
              "true");
    // Zero, a negative count and a non-string non-number are all no gap at all.
    js_expect("JSON.stringify({a: 1}, null, 0)", "{\"a\":1}");
    js_expect("JSON.stringify({a: 1}, null, -1)", "{\"a\":1}");
    js_expect("JSON.stringify({a: 1}, null, true)", "{\"a\":1}");
    js_expect("JSON.stringify({a: 1}, null, '')", "{\"a\":1}");

    // ================================================================
    // 5. The cycle check - a TypeError where there used to be a segfault
    // ================================================================
    js_expect(threw("var a = []; a[0] = a; JSON.stringify(a)"), "TypeError");
    js_expect(threw("var o = {}; o.self = o; JSON.stringify(o)"), "TypeError");
    js_expect(threw("var o = {a: {}}; o.a.up = o; JSON.stringify(o)"), "TypeError");
    // THE SAME OBJECT TWICE IS NOT A CYCLE. The stack holds the ancestors of
    // the value being written, not everything already seen, and a shared
    // subobject is perfectly serialisable.
    js_expect("(function () { var s = {v: 1}; return JSON.stringify([s, s]); })()",
              "[{\"v\":1},{\"v\":1}]");

    // ================================================================
    // 6. JSON.parse - what it reads
    // ================================================================
    js_expect("JSON.parse('{\"a\":1}').a", "1");
    js_expect("JSON.parse(' [1, 2] ')[1]", "2");
    js_expect("JSON.parse('null')", "null");
    js_expect("JSON.parse('true')", "true");
    js_expect("JSON.parse('-1.5e2')", "-150");
    js_expect("JSON.parse('\"a\"')", "a");
    js_expect_negative_zero("JSON.parse('-0')", true);
    // Every escape in the grammar, including the solidus that need not be one.
    js_expect("JSON.parse('[\"\\\\u0041\"]')[0]", "A");
    js_expect("JSON.parse('[\"\\\\/\"]')[0]", "/");
    js_expect("JSON.parse('[\"\\\\\\\\\"]')[0].length", "1");
    js_expect("JSON.parse('[\"\\\\b\"]')[0].charCodeAt(0)", "8");
    js_expect("JSON.parse('[\"\\\\f\"]')[0].charCodeAt(0)", "12");
    // A SURROGATE PAIR IS ONE CODE POINT: U+1F600 is four UTF-8 bytes, and a
    // string's `length` here counts bytes. Encoding the halves separately
    // produced six bytes of two replacement characters.
    js_expect("JSON.parse('[\"\\\\ud83d\\\\ude00\"]')[0].length", "4");
    // A LONE surrogate cannot be spelled in UTF-8 and becomes U+FFFD, which is
    // three bytes. This is the engine's answer, not V8's.
    js_expect("JSON.parse('[\"\\\\ud800\"]')[0].length", "3");
    // Round-tripping is the property that matters most, and it holds.
    js_expect("JSON.stringify(JSON.parse('{\"a\":[1,{\"b\":null}],\"c\":\"x\"}'))",
              "{\"a\":[1,{\"b\":null}],\"c\":\"x\"}");

    // ================================================================
    // 7. JSON.parse - what it REFUSES, and with which error
    // ================================================================
    js_expect(threw("JSON.parse('')"), "SyntaxError");
    js_expect(threw("JSON.parse('{')"), "SyntaxError");
    js_expect(threw("JSON.parse('[1,2')"), "SyntaxError");
    js_expect(threw("JSON.parse('[1,]')"), "SyntaxError");
    js_expect(threw("JSON.parse('{\"a\":1,}')"), "SyntaxError");
    js_expect(threw("JSON.parse('{a:1}')"), "SyntaxError");
    js_expect(threw("JSON.parse(\"{'a':1}\")"), "SyntaxError");
    // Trailing content, which this reader did not look for at all.
    js_expect(threw("JSON.parse('[1,2]junk')"), "SyntaxError");
    js_expect(threw("JSON.parse('1 2')"), "SyntaxError");
    // The number grammar: no leading plus, no leading zero, a digit either side
    // of the point, and a digit after the exponent marker.
    js_expect(threw("JSON.parse('+1')"), "SyntaxError");
    js_expect(threw("JSON.parse('01')"), "SyntaxError");
    js_expect(threw("JSON.parse('1.')"), "SyntaxError");
    js_expect(threw("JSON.parse('.5')"), "SyntaxError");
    js_expect(threw("JSON.parse('1e')"), "SyntaxError");
    js_expect(threw("JSON.parse('-')"), "SyntaxError");
    js_expect(threw("JSON.parse('Infinity')"), "SyntaxError");
    js_expect(threw("JSON.parse('undefined')"), "SyntaxError");
    // JSON whitespace is space, tab, CR and LF. A form feed is not one.
    js_expect(threw("JSON.parse(String.fromCharCode(12) + '1')"), "SyntaxError");
    js_expect("JSON.parse(String.fromCharCode(9) + '1')", "1");
    // A raw control character inside a string has to be escaped.
    js_expect(threw("JSON.parse('\"' + String.fromCharCode(10) + '\"')"), "SyntaxError");
    // An escape that is not in the grammar, and a short \\u.
    js_expect(threw("JSON.parse('[\"\\\\x41\"]')"), "SyntaxError");
    js_expect(threw("JSON.parse('[\"\\\\u00\"]')"), "SyntaxError");
    js_expect(threw("JSON.parse('[\"\\\\uZZZZ\"]')"), "SyntaxError");

    // ================================================================
    // 8. The reviver - InternalizeJSONProperty, 25.5.1.1
    // ================================================================
    js_expect("JSON.parse('{\"a\":1,\"b\":2}',"
              " function (k, v) { return typeof v === 'number' ? v * 10 : v; }).b",
              "20");
    // Returning undefined DELETES the property, which is how one filters.
    js_expect("JSON.stringify(JSON.parse('{\"a\":1,\"b\":2}',"
              " function (k, v) { return k === 'a' ? undefined : v; }))",
              "{\"b\":2}");
    // The walk is POST-ORDER: a child is revived and written back before its
    // parent is offered, so the parent sees the finished object.
    js_expect("JSON.parse('{\"a\":{\"b\":1}}',"
              " function (k, v) { return k === 'a' ? v.b : v; }).a",
              "1");
    // The root is offered last, under the empty key.
    js_expect("JSON.parse('1', function (k, v) { return k === '' ? v + 1 : v; })", "2");
    // The keys arrive as strings, including an array's indices.
    js_expect("JSON.parse('[7]', function (k, v) { return typeof k; })", "string");
    // A reviver that is not callable is ignored rather than being an error.
    js_expect("JSON.parse('{\"a\":1}', 5).a", "1");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
