//===- ctbrowser/unittests/js/vm/stdlib.cpp - VM standard library tests ---===//
//
// The standard library: Math, Array, String, RegExp, JSON, Date, the
// collections, typed arrays over a shared ArrayBuffer, eval, and the web
// globals btoa/atob and structuredClone. expect.hpp holds the shared
// assertions.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include "expect.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

using namespace ctbrowser::script;

namespace {

/// Regular expressions. The engine is a backtracking matcher ported from
/// ctjs, extended with what p5.js uses (lookahead, the sticky flag, named
/// groups) and, for test262's String tests, lookbehind and backreferences.
void test_regex() {
    // Invalid flags are an early error (a SyntaxError of the source, reported
    // as a parse error), not a throw when the line runs. So is a body that is
    // provably not a Pattern by inspection (an unclosed group, nothing to
    // repeat); anything the scan does not judge is left to run time
    // (lib/Script/compile/early_errors/regexp.cpp says what it judges).
    CHECK(!compiler::compile("var r = /a/gg;").ok);
    CHECK(compiler::compile("var r = /a/gg;").error.starts_with("parse error:"));
    CHECK(!compiler::compile("var r = /a/q;").ok);
    CHECK(compiler::compile("function f() { return /a(b+)c/gi; }").ok);
    CHECK(!compiler::compile("var r = /(/;").ok);
    CHECK(!compiler::compile("var r = /?/;").ok);
    CHECK(!compiler::compile("var r = /a{2,1}/;").ok);
    CHECK(!compiler::compile("var r = /(?<a>x)(?<a>y)/;").ok);
    CHECK(compiler::compile("var r = /(?<a>x)|(?<a>y)/;").ok);
    CHECK(compiler::compile("var r = /a{1/;").ok); // Annex B: a literal brace.
    CHECK(!compiler::compile("var r = /a{1/u;").ok);
    CHECK(compiler::compile("var r = /[/]/;").ok);
    CHECK(compiler::compile("var r = / /;").ok);
    expect_result("return /a(b+)c/.exec('xxabbbcyy')[0];", "abbbc");
    expect_result("return /a(b+)c/.exec('xxabbbcyy')[1];", "bbb");
    // `.index` is the most-used regex feature in p5.js.
    expect_result("return /a(b+)c/.exec('xxabbbcyy').index;", "2");
    expect_result("return /nope/.exec('abc') === null;", "true");
    expect_result("return /^\\d+$/.test('4711');", "true");
    expect_result("return /^\\d+$/.test('47a1');", "false");
    expect_result("return /x/i.test('X');", "true");
    expect_result("return /[a-f0-9]{2}/i.exec('zz A9 zz')[0];", "A9");
    // The constructor and the literal build the same thing.
    expect_result("return new RegExp('b+', '').exec('abbbc')[0];", "bbb");
    expect_result("return /ab/g.source + '|' + /ab/gi.flags;", "ab|gi");
    // Lookahead, positive and negative.
    expect_result("return /foo(?=bar)/.test('foobar');", "true");
    expect_result("return /foo(?=bar)/.test('foobaz');", "false");
    expect_result("return /foo(?!bar)/.test('foobaz');", "true");
    // A named group is an ordinary capture that also answers to a name.
    expect_result("return /(?<n>\\d+)/.exec('x42').groups.n;", "42");
    // `g` resumes from lastIndex and writes it back.
    expect_result("const re = /\\d/g; const s = 'a1b2'; re.exec(s); return re.exec(s)[0];", "2");
    expect_result("const re = /\\d/g; re.exec('a1'); re.exec('a1'); return re.lastIndex;", "0");
    // Lookbehind and backreferences (../regexp/model.cpp tests the rest of the
    // RegExp object model).
    expect_result("return /(?<=x)y/.test('xy');", "true");
    expect_result("return /(a)\\1/.test('aa') + '' + /(a)\\1/.test('ab');", "truefalse");
    // A pattern that cannot compile is a SyntaxError when the line runs.
    expect_result("try { new RegExp('('); return 'no'; } catch (e) { return e.name; }",
                  "SyntaxError");
}

/// `a.length = n` resizes the array. `a.length = 0` is how a great deal of code
/// empties an array: Phaser's scene manager ends boot with
/// `this._pending.length = 0`, and if the write were dropped the next frame
/// would add the same scene again and throw "Cannot add Scene with duplicate
/// key".
void test_array_length_is_writable() {
    expect_result("var a = [1,2,3]; a.length = 0; return a.length;", "0");
    expect_result("var a = [1,2,3]; a.length = 0; return JSON.stringify(a);", "[]");
    // Truncation keeps the front, not the back.
    expect_result("var a = [1,2,3,4]; a.length = 2; return JSON.stringify(a);", "[1,2]");
    // Growing pads with undefined, which JSON writes as null.
    expect_result("var a = [1]; a.length = 3; return a.length;", "3");
    expect_result("var a = [1]; a.length = 3; return String(a[2]);", "undefined");
    // A string coerces, as it does everywhere else.
    expect_result("var a = [1,2,3]; a.length = '1'; return JSON.stringify(a);", "[1]");
    // An invalid length (negative, fractional, NaN, or 2^32 or more) throws a
    // RangeError (ECMA-262 10.4.2.4), as every browser does.
    // array_object::set_js_length makes this check.
    expect_result("var a = [1,2]; try { a.length = -1; } catch (e) { return e.name; }",
                  "RangeError");
    expect_result("var a = [1,2]; try { a.length = NaN; } catch (e) { return e.name; }",
                  "RangeError");
    // The refused write leaves the array unchanged.
    expect_result("var a = [1,2]; try { a.length = -1; } catch (e) {} return a.length;", "2");
    // A typed array is a view over bytes sized once; resizing it would leave
    // the view and its buffer disagreeing, so the write is a no-op.
    expect_result("var t = new Uint8Array(4); t.length = 0; return t.length;", "4");
    // And the ordinary uses still work either side of it.
    expect_result("var a = []; a.push(1); a.push(2); a.length = 0; a.push(9); "
                  "return JSON.stringify(a);",
                  "[9]");
}

void test_collections() {
    expect_result("const s = new Set([1, 2, 3]); return s.has(2) + '|' + s.size;", "true|3");
    expect_result("const s = new Set(); s.add(1); s.add(1); return s.size;", "1");
    expect_result("const s = new Set([1, 2]); s.delete(1); return s.size;", "1");
    expect_result(
        "const s = new Set([1, 2]); let sum = 0; s.forEach(v => { sum += v; }); return sum;", "3");
    expect_result("const m = new Map([['a', 1]]); m.set('b', 2); return m.get('b') + '|' + m.size;",
                  "2|2");
    expect_result(
        "const m = new Map(); m.set('k', 1); m.set('k', 9); return m.get('k') + '|' + m.size;",
        "9|1");
    // `keys()` returns an iterator, which has no `join`; `[...m.keys()]` is how
    // it is written in a browser and here.
    expect_result("const m = new Map([['a', 1], ['b', 2]]); return [...m.keys()].join(',');",
                  "a,b");
    expect_result("const m = new Map([['a', 1]]); return m.has('z');", "false");
    // NaN matches NaN as a key (SameValueZero), which === does not.
    expect_result("const s = new Set([NaN]); return s.has(NaN);", "true");
    // A class may extend one, so super() has to initialise the receiver rather
    // than make a fresh object.
    expect_result(
        "class S extends Set {} const s = new S(); s.add(5); return s.has(5) + '|' + s.size;",
        "true|1");
}

void test_stdlib_additions() {
    expect_result("return Array.isArray([]) + '|' + Array.isArray({});", "true|false");
    expect_result("return Array.from('abc').join('-');", "a-b-c");
    expect_result("return Array.from([1, 2], x => x * 2).join(',');", "2,4");
    expect_result("return Array.of(1, 2, 3).length;", "3");
    expect_result("return [1, 2, 3].at(-1);", "3");
    expect_result("return [1, 2, 3, 4].fill(0, 1, 3).join(',');", "1,0,0,4");
    expect_result("return [1, [2, [3]]].flat().length;", "3");
    expect_result("return [1, [2, [3]]].flat(2).join(',');", "1,2,3");
    expect_result("return [1, 2].flatMap(x => [x, x]).join(',');", "1,1,2,2");
    expect_result("return [1, 2, 3].findLast(x => x < 3);", "2");
    expect_result("return Math.cbrt(27) + '|' + Math.log2(8) + '|' + Math.log10(1000);", "3|3|3");
    // Exactly 3, which the string comparison above cannot establish: a result
    // of 3.0000000000000004 prints as "3" too. glibc's cbrt is one ulp out on
    // some perfect cubes, so `===` checks the exact value.
    expect_result("return Math.cbrt(27) === 3 && Math.cbrt(216) === 6 && "
                  "Math.cbrt(-27) === -3 && Math.cbrt(1e9) === 1000;",
                  "true");
    expect_result("return Number.isInteger(2) + '|' + Number.isInteger(2.5);", "true|false");
    // Number.isFinite does not coerce and the global isFinite does; code relies
    // on the difference.
    expect_result("return Number.isFinite('1') + '|' + isFinite('1');", "false|true");
    expect_result("return Number.MAX_SAFE_INTEGER;", "9007199254740991");
}

/// Typed arrays coerce on write, which is what makes them typed. One without a
/// buffer is stored as an ordinary array of values rather than packed bytes
/// (see value/arrays.hpp), so the coercion is the only thing standing between
/// a write and a wrong value.
void test_typed_arrays() {
    expect_result("const a = new Uint8Array(3); return a.length + '|' + a[0];", "3|0");
    expect_result("const a = new Uint8Array([1, 2, 3]); return a.join(',');", "1,2,3");
    // Integer kinds wrap; Uint8ClampedArray, the pixel type, clamps instead.
    expect_result("const a = new Uint8Array(1); a[0] = 300; return a[0];", "44");
    expect_result("const a = new Uint8ClampedArray(1); a[0] = 300; return a[0];", "255");
    expect_result("const a = new Uint8ClampedArray(1); a[0] = -5; return a[0];", "0");
    expect_result("const a = new Int8Array(1); a[0] = 200; return a[0];", "-56");
    expect_result("const a = new Int32Array(1); a[0] = 2147483648; return a[0];", "-2147483648");
    // A float32 loses precision a double would keep, which is observable.
    expect_result("const a = new Float32Array(1); a[0] = 0.1; return a[0] === 0.1;", "false");
    expect_result("const a = new Float64Array(1); a[0] = 0.1; return a[0] === 0.1;", "true");
    // A typed array does not grow: a write past the end is dropped.
    expect_result("const a = new Uint8Array(2); a[5] = 1; return a.length;", "2");
    expect_result("return Uint16Array.BYTES_PER_ELEMENT;", "2");
    // ECMA-262 23.2.7: each constructor has a prototype object of its own,
    // with `constructor` and BYTES_PER_ELEMENT, and an instance's chain starts
    // there, which is what `class S extends Uint8Array {}` reads.
    expect_result("return Uint8Array.prototype.constructor === Uint8Array;", "true");
    expect_result("return Uint8Array.prototype.BYTES_PER_ELEMENT;", "1");
    expect_result("return Object.getPrototypeOf(new Uint8Array(1)) === Uint8Array.prototype;",
                  "true");
    expect_result("return new Uint8Array(1) instanceof Uint8Array;", "true");
    expect_result("return new Uint8Array(1) instanceof Int8Array;", "false");
    expect_result("return new Float32Array(1).constructor.name;", "Float32Array");
    expect_result("Uint8Array.prototype.tag = 'u8'; return new Uint8Array(1).tag;", "u8");
    expect_result("const a = new Uint8Array(4); a.set([9, 8], 1); return a.join(',');", "0,9,8,0");
    expect_result("const a = new Uint8Array([1, 2, 3, 4]); return a.subarray(1, 3).join(',');",
                  "2,3");
}

void test_string_statics() {
    expect_result("return String.fromCharCode(104, 105);", "hi");
    expect_result("return String.fromCharCode.apply(null, [97, 98, 99]);", "abc");
    expect_result("return String(42);", "42");
    // One code unit, whatever its UTF-8 is.
    expect_result("return String.fromCharCode(233).length;", "1");
}

/// btoa and atob are byte oriented: btoa's argument is a binary string of
/// values 0-255, not text. Treating it as text would truncate a page's exported
/// image at the first byte that is not valid UTF-8.
void test_base64() {
    expect_result("return btoa('hello');", "aGVsbG8=");
    expect_result("return btoa('hi');", "aGk="); // The padding says how many bytes were real.
    expect_result("return btoa('abc');", "YWJj");
    expect_result("return btoa('');", "");
    expect_result("return atob('aGVsbG8=');", "hello");
    expect_result("return atob('aGk=');", "hi");
    expect_result("return atob(btoa('the quick brown fox'));", "the quick brown fox");
    expect_result("return atob(btoa('\\x00\\x01\\x7f')).length;", "3");
    expect_result("return atob(btoa('\\x00\\x00\\x00')).charCodeAt(1);", "0");
    // Whitespace in the encoded text is ignored rather than decoded.
    expect_result("return atob('aGVs\\nbG8=');", "hello");
}

void test_math() {
    // Math.random aside, these are the functions every page reaches for.
    expect_result("return Math.floor(3.7);", "3");
    expect_result("return Math.ceil(3.2);", "4");
    expect_result("return Math.abs(-5);", "5");
    expect_result("return Math.max(1, 9, 4);", "9");
    expect_result("return Math.min(1, 9, 4);", "1");
    expect_result("return Math.sqrt(16);", "4");
    expect_result("return Math.pow(2, 10);", "1024");
    // JavaScript rounds .5 toward positive infinity; std::round rounds away
    // from zero and would give -1 here.
    expect_result("return Math.round(-0.5);", "0");
    expect_result("return Math.round(2.5);", "3");
    expect_result("return Math.floor(Math.PI * 100) / 100;", "3.14");
}

void test_math_random_is_in_range_and_moves() {
    expect_result("var ok = true;"
                  "for (var i = 0; i < 200; i++) { var r = Math.random();"
                  "  if (r < 0 || r >= 1) { ok = false; } }"
                  "return ok;",
                  "true");
    expect_result("var a = Math.random(); var b = Math.random(); return a != b;", "true");
}

void test_array_methods() {
    expect_result("var a = [1,2]; a.push(3); return a.length;", "3");
    expect_result("var a = [1,2,3]; return a.pop();", "3");
    expect_result("var a = [1,2,3]; a.shift(); return a[0];", "2");
    expect_result("var a = [2,3]; a.unshift(1); return a[0];", "1");
    expect_result("return [1,2,3].indexOf(2);", "1");
    expect_result("return [1,2,3].includes(9);", "false");
    expect_result("return [1,2,3].join('-');", "1-2-3");
    expect_result("return [1,2,3].slice(1).join('');", "23");
    expect_result("return [1,2,3].concat([4]).length;", "4");
    expect_result("var a = [1,2,3]; a.reverse(); return a.join('');", "321");
    expect_result("var a = [1,2,3,4]; a.splice(1, 2); return a.join('');", "14");
}

void test_array_iteration_calls_back_into_the_vm() {
    // These need context::call: a native function invoking a JavaScript one.
    expect_result("var t = 0; [1,2,3].forEach(function (x) { t += x; }); return t;", "6");
    expect_result("return [1,2,3].map(function (x) { return x * 2; }).join(',');", "2,4,6");
    expect_result("return [1,2,3,4].filter(function (x) { return x % 2 == 0; }).join(',');", "2,4");
    expect_result("return [1,2,3,4].reduce(function (t, x) { return t + x; }, 0);", "10");
    expect_result("return [1,2,3].find(function (x) { return x > 1; });", "2");
    expect_result("return [1,2,3].some(function (x) { return x > 2; });", "true");
    expect_result("return [1,2,3].every(function (x) { return x > 0; });", "true");
    // The callback gets the index too, which many real uses depend on.
    expect_result("var s = ''; ['a','b'].forEach(function (x, i) { s += i + x; }); return s;",
                  "0a1b");
}

void test_array_sort() {
    expect_result("return [3,1,2].sort(function (a, b) { return a - b; }).join('');", "123");
    expect_result("return [3,1,2].sort(function (a, b) { return b - a; }).join('');", "321");
    // The default really is lexicographic on the string form, which is why
    // [10, 9] sorts to [10, 9] and surprises everyone once.
    expect_result("return [10,9].sort().join(',');", "10,9");

    // The properties the merge sort must keep. These check correctness, not
    // speed.

    // Stable: equal keys keep their original order. The specification requires
    // it, and a merge that takes the right side on a tie would quietly break it
    // while still producing sorted output.
    expect_result("const xs = [['b',1],['a',1],['c',0],['d',1]];"
                  "xs.sort(function (p, q) { return p[1] - q[1]; });"
                  "return xs.map(function (p) { return p[0]; }).join('');",
                  "cbad");

    // Big enough that a quadratic sort would be visible, and checked by
    // content rather than by trusting the sort that produced it.
    expect_result("const a = []; let s = 12345;"
                  "for (let i = 0; i < 2000; i++) { s = (s * 1103515245 + 12345) & 2147483647;"
                  "  a.push(s % 1000); }"
                  "a.sort(function (x, y) { return x - y; });"
                  "let ok = a.length === 2000;"
                  "for (let i = 1; i < a.length; i++) { if (a[i-1] > a[i]) { ok = false; } }"
                  "return String(ok);",
                  "true");

    // An inconsistent comparator must not corrupt anything, which is why
    // std::sort is not used (it would be undefined behaviour). A merge reads
    // only inside two ranges it computed itself, so no answer can move an index
    // out of them: the order is unspecified, but the engine survives.
    expect_result("const a = [5,3,8,1,9,2,7];"
                  "a.sort(function () { return 1; });"
                  "return String(a.length);",
                  "7");
    expect_result("const a = [5,3,8,1,9,2,7];"
                  "a.sort(function () { return -1; });"
                  "return String(a.length);",
                  "7");

    // A comparator that mutates the array it is sorting. The merge sorts a
    // snapshot and writes it back, so the worst case is an unspecified order,
    // never a read past the end of the live array.
    expect_result("const a = [4,2,5,1,3];"
                  "a.sort(function (x, y) { a.length = 0; return x - y; });"
                  "return 'survived';",
                  "survived");
    expect_result("const a = [4,2,5,1,3];"
                  "a.sort(function (x, y) { a.push(99); return x - y; });"
                  "return 'survived';",
                  "survived");

    // The default comparison precomputes its string keys; it must still give
    // the same lexicographic order, and still be stable.
    expect_result("return ['banana','apple','cherry'].sort().join(',');", "apple,banana,cherry");
    expect_result("return [1,5,20,3].sort().join(',');", "1,20,3,5");
    expect_result("return [].sort().length;", "0");
    expect_result("return [1].sort().join('');", "1");
}

void test_string_methods() {
    expect_result("return 'abc'.toUpperCase();", "ABC");
    expect_result("return 'ABC'.toLowerCase();", "abc");
    expect_result("return 'a,b,c'.split(',').length;", "3");
    expect_result("return 'a,b,c'.split(',')[1];", "b");
    expect_result("return 'hello'.indexOf('ll');", "2");
    expect_result("return 'hello'.includes('ell');", "true");
    expect_result("return 'hello'.slice(1, 3);", "el");
    expect_result("return 'hello'.substring(3, 1);", "el"); // substring swaps its arguments.
    expect_result("return '  hi  '.trim();", "hi");
    expect_result("return 'ab'.repeat(3);", "ababab");
    expect_result("return 'a-b-a'.replace('a', 'X');", "X-b-a");    // The first only.
    expect_result("return 'a-b-a'.replaceAll('a', 'X');", "X-b-X"); // All.
    expect_result("return '5'.padStart(3, '0');", "005");
    expect_result("return 'abc'.charAt(1);", "b");
    expect_result("return 'A'.charCodeAt(0);", "65");
    expect_result("return 'abc'.startsWith('ab');", "true");
    expect_result("return 'abc'.length;", "3"); // Still works alongside the methods.
}

void test_number_and_conversions() {
    expect_result("return (3.14159).toFixed(2);", "3.14");
    expect_result("return parseInt('42');", "42");
    expect_result("return parseInt('ff', 16);", "255");
    expect_result("return parseFloat('3.5');", "3.5");
    // parseInt of nonsense is NaN, not an error; a page checks it with isNaN
    // straight afterwards.
    expect_result("return isNaN(parseInt('abc'));", "true");
    expect_result("return String(42);", "42");
    expect_result("return Number('7') + 1;", "8");
}

void test_json() {
    expect_result("return JSON.stringify({a: 1, b: 'x'});", "{\"a\":1,\"b\":\"x\"}");
    expect_result("return JSON.stringify([1, 'two', true]);", "[1,\"two\",true]");
    expect_result("return JSON.parse('{\"n\": 7}').n;", "7");
    expect_result("return JSON.parse('[1,2,3]')[2];", "3");
    expect_result("var o = JSON.parse(JSON.stringify({a: [1, {b: 2}]})); return o.a[1].b;", "2");
    expect_result("return JSON.stringify(JSON.parse('{\"s\":\"a\\\\nb\"}'));",
                  "{\"s\":\"a\\nb\"}"); // Escapes survive a round trip.
}

/// `entries`, `keys` and `values` on an array, and `entries` on a Set.
///
/// Each returns a real iterator: it has `next` and `@@iterator`, its own state
/// is not enumerable, and for..of and spread both walk it. Babylon drives one
/// by hand for the Map of shadow generators a light owns. An array's three are
/// live (ECMA-262 23.1.5.1): `length` is read on every step, over any
/// array-like.
void test_array_iterators() {
    // `{}` and not the pairs: an iterator has no enumerable own property, so
    // its internal state stays invisible.
    expect_result("return JSON.stringify(['x', 'y'].entries());", "{}");
    expect_result("return JSON.stringify([...['x', 'y'].entries()]);", "[[0,\"x\"],[1,\"y\"]]");
    expect_result("const it = [7, 8].values(); const a = it.next();"
                  "return a.value + ',' + a.done + ',' + it.next().value + ',' + it.next().done;",
                  "7,false,8,true");
    expect_result("const it = [1].values(); return it[Symbol.iterator]() === it;", "true");
    expect_result("let out = ''; for (const pair of ['x', 'y'].entries()) {"
                  "  out += pair[0] + ':' + pair[1] + ';'; } return out;",
                  "0:x;1:y;");
    // Destructured, which is how it is actually written.
    expect_result("let out = ''; for (const [i, v] of ['a', 'b'].entries()) {"
                  "  out += i + v; } return out;",
                  "0a1b");
    expect_result("return [...[7, 8].keys()].join(',');", "0,1");
    expect_result("return [...[7, 8].values()].join(',');", "7,8");
    expect_result("const a = []; const it = a.values(); a.push(1); const r = it.next();"
                  "a.push(2); return r.value + ',' + r.done + ',' + it.next().value + ',' +"
                  "it.next().done;",
                  "1,false,2,true");
    expect_result("const a = []; const it = a.keys(); it.next(); a.push(1);"
                  "return it.next().done;",
                  "true");
    expect_result("return [...Array.prototype.entries.call({length: 2, 0: 'a', 1: 'b'})]"
                  ".join('|');",
                  "0,a|1,b");
    expect_result("try { Array.prototype.values.call(null); } catch (e) { return e.name; }",
                  "TypeError");
    // A Set's entries pairs each member with itself, which looks odd and is the
    // spec: it lets a Set and a Map be walked by the same code.
    expect_result("const s = new Set(['a']);"
                  "let out = ''; for (const [k, v] of s.entries()) { out += k + v; } return out;",
                  "aa");
    expect_result("return typeof new Map().entries;", "function");
}

/// `Date` is constructible and reads a calendar out of a millisecond count.
/// p5's day()/month()/year()/hour() each build a Date.
///
/// UTC only (a timezone database is a different project), so the local and
/// UTC methods agree. `new Date()` with no argument is the epoch, deliberately:
/// the clock is fixed for the same reason Math.random is seeded, so a page
/// that draws from either can have a golden image.
void test_date() {
    expect_result("const d = new Date(0);"
                  "return [d.getFullYear(), d.getMonth(), d.getDate(), d.getDay()].join(',');",
                  "1970,0,1,4");
    expect_result("return new Date(0).toISOString();", "1970-01-01T00:00:00.000Z");
    // (year, monthIndex, day, h, m, s): the month is zero-based, the wart
    // every calendar bug starts with.
    expect_result("const d = new Date(2026, 6, 29, 13, 45, 30);"
                  "return [d.getFullYear(), d.getMonth(), d.getDate(),"
                  "        d.getHours(), d.getMinutes(), d.getSeconds()].join(',');",
                  "2026,6,29,13,45,30");
    // A round trip through the millisecond count is the arithmetic working
    // both ways, which is what the civil-date algorithms are for.
    expect_result("const d = new Date(2026, 6, 29, 13, 45, 30);"
                  "return new Date(d.getTime()).toISOString() === d.toISOString();",
                  "true");
    expect_result("return new Date(1234567890123).getFullYear();", "2009");
    // The two formatters, pinned byte-for-byte on a known instant, and a month
    // past December rolling into the next year.
    expect_result("return new Date(1234567890123).toISOString();", "2009-02-13T23:31:30.123Z");
    expect_result("return new Date(1234567890123).toString();",
                  "Fri Feb 13 2009 23:31:30 GMT+0000 (Coordinated Universal Time)");
    expect_result("return new Date(1234567890123).toUTCString();", "Fri, 13 Feb 2009 23:31:30 GMT");
    expect_result("return new Date(1234567890123).toDateString();", "Fri Feb 13 2009");
    expect_result("return new Date(2024, 12, 1).toISOString();", "2025-01-01T00:00:00.000Z");
    expect_result("return Date.UTC(1970, 0, 2);", "86400000");
    expect_result("return new Date(0) instanceof Date;", "true");
    // valueOf makes a Date work in arithmetic, as in `end - start`.
    expect_result("return typeof (new Date(5000) - new Date(2000));", "number");
    expect_result("return new Date(5000) - new Date(2000);", "3000");
    // ECMA-262 21.4.4.45 @@toPrimitive: `+` takes the string, `-` the number.
    expect_result("return new Date(0) + '';",
                  "Thu Jan 01 1970 00:00:00 GMT+0000 (Coordinated Universal Time)");
    // Date.parse and the string constructor: the ISO format, with and without
    // a time and an offset, and the two forms the engine itself prints.
    expect_result("return Date.parse('2009-02-13T23:31:30.123Z');", "1234567890123");
    expect_result("return Date.parse('2009-02-13T23:31:30.123+01:00');", "1234564290123");
    expect_result("return new Date('2009-02-13').getTime();", "1234483200000");
    expect_result("return new Date('2009').getTime();", "1230768000000");
    expect_result("return Date.parse('Fri Feb 13 2009 23:31:30 GMT+0000 (UTC)');", "1234567890000");
    expect_result("return Date.parse('Fri, 13 Feb 2009 23:31:30 GMT');", "1234567890000");
    expect_result("return Date.parse('nonsense');", "NaN");
    expect_result("return String(new Date(NaN));", "Invalid Date");
    expect_result("return new Date(NaN).getFullYear();", "NaN");
    expect_result("try { new Date(NaN).toISOString(); } catch (e) { return e.constructor.name; }",
                  "RangeError");
    expect_result("return new Date(8.64e15 + 1).getTime();", "NaN");
    expect_result("return JSON.stringify({d: new Date(0)});",
                  "{\"d\":\"1970-01-01T00:00:00.000Z\"}");
    // The setters rebuild through MakeDate, normalising an overflow; the
    // UTC twins are the same functions under this engine's one zone.
    expect_result("const d = new Date(0); d.setFullYear(2000, 1, 29); return d.toISOString();",
                  "2000-02-29T00:00:00.000Z");
    expect_result("const d = new Date(0); d.setMonth(13); return d.toISOString();",
                  "1971-02-01T00:00:00.000Z");
    expect_result("const d = new Date(0); d.setUTCHours(25, 61); return d.toISOString();",
                  "1970-01-02T02:01:00.000Z");
    expect_result("const d = new Date(0); return d.setTime(5) + ',' + d.getTime();", "5,5");
    expect_result("const d = new Date(0); d.setDate(0); return d.toISOString();",
                  "1969-12-31T00:00:00.000Z");
    expect_result("return new Date(99, 0).getFullYear();", "1999");
    expect_result("return new Date(-1).getUTCMilliseconds();", "999");
    expect_result("return new Date(Date.UTC(-100000, 0, 1)).toISOString();",
                  "-100000-01-01T00:00:00.000Z");
    expect_result("return typeof Date();", "string");
    expect_result(
        "try { Date.prototype.getTime.call({}); } catch (e) { return e.constructor.name; }",
        "TypeError");
}

/// An ArrayBuffer is shared storage: every view over it reads and writes the
/// same bytes, so a page that writes through one view and reads through
/// another sees its writes. That is why a page wraps `await res.arrayBuffer()`
/// in a view.
void test_array_buffer_is_shared() {
    expect_result("return new ArrayBuffer(4).byteLength;", "4");
    expect_result("const buf = new ArrayBuffer(4);"
                  "const a = new Uint8Array(buf), b = new Uint8Array(buf);"
                  "a[0] = 42; return b[0];",
                  "42");
    expect_result("const buf = new ArrayBuffer(4);"
                  "const a = new Uint8Array(buf), b = new Uint8Array(buf);"
                  "b[1] = 7; return a[1];",
                  "7");
    expect_result("const buf = new ArrayBuffer(4); return new Uint8Array(buf).length;", "4");
    // The view still coerces on write, so the element kind means something.
    expect_result("const a = new Uint8Array(new ArrayBuffer(2)); a[0] = 300; return a[0];", "44");
    // A view made without a buffer owns its own elements.
    expect_result("const buf = new ArrayBuffer(2); const shared = new Uint8Array(buf);"
                  "const own = new Uint8Array(2); own[0] = 9; shared[0] = 1; return own[0];",
                  "9");
    // A sub-range view sees writes through the buffer at its offset.
    expect_result("const buf = new ArrayBuffer(4); const whole = new Uint8Array(buf);"
                  "const part = new Uint8Array(buf, 1, 2);"
                  "whole[1] = 5; whole[2] = 6; return part[0] + ',' + part[1] + ',' + part.length;",
                  "5,6,2");
    expect_result("const buf = new ArrayBuffer(4); const whole = new Uint8Array(buf);"
                  "const part = new Uint8Array(buf, 2, 2); part[0] = 9; return whole[2];",
                  "9");

    // Views of different element widths over one buffer must reinterpret the
    // same bytes (Phaser's WebGL renderer keeps four such views over its vertex
    // buffer).
    expect_result("const buf = new ArrayBuffer(8);"
                  "const f = new Float32Array(buf), u = new Uint32Array(buf);"
                  "const b = new Uint8Array(buf), h = new Uint16Array(buf);"
                  "f[0] = 64; return f[0];",
                  "64");
    // 64.0f is 0x42800000 (1115684864 as a uint32; bytes 0, 0, 128, 66 in
    // little-endian order), readable through every other view.
    expect_result("const buf = new ArrayBuffer(4);"
                  "const f = new Float32Array(buf), u = new Uint32Array(buf);"
                  "f[0] = 64; return u[0];",
                  "1115684864");
    expect_result("const buf = new ArrayBuffer(4);"
                  "const f = new Float32Array(buf), b = new Uint8Array(buf);"
                  "f[0] = 64; return b[0] + ',' + b[1] + ',' + b[2] + ',' + b[3];",
                  "0,0,128,66");
    // And back the other way: bytes written through the narrow view are the
    // float the wide one reads.
    expect_result("const buf = new ArrayBuffer(4);"
                  "const f = new Float32Array(buf), b = new Uint8Array(buf);"
                  "b[3] = 66; b[2] = 128; return f[0];",
                  "64");
    // A view's length is in elements and its byteLength in bytes.
    expect_result("const buf = new ArrayBuffer(8); const f = new Float32Array(buf);"
                  "return f.length + ',' + f.byteLength + ',' + f.BYTES_PER_ELEMENT;",
                  "2,8,4");
    // `subarray` shares the buffer; it is not `slice`.
    expect_result("const buf = new ArrayBuffer(16); const f = new Float32Array(buf);"
                  "const tail = f.subarray(2); f[2] = 7; return tail[0] + ',' + tail.length;",
                  "7,2");
    // A view over a buffer another view reached through `.buffer`.
    expect_result("const f = new Float32Array(new ArrayBuffer(4));"
                  "const b = new Uint8Array(f.buffer); f[0] = 64; return b[3];",
                  "66");
    // Signed kinds reinterpret the bytes on read.
    expect_result("const buf = new ArrayBuffer(4);"
                  "const u = new Uint8Array(buf), s = new Int8Array(buf);"
                  "u[0] = 200; return s[0];",
                  "-56");
}

/// String.prototype.replace with a RegExp pattern: global vs. first match,
/// $-substitutions, and function replacements. acorn (bundled by p5) builds
/// its keyword tables with `words.replace(/ /g, '|')`; treating the pattern as
/// literal text would leave every keyword matcher unable to match.
void test_string_replace() {
    // A string pattern is literal text.
    expect_result("return 'a b c'.replace(' ', '|');", "a|b c");
    expect_result("return 'a b c'.replaceAll(' ', '|');", "a|b|c");
    // `g` on the pattern means every match; without it, the first only.
    expect_result("return 'a b c'.replace(/ /g, '|');", "a|b|c");
    expect_result("return 'a b c'.replace(/ /, '|');", "a|b c");
    expect_result("return 'a b c'.replaceAll(/ /g, '|');", "a|b|c");
    expect_result("return 'a\\tb'.replace(/\\t/g, '  ');", "a  b");
    expect_result("return 'abc'.replace(/z/g, '!');", "abc");
    // `$n` is a capture, `$&` the whole match, `$$` a literal dollar.
    expect_result("return 'John Smith'.replace(/(\\w+) (\\w+)/, '$2, $1');", "Smith, John");
    expect_result("return 'abc'.replace(/b/, '[$&]');", "a[b]c");
    expect_result("return 'abc'.replace(/b/, '$$');", "a$c");
    // A function replacement is called with (match, p1..pn, offset, whole).
    expect_result("return 'a1b2'.replace(/\\d/g, function (m) { return '<' + m + '>'; });",
                  "a<1>b<2>");
    expect_result("return 'xy'.replace(/(x)(y)/, function (m, p1, p2, off, whole) {"
                  "  return p2 + p1 + off + whole.length; });",
                  "yx02");
    // Anchors and greedy groups, which is the shape p5 builds its CDN url with.
    expect_result("return '1.2.3-x'.replace(/^(\\d+\\.\\d+)\\.\\d+.*$/, '$1');", "1.2");
    // acorn's keyword-table idiom.
    expect_result("return new RegExp('^(?:' + 'break case function'.replace(/ /g, '|') + ')$')"
                  "  .test('function');",
                  "true");
    // An empty match still advances rather than looping forever.
    expect_result("return 'ab'.replace(/x*/g, '-');", "-a-b-");
}

/// `String.prototype.match`. The forms with and without `g` return different
/// shapes, which is what code branches on.
void test_string_match() {
    expect_result("const m = 'a1b22'.match(/([a-z])(\\d+)/);"
                  "return m[0] + ',' + m[1] + ',' + m[2] + ',' + m.index;",
                  "a1,a,1,0");
    // With `g` it is a flat list of matched strings and nothing else.
    expect_result("return 'a1b22'.match(/\\d+/g).join('|');", "1|22");
    // No match is null in both forms: the usual guard is `if (m)`, and an
    // empty array is truthy.
    expect_result("return 'xyz'.match(/\\d/) === null;", "true");
    expect_result("return 'xyz'.match(/\\d/g) === null;", "true");
}

/// `structuredClone`: a deep copy of plain data, including through a cycle.
void test_structured_clone() {
    expect_result("const a = { n: 1, deep: { list: [1, 2] } };"
                  "const b = structuredClone(a);"
                  "b.deep.list[0] = 9;"
                  "return a.deep.list[0] + ',' + b.deep.list[0];",
                  "1,9");
    expect_result("const a = [1, [2, 3]]; const b = structuredClone(a);"
                  "return (a[1] === b[1]) + ',' + b[1][1];",
                  "false,3");
    // A structure that points back at itself is exactly what a naive recursive
    // copy cannot survive.
    expect_result("const a = { n: 1 }; a.self = a; const b = structuredClone(a);"
                  "return (b.self === b) + ',' + (b.self === a);",
                  "true,false");
    // A typed array keeps being one, so a clone of a pixel buffer still clamps.
    expect_result("const a = new Uint8ClampedArray([1, 2]); const b = structuredClone(a);"
                  "b[0] = 400; return b[0] + ',' + a[0];",
                  "255,1");
    // Data only: a function is not clonable, and that is a throw rather than a
    // silent share of the original.
    expect_result("try { structuredClone({ f: function () {} }); } catch (e) { return e.name; }"
                  "return 'not thrown';",
                  "DataCloneError");
    // HTML 2.7: what the specification serialises (a wrapper, a Date, a
    // RegExp with lastIndex reset, a Map and a Set, an Error of the seven
    // kinds, a bigint, an ArrayBuffer and a view over it) and what it does not
    // (a non-enumerable or inherited property, a symbol, a getter that throws).
    expect_result("const b = structuredClone(new Number(5));"
                  "return (b instanceof Number) + ',' + Number(b) + ',' + typeof b;",
                  "true,5,object");
    expect_result("const d = structuredClone(new Date(86400000));"
                  "return (d instanceof Date) + ',' + d.getTime();",
                  "true,86400000");
    expect_result("const r = /a+/gi; r.lastIndex = 3; const c = structuredClone(r);"
                  "return c.source + '/' + c.flags + '/' + c.lastIndex + '/' + (c !== r);",
                  "a+/gi/0/true");
    expect_result("const m = new Map([[1, {x: 1}]]); const c = structuredClone(m);"
                  "return (c instanceof Map) + ',' + c.get(1).x + ',' + (c.get(1) !== m.get(1));",
                  "true,1,true");
    expect_result("const s = structuredClone(new Set([1, 'a'])); return s.has('a') + ',' + s.size;",
                  "true,2");
    expect_result("const e = new RangeError('m', { cause: 7 }); e.foo = 1;"
                  "const c = structuredClone(e); return c.name + ',' + c.message + ',' + c.cause +"
                  "',' + (c instanceof RangeError) + ',' + c.foo;",
                  "RangeError,m,7,true,undefined");
    expect_result("return typeof structuredClone(12n) + ',' + structuredClone(12n);", "bigint,12");
    expect_result("const buf = new Uint8Array([1, 2, 3, 4]).buffer;"
                  "const v = new Uint8Array(buf, 1, 2); const c = structuredClone({ v, buf });"
                  "return c.v[0] + ',' + c.v.length + ',' + (c.v.buffer === c.buf) + ',' +"
                  "(c.buf !== buf) + ',' + c.buf.byteLength;",
                  "2,2,true,true,4");
    expect_result("const o = Object.create({ inherited: 1 }); o.own = 2;"
                  "Object.defineProperty(o, 'hidden', { value: 3, enumerable: false });"
                  "const c = structuredClone(o);"
                  "return ('inherited' in c) + ',' + c.own + ',' + ('hidden' in c);",
                  "false,2,false");
    expect_result("const a = [1, , 3]; a.foo = 'bar'; const c = structuredClone(a);"
                  "return c.length + ',' + (1 in c) + ',' + c.foo;",
                  "3,false,bar");
    expect_result("try { structuredClone({ get x() { throw new TypeError('g'); } }); }"
                  "catch (e) { return e.name + ':' + e.message; }",
                  "TypeError:g");
    expect_result("try { structuredClone(Symbol('s')); } catch (e) { return e.name; }",
                  "DataCloneError");
    expect_result("const p = structuredClone(Object.prototype); return Object.keys(p).length + ','"
                  " + (p !== Object.prototype);",
                  "0,true");
}

} // namespace

