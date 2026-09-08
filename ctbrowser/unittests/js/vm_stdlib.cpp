// The standard library: Math, Array, String, RegExp, JSON, Date, the
// collections, typed arrays over a shared ArrayBuffer, and the two web globals
// (btoa/atob and structuredClone). Carved out of js/vm_basics.cpp on
// 2026-09-08 - vm_operators.cpp names the family, and vm_expect.hpp is the
// assertion every file in it shares.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include "vm_expect.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

using namespace ctbrowser::script;

namespace {

// A REGEX ENGINE. Literals were rejected by name; there was none. Ported from
// ctjs's backtracking matcher, which was already self-contained and coupled to
// its host by exactly two calls, then extended with what p5.js actually uses:
// lookahead, the sticky flag and named groups. Lookbehind and backreferences
// are REFUSED rather than mis-matched - neither appears in p5.js, and a
// matcher that silently ignores an assertion is worse than one that says no.
void test_regex() {
    expect_result("return /a(b+)c/.exec('xxabbbcyy')[0];", "abbbc");
    expect_result("return /a(b+)c/.exec('xxabbbcyy')[1];", "bbb");
    // .index is the most-used feature of all, at 143 sites in p5.js
    expect_result("return /a(b+)c/.exec('xxabbbcyy').index;", "2");
    expect_result("return /nope/.exec('abc') === null;", "true");
    expect_result("return /^\\d+$/.test('4711');", "true");
    expect_result("return /^\\d+$/.test('47a1');", "false");
    expect_result("return /x/i.test('X');", "true");
    expect_result("return /[a-f0-9]{2}/i.exec('zz A9 zz')[0];", "A9");
    // the constructor and the literal build the same thing
    expect_result("return new RegExp('b+', '').exec('abbbc')[0];", "bbb");
    expect_result("return /ab/g.source + '|' + /ab/gi.flags;", "ab|gi");
    // lookahead, positive and negative
    expect_result("return /foo(?=bar)/.test('foobar');", "true");
    expect_result("return /foo(?=bar)/.test('foobaz');", "false");
    expect_result("return /foo(?!bar)/.test('foobaz');", "true");
    // a named group is an ordinary capture that also answers to a name
    expect_result("return /(?<n>\\d+)/.exec('x42').groups.n;", "42");
    // `g` resumes from lastIndex and writes it back
    expect_result("const re = /\\d/g; const s = 'a1b2'; re.exec(s); return re.exec(s)[0];", "2");
    expect_result("const re = /\\d/g; re.exec('a1'); re.exec('a1'); return re.lastIndex;", "0");
    // and a pattern that cannot compile does not match rather than crashing
    expect_result("return /(?<=x)y/.test('xy');", "false");
}

// `a.length = n` RESIZES, and a write that is silently dropped is the kind of
// gap that looks like support. The engine read `length` and ignored every write
// to it, so `a.length = 0` - which is how a great deal of code empties an array
// - left the array exactly as full as it was. Phaser found it: its scene
// manager ends boot with `this._pending.length = 0`, so the queue it had just
// drained was still full and the next frame added the same scene again and
// threw "Cannot add Scene with duplicate key".
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
    // NONSENSE THROWS, and these two rows used to assert the opposite.
    //
    // The leniency they pinned - "a dropped nonsense write leaves the array as
    // it was" - was not free: the same branch that swallowed `-1` also had to
    // decide what to do with 4294967295, and it RESIZED, which is 34 GB and the
    // SIGABRT test262 measured in eleven of its Array tests on 2026-09-02. One
    // predicate now answers both questions (array_object::set_js_length), and
    // 10.4.2.4's RangeError is what it answers with. No page can have depended
    // on the old behaviour, because every browser throws here.
    expect_result("var a = [1,2]; try { a.length = -1; } catch (e) { return e.name; }",
                  "RangeError");
    expect_result("var a = [1,2]; try { a.length = NaN; } catch (e) { return e.name; }",
                  "RangeError");
    // ...and the array is untouched by the refusal, which is the half of the
    // old leniency worth keeping.
    expect_result("var a = [1,2]; try { a.length = -1; } catch (e) {} return a.length;", "2");
    // A TYPED array is a view over bytes sized once; resizing it would leave
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
    // An ITERATOR, so it has no `join` - `[...m.keys()].join(',')` is how it is
    // written in a browser and here.
    expect_result("const m = new Map([['a', 1], ['b', 2]]); return [...m.keys()].join(',');",
                  "a,b");
    expect_result("const m = new Map([['a', 1]]); return m.has('z');", "false");
    // NaN matches NaN as a key, which === does not
    expect_result("const s = new Set([NaN]); return s.has(NaN);", "true");
    // and a class may extend one - which means super() has to initialise the
    // RECEIVER rather than making a fresh object
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
    // AND IT IS EXACTLY 3, which the line above did not actually establish: it
    // passed for years against 3.0000000000000004, because `to_string` rounded
    // to six decimals and printed "3" either way. glibc's cbrt is an ulp out on
    // a perfect cube. `===` is what asks the question the string never did.
    expect_result("return Math.cbrt(27) === 3 && Math.cbrt(216) === 6 && "
                  "Math.cbrt(-27) === -3 && Math.cbrt(1e9) === 1000;",
                  "true");
    expect_result("return Number.isInteger(2) + '|' + Number.isInteger(2.5);", "true|false");
    // these do NOT coerce, and the difference is used deliberately
    expect_result("return Number.isFinite('1') + '|' + isFinite('1');", "false|true");
    expect_result("return Number.MAX_SAFE_INTEGER;", "9007199254740991");
}

