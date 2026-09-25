// RegExp.prototype as 22.2.6 writes it: the flag accessors, `source`, `flags`,
// and the five Symbol methods that String.prototype's match, matchAll,
// replace, replaceAll, search and split INVOKE rather than reimplement. Each
// assertion below is one that test262's built-ins/String or built-ins/RegExp
// rows failed on 2026-09-12 while the model was six own data properties and
// a per-method regex loop in string.cpp.

#include "vm_expect.hpp"

namespace {

void test_accessors() {
    // The instance owns lastIndex and nothing a page can see; everything else
    // is an accessor on the prototype.
    expect_result("return Object.getOwnPropertyNames(/a/g).join();", "lastIndex");
    expect_result("return Object.keys(/a/g).length;", "0");
    expect_result("const d = Object.getOwnPropertyDescriptor(RegExp.prototype, 'global');"
                  "return typeof d.get + ',' + d.set + ',' + d.enumerable + ',' + d.configurable;",
                  "function,undefined,false,true");
    expect_result("return /a/dgimsuy.flags;", "dgimsuy");
    expect_result("return /a/.hasIndices + ',' + /a/s.dotAll + ',' + /a/v.unicodeSets;",
                  "false,true,true");
    // `flags` is generic: it reads the booleans through [[Get]]
    expect_result("return Object.getOwnPropertyDescriptor(RegExp.prototype, 'flags').get"
                  ".call({global: 1, sticky: 'y'});",
                  "gy");
    // RegExp.prototype itself answers undefined and "(?:)", not a TypeError
    expect_result("return RegExp.prototype.global + '|' + RegExp.prototype.source;",
                  "undefined|(?:)");
    expect_result("try { Object.getOwnPropertyDescriptor(RegExp.prototype, 'global').get.call({});"
                  " return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
    // EscapeRegExpPattern
    expect_result("return new RegExp('').source + ' ' + new RegExp('a/b').source + ' ' + "
                  "new RegExp('\\n').source;",
                  "(?:) a\\/b \\n");
    expect_result("return RegExp.prototype.toString.call({source: 'a', flags: 'g'});", "/a/g");
}

void test_constructor() {
    // 22.2.4.1 step 4: RegExp(re) with no flags IS re
    expect_result("const re = /a/g; return RegExp(re) === re;", "true");
    expect_result("const re = /a/g; return new RegExp(re) === re;", "false");
    expect_result("return new RegExp(/a/g).flags + new RegExp(/a/g, 'i').flags;", "gi");
    // something that claims to be a RegExp is read through [[Get]]
    expect_result("return new RegExp({[Symbol.match]: true, source: 'b+', flags: 'g'})"
                  ".exec('abbb')[0];",
                  "bbb");
    expect_result("try { new RegExp('a', 'gg'); return 'no'; } catch (e) { return e.name; }",
                  "SyntaxError");
    expect_result("try { new RegExp('a', 'uv'); return 'no'; } catch (e) { return e.name; }",
                  "SyntaxError");
    expect_result("return Object.getOwnPropertyDescriptor(/a/, 'lastIndex').writable + ',' + "
                  "Object.getOwnPropertyDescriptor(/a/, 'lastIndex').enumerable;",
                  "true,false");
    // a frozen pattern refuses the lastIndex write with a TypeError
    expect_result("const re = Object.freeze(/a/g); try { re.exec('a'); return 'no'; } "
                  "catch (e) { return e.name; }",
                  "TypeError");
    expect_result("return RegExp[Symbol.species] === RegExp;", "true");
    // RegExp.escape (ES2025): a leading alphanumeric is hex-escaped, syntax
    // characters backslashed, other punctuators and spaces \xHH
    expect_result("return RegExp.escape('1a.b') + ' ' + RegExp.escape('x-y z');",
                  "\\x31a\\.b \\x78\\x2dy\\x20z");
    // (Spelled as JS escapes: a raw line break inside a string literal is a
    // SyntaxError, 12.9.4.)
    expect_result("return RegExp.escape('\\t\\n');", "\\t\\n");
    expect_result("try { RegExp.escape(1); return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
}

void test_exec() {
    // lastIndex is read through ToLength: a string, a negative, a huge one
    expect_result("const re = /a/g; re.lastIndex = '1'; return re.exec('aa').index;", "1");
    expect_result("const re = /a/g; re.lastIndex = -5; return re.exec('aa').index;", "0");
    expect_result("const re = /a/g; re.lastIndex = 9; return re.exec('aa') + ',' + re.lastIndex;",
                  "null,0");
    // a non-global, non-sticky pattern ignores lastIndex and leaves it alone
    expect_result("const re = /a/; re.lastIndex = 7; re.exec('a'); return re.lastIndex;", "7");
    // sticky matches at lastIndex or not at all
    expect_result("const re = /b/y; return re.exec('ab') + ',' + re.lastIndex;", "null,0");
    expect_result("const re = /b/y; re.lastIndex = 1; return re.exec('ab')[0] + re.lastIndex;",
                  "b2");
    // groups is undefined without named groups, null-prototype with them
    expect_result("return /(a)/.exec('a').groups;", "undefined");
    expect_result("const g = /(?<x>a)/.exec('a').groups; return Object.getPrototypeOf(g) + ',' + "
                  "g.x + ',' + ('toString' in g);",
                  "null,a,false");
    // `d`: indices
    expect_result("const m = /b(?<c>c)/d.exec('abc'); return m.indices[0] + '|' + m.indices[1] + "
                  "'|' + m.indices.groups.c;",
                  "1,3|2,3|2,3");
    // a repetition clears the captures of the group it repeats
    expect_result("return /(?:(a)|(b))+/.exec('ab')[1];", "undefined");
    expect_result("return /(a)|(b)/.exec('b')[1];", "undefined");
    // the s flag, and the default . refusing both line terminators
    expect_result("return /a.b/.test('a\\nb') + '' + /a.b/s.test('a\\nb') + /a.b/.test('a\\rb');",
                  "falsetruefalse");
    // a class and the dot consume a whole UTF-8 code point, and a match never
    // starts inside one
    expect_result("return /[^\u{1F49A}]/u.exec('\u{1F49A}') + '|' + 'é'.match(/./)[0] + '|' + "
                  "/(.+).*\\1/u.test('\\ud800\\udc00\\ud800');",
                  "null|é|false");
    // named backreference, and Annex B's octal for a number past the groups
    expect_result("return /(?<q>['\"]).*\\k<q>/.exec('say \"hi\" now')[0];", "\"hi\"");
    expect_result("return /\\1/.test('\\u0001') + '' + /\\8/.test('8');", "truetrue");
}

void test_symbol_methods() {
    // String.prototype.match with g is the list of matched texts; without,
    // the exec result
    expect_result("return 'a1b2'.match(/\\d/g).join() + '|' + 'a1b2'.match(/\\d/).index;", "1,2|1");
    expect_result("return 'abc'.match(/x/g);", "null");
    // an empty match advances by one, so a global match over the empty
    // pattern terminates with one entry per position
    expect_result("return 'ab'.match(/(?:)/g).length;", "3");
    // replace: the template language, $<name>, and the function form's args
    expect_result("return 'abc'.replace(/b/, '[$`|$&|$\\']');", "a[a|b|c]c");
    expect_result("return 'abc'.replace(/(?<mid>b)/, '<$<mid>>');", "a<b>c");
    expect_result("return 'abc'.replace(/b/, '<$<mid>>');", "a<$<mid>>c");
    expect_result("return 'x'.replace(/x/, '$1');", "$1");
    expect_result("return 'abcdefghijkl'.replace(/(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)/, '$11$1');",
                  "kal");
    expect_result("return 'abc'.replace(/(?<n>b)/, (...args) => typeof args[args.length - 1]);",
                  "aobjectc");
    expect_result("return 'aaa'.replace(/a/g, (m, i) => i);", "012");
    // replaceAll: string form finds every position, including with an empty
    // search string; a non-global RegExp is refused
    expect_result("return 'abc'.replaceAll('', '_');", "_a_b_c_");
    expect_result("return 'aaa'.replaceAll('a', '$&$&');", "aaaaaa");
    expect_result("try { 'a'.replaceAll(/a/, 'b'); return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
    expect_result("return 'a.b.c'.replaceAll(/\\./g, '-');", "a-b-c");
    // split through @@split: the sticky walk keeps captures and honours limit
    expect_result("return 'x'.split(/^/).join('|');", "x");
    expect_result("return 'a1b2c'.split(/(\\d)/).join('|');", "a|1|b|2|c");
    expect_result("return 'hello'.split(/l/, 2).join('|');", "he|");
    expect_result("return 'ab'.split(/(?:)/).join('|');", "a|b");
    expect_result("return ''.split(/x/).length + ',' + ''.split(/(?:)/).length;", "1,0");
    // search: lastIndex is restored afterwards
    expect_result(
        "const re = /b/g; re.lastIndex = 5; return 'abc'.search(re) + ',' + re.lastIndex;", "1,5");
    // matchAll: a live iterator over a species copy, the original untouched
    expect_result("const re = /a/g; const it = 'aa'.matchAll(re); const a = it.next(); "
                  "return a.value.index + ',' + re.lastIndex + ',' + it.next().value.index + ',' + "
                  "it.next().done;",
                  "0,0,1,true");
    expect_result("return [...'a1b2'.matchAll(/\\d/g)].map(m => m[0] + m.index).join();", "11,23");
    expect_result("return Object.prototype.toString.call('a'.matchAll(/a/g));",
                  "[object RegExp String Iterator]");
    // a page's own @@ methods are honoured, and a non-object result from a
    // custom exec is refused
    expect_result("return 'abc'.replace({[Symbol.replace]: (s, r) => s + r}, 'X');", "abcX");
    expect_result("const re = /a/; re.exec = () => 1; try { re.test('a'); return 'no'; } "
                  "catch (e) { return e.name; }",
                  "TypeError");
    expect_result("const re = /a/; re.exec = () => null; return re.test('a');", "false");
}

void test_substitution_limits() {
    // Small inputs expand past the existing 2^28-byte string ceiling. Size
    // preflight must reject them without allocating that result, and numbered
    // captures are coerced once before expanding the template.
    expect_result("let calls = 0; const text = 'x'.repeat(65536); const re = /x/;"
                  "re.exec = () => ({0: 'x', 1: {toString() { ++calls; return text; }},"
                  "length: 2, index: 0});"
                  "try { 'x'.replace(re, '$1'.repeat(4097)); return 'no'; }"
                  "catch (e) { return e.name + ',' + calls; }",
                  "RangeError,1");
    expect_result("const text = 'x'.repeat(65536);"
                  "try { text.replace('', \"$'\".repeat(4097)); return 'no'; }"
                  "catch (e) { return e.name; }",
                  "RangeError");
    // Named captures remain observable once per occurrence, in template order.
    expect_result("let log = ''; let gets = 0; const re = /x/;"
                  "const groups = {get n() { log += 'g'; const n = ++gets;"
                  "return {toString() { log += 's'; return '' + n; }}; }};"
                  "re.exec = () => ({0: 'x', 1: {toString() { log += 'c'; return 'v'; }},"
                  "length: 2, index: 0, groups});"
                  "const result = 'x'.replace(re, '$1$<n>$1$<n>');"
                  "return result + '|' + log;",
                  "v1v2|cgsgs");
    expect_result("const text = 'x'.repeat(65536); const re = /x/;"
                  "re.exec = () => ({0: 'x', 1: text, length: 2, index: 0,"
                  "groups: {get n() { throw 17; }}});"
                  "try { 'x'.replace(re, '$<n>' + '$1'.repeat(4097)); return 'no'; }"
                  "catch (e) { return e; }",
                  "17");
}

} // namespace

int main() {
    test_accessors();
    test_constructor();
    test_exec();
    test_symbol_methods();
    test_substitution_limits();
    REPORT("regexp_model");
}