/// `eval`, as an indirect eval (global scope): the completion value comes
/// back, declarations land on the global object, a syntax error is a
/// catchable SyntaxError, and a non-string is returned as is.
void test_eval() {
    expect_result("return eval('1 + 1');", "2");
    expect_result("return eval('var q = 5; q * 2');", "10");
    expect_result("eval('var made = 7'); return made;", "7");
    expect_result("return eval(42);", "42");
    expect_result("return typeof eval('function f() { return 3; }; f');", "function");
    expect_result(
        "try { eval('let x = ;'); return 'no'; } catch (e) { return e instanceof SyntaxError; }",
        "true");
    expect_result("return eval('') === undefined;", "true");
    // `++x` as a program's first statement: the update node is node 1 and its
    // prefix flag is 1, which a tree walk must not mistake for a child index.
    expect_result("var x = 1; return eval('++x');", "2");
    expect_result("var x = 1; return eval('++x; x++; x');", "3");
    // The completion value (ECMA-262 14.2.2 and UpdateEmpty): a declaration is
    // empty and keeps the previous value; an `if`, a loop, a `switch` and a
    // `try` start from undefined and take their body's; a finally keeps the
    // try's.
    expect_result("return eval('3; var y = 1;');", "3");
    expect_result("return String(eval('3; if (true) {}'));", "undefined");
    expect_result("return eval('3; if (true) { 4; }');", "4");
    expect_result("return String(eval('3; while (false);'));", "undefined");
    expect_result("return eval('3; do { 5; break; } while (false)');", "5");
    expect_result("return eval('3; switch (1) { case 1: 6; break; case 2: 7; }');", "6");
    expect_result("return String(eval('3; switch (1) { case 2: 7; }'));", "undefined");
    expect_result("return eval('3; try { 8; } finally { 9; }');", "8");
    expect_result("return eval('3; try { throw 1; } catch (e) { 10; }');", "10");
    expect_result("return eval('3; L: { 11; break L; }');", "11");
    expect_result("return eval('3; {}');", "3");
    expect_result("return eval('3; function f() {}');", "3");
    expect_result("return eval('for (var i = 0; i < 2; i++) { i; }');", "1");
}

int main() {
    test_eval();
    test_regex();
    test_array_length_is_writable();
    test_collections();
    test_stdlib_additions();
    test_typed_arrays();
    test_string_statics();
    test_base64();
    test_math();
    test_math_random_is_in_range_and_moves();
    test_array_methods();
    test_array_iteration_calls_back_into_the_vm();
    test_array_sort();
    test_string_methods();
    test_number_and_conversions();
    test_json();
    test_array_iterators();
    test_date();
    test_array_buffer_is_shared();
    test_string_replace();
    test_string_match();
    test_structured_clone();
    REPORT("vm_stdlib");
}