// TYPED ARRAYS COERCE ON WRITE, which is the whole of what makes them typed.
// Stored as ordinary arrays of values rather than packed bytes - that buys the
// existing array machinery for nothing - but a shortcut on the coercion would
// have been a silent wrong answer in exactly the place it matters most.
void test_typed_arrays() {
    expect_result("const a = new Uint8Array(3); return a.length + '|' + a[0];", "3|0");
    expect_result("const a = new Uint8Array([1, 2, 3]); return a.join(',');", "1,2,3");
    // wrapping, and the one type that CLAMPS instead - it is the pixel type
    expect_result("const a = new Uint8Array(1); a[0] = 300; return a[0];", "44");
    expect_result("const a = new Uint8ClampedArray(1); a[0] = 300; return a[0];", "255");
    expect_result("const a = new Uint8ClampedArray(1); a[0] = -5; return a[0];", "0");
    expect_result("const a = new Int8Array(1); a[0] = 200; return a[0];", "-56");
    expect_result("const a = new Int32Array(1); a[0] = 2147483648; return a[0];", "-2147483648");
    // a float32 loses precision a double would keep, which is observable
    expect_result("const a = new Float32Array(1); a[0] = 0.1; return a[0] === 0.1;", "false");
    expect_result("const a = new Float64Array(1); a[0] = 0.1; return a[0] === 0.1;", "true");
    // and a typed array does NOT grow: a write past the end is dropped
    expect_result("const a = new Uint8Array(2); a[5] = 1; return a.length;", "2");
    expect_result("return Uint16Array.BYTES_PER_ELEMENT;", "2");
    expect_result("const a = new Uint8Array(4); a.set([9, 8], 1); return a.join(',');", "0,9,8,0");
    expect_result("const a = new Uint8Array([1, 2, 3, 4]); return a.subarray(1, 3).join(',');",
                  "2,3");
}

void test_string_statics() {
    expect_result("return String.fromCharCode(104, 105);", "hi");
    expect_result("return String.fromCharCode.apply(null, [97, 98, 99]);", "abc");
    expect_result("return String(42);", "42");
    // above 0x7F encodes as UTF-8, because strings here are bytes
    expect_result("return String.fromCharCode(233).length;", "2");
}

