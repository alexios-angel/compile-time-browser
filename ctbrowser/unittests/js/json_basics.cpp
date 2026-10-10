//===- ctbrowser/unittests/js/json_basics.cpp - JSON against V8 -----------===//
//
// JSON.parse and JSON.stringify (ECMA-262 25.5), with expected values taken
// from node. Covers replacer, space, toJSON, reviver, cycle detection, the
// strict number and whitespace grammar, and bounded nesting depth.
//
// This file is the regression net, not test262: that suite is opt-in
// (-DCTBROWSER_TEST262=ON), needs a 273 MB corpus kept outside the repository,
// and takes minutes.
//
// One deviation is asserted as it is rather than as the specification has it,
// because it belongs to this engine's value model rather than to JSON: an
// array is walked to `items.size()` and not to its `length` property, the same
// deviation every array built-in has (see array_object::sparse).
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

/// Builds an expression answering `e.name` for whatever `source` threw, or "no"
/// when it did not throw. The negative cases below assert which error, which
/// js_expect's flattened "THREW" cannot say.
[[nodiscard]] static std::string threw(std::string_view source) {
    return "(function () { try { " + std::string{source} +
           "; return 'no'; } catch (e) { return e.name; } })()";
}

int main() {
    // ================================================================
    // 1. JSON.stringify - the shapes that always worked, first
    // ================================================================
    js_expect("JSON.stringify({a: 1, b: 'x'})", "{\"a\":1,\"b\":\"x\"}");
    // Well-formed JSON.stringify (25.5.2.3 step 2.b): a lone surrogate is
    // escaped, a pair is kept as the character.
    js_expect("JSON.stringify('\\uD834\\uDF06\\uD834')", "\"\xF0\x9D\x8C\x86\\ud834\"");
    js_expect("JSON.stringify('\\uDF06')", "\"\\udf06\"");
    // 25.5.1 step 1: ToString of the text, so a Symbol is the TypeError.
    js_expect("(function () { try { JSON.parse(Symbol()); } catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("JSON.parse(null)", "null");
    js_expect("JSON.stringify([1, 2, 3])", "[1,2,3]");
    js_expect("JSON.stringify([])", "[]");
    js_expect("JSON.stringify({})", "{}");
    js_expect("JSON.stringify(null)", "null");
    js_expect("JSON.stringify(true)", "true");
    js_expect("JSON.stringify('a')", "\"a\"");
    js_expect("JSON.stringify(1.5)", "1.5");
    // 25.5.2 step 12: an unserialisable value at the top level is undefined,
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
    // \b and \f use their short escapes, not \u0008 and \u000c.
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
    // It is handed the key it is being serialised under, which is what lets one
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
    // Returning undefined omits the member.
    js_expect("JSON.stringify({a: 1, b: 2},"
              " function (k, v) { return k === 'a' ? undefined : v; })",
              "{\"b\":2}");
    // Its receiver is the holder, so a replacer can see which object a value
    // came out of.
    js_expect("JSON.stringify({a: 1}, function (k, v) { return k === 'a' ? this.a + 10 : v; })",
              "{\"a\":11}");
    // A replacer array is a property list, applied to objects and not to
    // arrays, keeping its own order rather than the object's.
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
    // An empty object or array is written flat even with a gap: there are no
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
    // 5. The cycle check - a TypeError, not unbounded recursion
    // ================================================================
    js_expect(threw("var a = []; a[0] = a; JSON.stringify(a)"), "TypeError");
    js_expect(threw("var o = {}; o.self = o; JSON.stringify(o)"), "TypeError");
    js_expect(threw("var o = {a: {}}; o.a.up = o; JSON.stringify(o)"), "TypeError");
    // The same object twice is not a cycle. The stack holds the ancestors of
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
    // A surrogate pair escape is one code point: two UTF-16 units.
    js_expect("JSON.parse('[\"\\\\ud83d\\\\ude00\"]')[0].length", "2");
    // A lone surrogate stays one: its own unit, kept as WTF-8.
    js_expect("JSON.parse('[\"\\\\ud800\"]')[0].length", "1");
    js_expect("JSON.parse('[\"\\\\ud800\"]')[0].charCodeAt(0)", "55296");
    // Round-tripping is the property that matters most, and it holds.
    js_expect("JSON.stringify(JSON.parse('{\"a\":[1,{\"b\":null}],\"c\":\"x\"}'))",
              "{\"a\":[1,{\"b\":null}],\"c\":\"x\"}");

    // ================================================================
    // 7. JSON.parse - what it refuses, and with which error
    // ================================================================
    js_expect(threw("JSON.parse('')"), "SyntaxError");
    js_expect(threw("JSON.parse('{')"), "SyntaxError");
    js_expect(threw("JSON.parse('[1,2')"), "SyntaxError");
    js_expect(threw("JSON.parse('[1,]')"), "SyntaxError");
    js_expect(threw("JSON.parse('{\"a\":1,}')"), "SyntaxError");
    js_expect(threw("JSON.parse('{a:1}')"), "SyntaxError");
    js_expect(threw("JSON.parse(\"{'a':1}\")"), "SyntaxError");
    // Trailing content after the document.
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
    // Returning undefined deletes the property, which is how one filters.
    js_expect("JSON.stringify(JSON.parse('{\"a\":1,\"b\":2}',"
              " function (k, v) { return k === 'a' ? undefined : v; }))",
              "{\"b\":2}");
    // The walk is post-order: a child is revived and written back before its
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
    // ...and the walk is the ordinary object operations, so a Proxy a reviver
    // grafts in sees its traps: ownKeys, get, defineProperty, deleteProperty.
    js_expect("(function(){ var seen = []; JSON.parse('[0,0]', function (k, v) {"
              "  if (k === '0') { this[1] = new Proxy({a: 1}, {ownKeys: function (t) {"
              "    seen.push('keys'); return Reflect.ownKeys(t); }}); }"
              "  return v; }); return seen.join(); })()",
              "keys");
    js_expect("JSON.parse('[0,0]', function (k, v) { if (k === '0') { this[1] = new Proxy({}, "
              "{ownKeys: function () { throw new RangeError('x'); }}); } return v; })",
              "THREW");
    js_expect("JSON.parse('{\"a\":{\"b\":1}}', function (k, v) { if (k === 'b') { return "
              "undefined; } return v; }).a.hasOwnProperty('b')",
              "false");
    js_expect("JSON.parse('[1,2,3]', function (k, v) { return k === '1' ? undefined : v; })"
              ".length",
              "3");
    // The reviver's third argument carries a primitive's exact source text
    // (JSON.parse source text access), and none once the value was replaced.
    js_expect("(function(){ var log = []; JSON.parse('[1.0, \"a\", {\"b\": true}]',"
              " function (k, v, c) { log.push(k + ':' + c.source); return v; });"
              " return log.join('|'); })()",
              "0:1.0|1:\"a\"|b:true|2:undefined|:undefined");
    js_expect("(function(){ var log = []; JSON.parse('[1, 2]', function (k, v, c) {"
              " if (k === '0') { this[1] = 3; } log.push(k + ':' + c.source); return this[k]; });"
              " return log.join('|'); })()",
              "0:1|1:undefined|:undefined");
    // A refused [[Delete]] is ignored, not a TypeError (step 2.b.iii.2.a).
    js_expect("JSON.parse('[1,2]', function (k, v) { if (k === '0') { Object.defineProperty("
              "this, '1', {configurable: false}); } return k === '1' ? undefined : v; })[1]",
              "2");

    // --- JSON.rawJSON / JSON.isRawJSON (ES2025) ------------------------------
    js_expect("JSON.stringify({n: JSON.rawJSON('12345678901234567890')})",
              "{\"n\":12345678901234567890}");
    js_expect("JSON.stringify([JSON.rawJSON('\"x\"'), JSON.rawJSON('null')])", "[\"x\",null]");
    js_expect("(function(){var r=JSON.rawJSON('1');return [JSON.isRawJSON(r), JSON.isRawJSON({}),"
              "Object.isFrozen(r), String(Object.getPrototypeOf(r)), r.rawJSON].join();})()",
              "true,false,true,null,1");
    js_expect("JSON.rawJSON('{}')", "THREW");
    js_expect("JSON.rawJSON(' 1')", "THREW");
    js_expect("JSON.rawJSON('')", "THREW");
    js_expect("JSON.rawJSON('tru')", "THREW");

    // 25.5.2 steps 4-8 and 10: a replacer list through Get (a proxy of an
    // array), String/Number objects in it ToString'd, a Number/String object
    // as space unwrapped, a proxy of an array serialised as an array, a proxy
    // of an object through its traps, and an abrupt completion propagated.
    js_expect("JSON.stringify({a: 1, b: 2, c: 3}, new Proxy(['a', 'c'], {}))", "{\"a\":1,\"c\":3}");
    js_expect("JSON.stringify({a: 1, 1: 2}, [new String('a'), new Number(1)])",
              "{\"a\":1,\"1\":2}");
    js_expect("JSON.stringify([1], null, new Number(2))", "[\n  1\n]");
    js_expect("JSON.stringify([1], null, new String('--'))", "[\n--1\n]");
    js_expect("JSON.stringify(new Proxy([1, 2], {}))", "[1,2]");
    js_expect("JSON.stringify(new Proxy({x: 1}, {}))", "{\"x\":1}");
    js_expect("(function () { try { JSON.stringify({}, [{ toString() { throw new RangeError('r');"
              " } }]); return 'no'; } catch (e) { return e.name; } })()",
              "no");

    // Deep nesting is capped: past the limit, each path fails with a catchable
    // error instead of overflowing the native stack and aborting the process.
    js_expect("(function () { var s = '['.repeat(50000) + ']'.repeat(50000);"
              " try { JSON.parse(s); return 'no throw'; } catch (e) { return e.name; } })()",
              "SyntaxError");
    js_expect("(function () { var a = []; for (var i = 0; i < 50000; i++) { a = [a]; }"
              " try { JSON.stringify(a); return 'no throw'; } catch (e) { return e.name; } })()",
              "RangeError");
    js_expect("(function () { var a = []; for (var i = 0; i < 50000; i++) { a = [a]; }"
              " try { structuredClone(a); return 'no throw'; } catch (e) { return e.name; } })()",
              "DataCloneError");
    // ...and a document at the legal depth still round-trips.
    js_expect("JSON.parse('[[[[[1]]]]]')[0][0][0][0][0]", "1");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