// btoa and atob are BYTE oriented: btoa's argument is a binary string of values
// 0-255, not text. Treating it as text is how a page's exported image arrives
// truncated at the first byte that is not valid UTF-8.
void test_base64() {
    expect_result("return btoa('hello');", "aGVsbG8=");
    expect_result("return btoa('hi');", "aGk="); // the padding says how many bytes were real
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
    // Math.random aside, these are the functions every page reaches for. None
    // of them existed: `Math` itself was undefined.
    expect_result("return Math.floor(3.7);", "3");
    expect_result("return Math.ceil(3.2);", "4");
    expect_result("return Math.abs(-5);", "5");
    expect_result("return Math.max(1, 9, 4);", "9");
    expect_result("return Math.min(1, 9, 4);", "1");
    expect_result("return Math.sqrt(16);", "4");
    expect_result("return Math.pow(2, 10);", "1024");
    // JS rounds .5 toward POSITIVE infinity; std::round rounds away from zero
    // and would give -1 here.
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
    // `arr.push` resolved to nothing before the prototype table existed.
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
    // These are the ones that need context::call - a native invoking a JS
    // function. Without it none of them can exist at all.
    expect_result("var t = 0; [1,2,3].forEach(function (x) { t += x; }); return t;", "6");
    expect_result("return [1,2,3].map(function (x) { return x * 2; }).join(',');", "2,4,6");
    expect_result("return [1,2,3,4].filter(function (x) { return x % 2 == 0; }).join(',');", "2,4");
    expect_result("return [1,2,3,4].reduce(function (t, x) { return t + x; }, 0);", "10");
    expect_result("return [1,2,3].find(function (x) { return x > 1; });", "2");
    expect_result("return [1,2,3].some(function (x) { return x > 2; });", "true");
    expect_result("return [1,2,3].every(function (x) { return x > 0; });", "true");
    // The callback gets the index too, which half of real uses depend on.
    expect_result("var s = ''; ['a','b'].forEach(function (x, i) { s += i + x; }); return s;",
                  "0a1b");
}

void test_array_sort() {
    expect_result("return [3,1,2].sort(function (a, b) { return a - b; }).join('');", "123");
    expect_result("return [3,1,2].sort(function (a, b) { return b - a; }).join('');", "321");
    // The default really is lexicographic on the string form, which is why
    // [10, 9] sorts to [10, 9] and surprises everyone once.
    expect_result("return [10,9].sort().join(',');", "10,9");

    // --- what the merge sort has to keep -----------------------------------
    // It replaced a stable insertion sort that was O(n^2) - 104 ms at 4000
    // elements, tracking n^2 exactly. Speed is not what these check.

    // STABLE: equal keys keep their original order. The specification requires
    // it, and a merge that takes the right side on a tie would quietly break it
    // while still producing sorted output.
    expect_result("const xs = [['b',1],['a',1],['c',0],['d',1]];"
                  "xs.sort(function (p, q) { return p[1] - q[1]; });"
                  "return xs.map(function (p) { return p[0]; }).join('');",
                  "cbad");

    // Big enough that a quadratic sort would be visible, and checked by
    // CONTENT rather than by trusting the sort that produced it.
    expect_result("const a = []; let s = 12345;"
                  "for (let i = 0; i < 2000; i++) { s = (s * 1103515245 + 12345) & 2147483647;"
                  "  a.push(s % 1000); }"
                  "a.sort(function (x, y) { return x - y; });"
                  "let ok = a.length === 2000;"
                  "for (let i = 1; i < a.length; i++) { if (a[i-1] > a[i]) { ok = false; } }"
                  "return String(ok);",
                  "true");

    // AN INCONSISTENT COMPARATOR MUST NOT CORRUPT ANYTHING. This is why
    // std::sort is not used: it would be undefined behaviour. A merge reads
    // only inside two ranges it computed itself, so no answer can move an index
    // out of them - the result is unspecified, staying alive is not.
    expect_result("const a = [5,3,8,1,9,2,7];"
                  "a.sort(function () { return 1; });"
                  "return String(a.length);",
                  "7");
    expect_result("const a = [5,3,8,1,9,2,7];"
                  "a.sort(function () { return -1; });"
                  "return String(a.length);",
                  "7");

    // A COMPARATOR THAT MUTATES THE ARRAY IT IS SORTING. The old loop indexed
    // the live array while calling out, so this walked off the end; the merge
    // sorts a snapshot and writes it back, so the worst case is an unspecified
    // order.
    expect_result("const a = [4,2,5,1,3];"
                  "a.sort(function (x, y) { a.length = 0; return x - y; });"
                  "return 'survived';",
                  "survived");
    expect_result("const a = [4,2,5,1,3];"
                  "a.sort(function (x, y) { a.push(99); return x - y; });"
                  "return 'survived';",
                  "survived");

    // The default path now precomputes its string keys; it must still be the
    // same lexicographic order, and still stable.
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
    expect_result("return 'hello'.substring(3, 1);", "el"); // substring swaps
    expect_result("return '  hi  '.trim();", "hi");
    expect_result("return 'ab'.repeat(3);", "ababab");
    expect_result("return 'a-b-a'.replace('a', 'X');", "X-b-a");    // first only
    expect_result("return 'a-b-a'.replaceAll('a', 'X');", "X-b-X"); // all
    expect_result("return '5'.padStart(3, '0');", "005");
    expect_result("return 'abc'.charAt(1);", "b");
    expect_result("return 'A'.charCodeAt(0);", "65");
    expect_result("return 'abc'.startsWith('ab');", "true");
    expect_result("return 'abc'.length;", "3"); // still works alongside the methods
}

void test_number_and_conversions() {
    expect_result("return (3.14159).toFixed(2);", "3.14");
    expect_result("return parseInt('42');", "42");
    expect_result("return parseInt('ff', 16);", "255");
    expect_result("return parseFloat('3.5');", "3.5");
    // parseInt of nonsense is NaN, not an error - a page checks it with isNaN
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
                  "{\"s\":\"a\\nb\"}"); // escapes survive a round trip
}

// `entries`, `keys` and `values` on an array, and `entries` on a Set.
//
// Each is a REAL ITERATOR: it has `next` and `@@iterator`, its own state is not
// enumerable, and for..of and spread both walk it - the last because the object
// also carries the array under `__items`, which is what `iterable_values`
// recognises. They used to be bare Arrays, and a page that drove one by hand -
// Babylon does, for the Map of shadow generators a light owns - got
// "`next` is undefined".
void test_array_iterators() {
    // `{}` and not the pairs: an iterator has no enumerable own property, which
    // is also the guard that its `__items` and `__at` stay invisible.
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
    // A Set's entries pairs each member WITH ITSELF, which looks odd and is the
    // spec: it exists so a Set and a Map can be walked by the same code.
    expect_result("const s = new Set(['a']);"
                  "let out = ''; for (const [k, v] of s.entries()) { out += k + v; } return out;",
                  "aa");
    expect_result("return typeof new Map().entries;", "function");
}

// `Date` IS CONSTRUCTIBLE, and reads a calendar out of a millisecond count.
//
// It was a namespace with `now()` on it, so `new Date()` was "Date is not a
// function". p5 exposes day()/month()/year()/hour() and every one builds a
// Date, so a sketch showing a clock failed on its first line.
//
// UTC only and no string parsing - that is a timezone database and a different
// project. `new Date()` with no argument is the epoch, deliberately: the clock
// is fixed here for the same reason Math.random is seeded, so a page that draws
// from either can have a golden.
void test_date() {
    expect_result("const d = new Date(0);"
                  "return [d.getFullYear(), d.getMonth(), d.getDate(), d.getDay()].join(',');",
                  "1970,0,1,4");
    expect_result("return new Date(0).toISOString();", "1970-01-01T00:00:00.000Z");
    // (year, monthIndex, day, h, m, s) - the month is ZERO-BASED, which is the
    // wart every calendar bug starts with.
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
    expect_result("return Date.UTC(1970, 0, 2);", "86400000");
    expect_result("return new Date(0) instanceof Date;", "true");
    // valueOf, so a Date works in arithmetic - `end - start` is the reason it
    // exists.
    expect_result("return typeof (new Date(5000) - new Date(2000));", "number");
    expect_result("return new Date(5000) - new Date(2000);", "3000");
}

// AN ARRAYBUFFER IS SHARED STORAGE.
//
// It used to be a length and nothing else, so two views over one buffer were
// silently independent: a page that wrote through one and read through the
// other got zeroes. A view over the WHOLE of a buffer is now that storage
// rather than a copy of it, which is the entire reason a page wraps
// `await res.arrayBuffer()` in one.
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
    // A view made WITHOUT a buffer owns its own elements, as before.
    expect_result("const buf = new ArrayBuffer(2); const shared = new Uint8Array(buf);"
                  "const own = new Uint8Array(2); own[0] = 9; shared[0] = 1; return own[0];",
                  "9");
    // A SUB-RANGE view. This used to throw RangeError, because a view owned its
    // elements and an offset one could not have seen writes through the buffer.
    // A view views now, so it works and sees them.
    expect_result("const buf = new ArrayBuffer(4); const whole = new Uint8Array(buf);"
                  "const part = new Uint8Array(buf, 1, 2);"
                  "whole[1] = 5; whole[2] = 6; return part[0] + ',' + part[1] + ',' + part.length;",
                  "5,6,2");
    expect_result("const buf = new ArrayBuffer(4); const whole = new Uint8Array(buf);"
                  "const part = new Uint8Array(buf, 2, 2); part[0] = 9; return whole[2];",
                  "9");

    // VIEWS OF DIFFERENT WIDTHS OVER ONE BUFFER, which is the case that was
    // silently wrong: every view was handed the SAME array with its element
    // kind overwritten, so the last one made won and every earlier view's
    // writes were coerced to the wrong type. A float written through the F32
    // view came back as an integer - 64 read back as 9e-44, which is zero to
    // anything that asks. Phaser makes four such views over its vertex buffer,
    // which is why its WebGL renderer drew nothing at all.
    expect_result("const buf = new ArrayBuffer(8);"
                  "const f = new Float32Array(buf), u = new Uint32Array(buf);"
                  "const b = new Uint8Array(buf), h = new Uint16Array(buf);"
                  "f[0] = 64; return f[0];",
                  "64");
    // 64.0f is 0x42800000, so the bytes are readable through every other view.
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
    // A view's length is in ELEMENTS and its byteLength in bytes.
    expect_result("const buf = new ArrayBuffer(8); const f = new Float32Array(buf);"
                  "return f.length + ',' + f.byteLength + ',' + f.BYTES_PER_ELEMENT;",
                  "2,8,4");
    // `subarray` SHARES; it is not `slice`.
    expect_result("const buf = new ArrayBuffer(16); const f = new Float32Array(buf);"
                  "const tail = f.subarray(2); f[2] = 7; return tail[0] + ',' + tail.length;",
                  "7,2");
    // A view over a buffer another view reached through `.buffer`.
    expect_result("const f = new Float32Array(new ArrayBuffer(4));"
                  "const b = new Uint8Array(f.buffer); f[0] = 64; return b[3];",
                  "66");
    // Signed kinds reinterpret rather than wrap on READ.
    expect_result("const buf = new ArrayBuffer(4);"
                  "const u = new Uint8Array(buf), s = new Int8Array(buf);"
                  "u[0] = 200; return s[0];",
                  "-56");
}

// `replace` WITH A REGEXP - which is what the method is mostly for, and which
// did not work at all.
//
// The pattern was coerced with to_string and looked for with std::string::find,
// so `s.replace(/ /g, '|')` searched for the literal text of a regex object and,
// finding none, returned the string unchanged. No error, no clue.
//
// It is not a corner: acorn builds its keyword tables with
// `new RegExp('^(?:' + words.replace(/ /g, '|') + ')$')`, so every keyword
// matcher inside the parser p5 bundles was a pattern that could never match -
// and p5's error system reported a SyntaxError on ordinary code because of it.
void test_string_replace() {
    // The whole reason the bug was invisible: with a STRING pattern it was right.
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
    // A FUNCTION replacement is called with (match, p1..pn, offset, whole).
    expect_result("return 'a1b2'.replace(/\\d/g, function (m) { return '<' + m + '>'; });",
                  "a<1>b<2>");
    expect_result("return 'xy'.replace(/(x)(y)/, function (m, p1, p2, off, whole) {"
                  "  return p2 + p1 + off + whole.length; });",
                  "yx02");
    // Anchors and greedy groups, which is the shape p5 builds its CDN url with.
    expect_result("return '1.2.3-x'.replace(/^(\\d+\\.\\d+)\\.\\d+.*$/, '$1');", "1.2");
    // The idiom that broke acorn.
    expect_result("return new RegExp('^(?:' + 'break case function'.replace(/ /g, '|') + ')$')"
                  "  .test('function');",
                  "true");
    // An empty match still advances rather than looping forever.
    expect_result("return 'ab'.replace(/x*/g, '-');", "-a-b-");
}

// `String.prototype.match` - the commonest thing done with a regular
// expression, and it simply was not here. The two forms return DIFFERENT
// SHAPES, which is what code branches on.
void test_string_match() {
    expect_result("const m = 'a1b22'.match(/([a-z])(\\d+)/);"
                  "return m[0] + ',' + m[1] + ',' + m[2] + ',' + m.index;",
                  "a1,a,1,0");
    // With `g` it is a flat list of matched strings and nothing else.
    expect_result("return 'a1b22'.match(/\\d+/g).join('|');", "1|22");
    // No match is NULL in both forms - the usual guard is `if (m)`, and an
    // empty array is truthy.
    expect_result("return 'xyz'.match(/\\d/) === null;", "true");
    expect_result("return 'xyz'.match(/\\d/g) === null;", "true");
}

// `structuredClone` - a deep copy of plain data, including through a cycle.
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
}

} // namespace

int main() {
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
