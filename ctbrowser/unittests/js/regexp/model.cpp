//===- ctbrowser/unittests/js/regexp/model.cpp - RegExp.prototype tests ---===//
//
// RegExp.prototype as ECMA-262 22.2.6 defines it: the flag accessors,
// `source`, `flags`, and the five Symbol methods that String.prototype's
// match, matchAll, replace, replaceAll, search and split call rather than
// reimplement. Each assertion reproduces a test262 built-ins/String or
// built-ins/RegExp failure. The matcher's own limits are tested through
// ctbrowser::script::rx directly.
//
//===----------------------------------------------------------------------===//

#include "../vm/expect.hpp"

#include <ctbrowser/script/regex.hpp>

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
    // `flags` is generic: it reads the booleans through [[Get]].
    expect_result("return Object.getOwnPropertyDescriptor(RegExp.prototype, 'flags').get"
                  ".call({global: 1, sticky: 'y'});",
                  "gy");
    // RegExp.prototype itself answers undefined and "(?:)", not a TypeError.
    expect_result("return RegExp.prototype.global + '|' + RegExp.prototype.source;",
                  "undefined|(?:)");
    expect_result("try { Object.getOwnPropertyDescriptor(RegExp.prototype, 'global').get.call({});"
                  " return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
    // EscapeRegExpPattern.
    expect_result("return new RegExp('').source + ' ' + new RegExp('a/b').source + ' ' + "
                  "new RegExp('\\n').source;",
                  "(?:) a\\/b \\n");
    expect_result("return RegExp.prototype.toString.call({source: 'a', flags: 'g'});", "/a/g");
}

void test_constructor() {
    // ECMA-262 22.2.4.1 step 4: RegExp(re) with no flags is re.
    expect_result("const re = /a/g; return RegExp(re) === re;", "true");
    expect_result("const re = /a/g; return new RegExp(re) === re;", "false");
    expect_result("return new RegExp(/a/g).flags + new RegExp(/a/g, 'i').flags;", "gi");
    // Something that claims to be a RegExp is read through [[Get]].
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
    // A frozen pattern refuses the lastIndex write with a TypeError.
    expect_result("const re = Object.freeze(/a/g); try { re.exec('a'); return 'no'; } "
                  "catch (e) { return e.name; }",
                  "TypeError");
    expect_result("return RegExp[Symbol.species] === RegExp;", "true");
    // RegExp.escape (ES2025): a leading alphanumeric is hex-escaped, syntax
    // characters backslashed, other punctuators and spaces \xHH.
    expect_result("return RegExp.escape('1a.b') + ' ' + RegExp.escape('x-y z');",
                  "\\x31a\\.b \\x78\\x2dy\\x20z");
    // (Spelled as JS escapes: a raw line break inside a string literal is a
    // SyntaxError, ECMA-262 12.9.4.)
    expect_result("return RegExp.escape('\\t\\n');", "\\t\\n");
    expect_result("try { RegExp.escape(1); return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
}

void test_exec() {
    // lastIndex is read through ToLength: a string, a negative, a huge one.
    expect_result("const re = /a/g; re.lastIndex = '1'; return re.exec('aa').index;", "1");
    expect_result("const re = /a/g; re.lastIndex = -5; return re.exec('aa').index;", "0");
    expect_result("const re = /a/g; re.lastIndex = 9; return re.exec('aa') + ',' + re.lastIndex;",
                  "null,0");
    // A non-global, non-sticky pattern ignores lastIndex and leaves it alone.
    expect_result("const re = /a/; re.lastIndex = 7; re.exec('a'); return re.lastIndex;", "7");
    // Sticky matches at lastIndex or not at all.
    expect_result("const re = /b/y; return re.exec('ab') + ',' + re.lastIndex;", "null,0");
    expect_result("const re = /b/y; re.lastIndex = 1; return re.exec('ab')[0] + re.lastIndex;",
                  "b2");
    // groups is undefined without named groups, null-prototype with them.
    expect_result("return /(a)/.exec('a').groups;", "undefined");
    expect_result("const g = /(?<x>a)/.exec('a').groups; return Object.getPrototypeOf(g) + ',' + "
                  "g.x + ',' + ('toString' in g);",
                  "null,a,false");
    // `d`: indices.
    expect_result("const m = /b(?<c>c)/d.exec('abc'); return m.indices[0] + '|' + m.indices[1] + "
                  "'|' + m.indices.groups.c;",
                  "1,3|2,3|2,3");
    // A repetition clears the captures of the group it repeats.
    expect_result("return /(?:(a)|(b))+/.exec('ab')[1];", "undefined");
    expect_result("return /(a)|(b)/.exec('b')[1];", "undefined");
    // The s flag, and the default . refusing both line terminators.
    expect_result("return /a.b/.test('a\\nb') + '' + /a.b/s.test('a\\nb') + /a.b/.test('a\\rb');",
                  "falsetruefalse");
    // A class and the dot consume a whole UTF-8 code point, and a match never
    // starts inside one.
    expect_result("return /[^\u{1F49A}]/u.exec('\u{1F49A}') + '|' + 'é'.match(/./)[0] + '|' + "
                  "/(.+).*\\1/u.test('\\ud800\\udc00\\ud800');",
                  "null|é|false");
    // Named backreference, and Annex B's octal for a number past the groups.
    expect_result("return /(?<q>['\"]).*\\k<q>/.exec('say \"hi\" now')[0];", "\"hi\"");
    expect_result("return /\\1/.test('\\u0001') + '' + /\\8/.test('8');", "truetrue");
}

void test_symbol_methods() {
    // String.prototype.match with g is the list of matched texts; without,
    // the exec result.
    expect_result("return 'a1b2'.match(/\\d/g).join() + '|' + 'a1b2'.match(/\\d/).index;", "1,2|1");
    expect_result("return 'abc'.match(/x/g);", "null");
    // An empty match advances by one, so a global match over the empty
    // pattern terminates with one entry per position.
    expect_result("return 'ab'.match(/(?:)/g).length;", "3");
    // replace: the template language, $<name>, and the function form's args.
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
    // search string; a non-global RegExp is refused.
    expect_result("return 'abc'.replaceAll('', '_');", "_a_b_c_");
    expect_result("return 'aaa'.replaceAll('a', '$&$&');", "aaaaaa");
    expect_result("try { 'a'.replaceAll(/a/, 'b'); return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
    expect_result("return 'a.b.c'.replaceAll(/\\./g, '-');", "a-b-c");
    // split through @@split: the sticky walk keeps captures and honours limit.
    expect_result("return 'x'.split(/^/).join('|');", "x");
    expect_result("return 'a1b2c'.split(/(\\d)/).join('|');", "a|1|b|2|c");
    expect_result("return 'hello'.split(/l/, 2).join('|');", "he|");
    expect_result("return 'ab'.split(/(?:)/).join('|');", "a|b");
    expect_result("return ''.split(/x/).length + ',' + ''.split(/(?:)/).length;", "1,0");
    // search: lastIndex is restored afterwards.
    expect_result(
        "const re = /b/g; re.lastIndex = 5; return 'abc'.search(re) + ',' + re.lastIndex;", "1,5");
    // matchAll: a live iterator over a species copy, the original untouched.
    expect_result("const re = /a/g; const it = 'aa'.matchAll(re); const a = it.next(); "
                  "return a.value.index + ',' + re.lastIndex + ',' + it.next().value.index + ',' + "
                  "it.next().done;",
                  "0,0,1,true");
    expect_result("return [...'a1b2'.matchAll(/\\d/g)].map(m => m[0] + m.index).join();", "11,23");
    expect_result("return Object.prototype.toString.call('a'.matchAll(/a/g));",
                  "[object RegExp String Iterator]");
    // A page's own @@ methods are honoured, and a non-object result from a
    // custom exec is refused.
    expect_result("return 'abc'.replace({[Symbol.replace]: (s, r) => s + r}, 'X');", "abcX");
    expect_result("const re = /a/; re.exec = () => 1; try { re.test('a'); return 'no'; } "
                  "catch (e) { return e.name; }",
                  "TypeError");
    expect_result("const re = /a/; re.exec = () => null; return re.test('a');", "false");
}

void test_substitution_limits() {
    // Small inputs can expand past the 2^28-byte (256 MiB) string ceiling. A
    // size preflight must reject them without allocating that result, and numbered
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

/// The pieces of the grammar that are easy to misread: `\cX`, a
/// class's `[\b]`, a quantifier bound past int32, and the RepeatMatcher rule
/// that an iteration matching nothing fails once the minimum is met.
void test_escapes_and_repetition() {
    expect_result("return /\\cJ/.test('\\n') + ',' + /[\\cJ]/.test('\\n');", "true,true");
    expect_result("return /\\c1/.test('\\\\c1');", "true");
    expect_result("return /[\\b]/.test('\\b') + ',' + /[\\b]/.test('b');", "true,false");
    // Annex B's legacy octal escape in a class: [\12-\14] is U+000A-U+000C.
    expect_result("return /[\\12-\\14]/.test('\\x0b') + ',' + /[\\12-\\14]/.test('1');",
                  "true,false");
    expect_result("return /(a?b?" /* not a trigraph */ "?)*/.exec('ab')[0];", "ab");
    expect_result("return String('abc'.match(/(?:(?=(abc)))?a/)[1]);", "undefined");
    expect_result("return 'abc'.match(/(?:(?=(abc))){1,1}a/)[1];", "abc");
    expect_result("return new RegExp('b{' + Number.MAX_SAFE_INTEGER + '}').test('b');", "false");
    // A lookbehind is matched right to left: greedy nearest the position,
    // and the captures that reading gives.
    expect_result("return /(?<=(\\d+)(\\d+))$/.exec('1053').slice(1).join();", "1,053");
    expect_result("return /(?<=\\$\\d+\\.)\\d+/.exec('cost $10.53')[0];", "53");
    expect_result("return /(?<!abc)\\w\\w\\w/.exec('abcdef')[0];", "abc");
    expect_result("return /(?<=(b+))c/.exec('abbbbbbc')[1];", "bbbbbb");
    // An exec() result's index, input and groups are own data properties.
    expect_result(
        "const m = /(?<x>a)/.exec('a'); delete m.input;"
        "return m.hasOwnProperty('groups') + ',' + Object.keys(m).join() + ',' + m.input;",
        "true,0,1,index,groups,undefined");
}

/// Annex B's legacy RegExp features: the static accessors RegExpBuiltinExec
/// updates, refused for any receiver but %RegExp% and emptied by a subclass
/// instance's match; compile refuses a subclass instance.
void test_legacy_statics() {
    expect_result("/(b)(c)?/.exec('abd'); return [RegExp.lastMatch, RegExp['$1'], "
                  "RegExp['$2'], RegExp.lastParen, RegExp.leftContext, RegExp.rightContext, "
                  "RegExp.input, RegExp['$_'], RegExp['$9']].join('|');",
                  "b|b|||a|d|abd|abd|");
    expect_result("/x/.test('axb'); RegExp.input = 'q'; return RegExp.input + RegExp['$`'] + "
                  "RegExp[\"$'\"];",
                  "qab");
    expect_result("const d = Object.getOwnPropertyDescriptor(RegExp, '$&');"
                  "return typeof d.get + ',' + d.set + ',' + d.enumerable + ',' + d.configurable;",
                  "function,undefined,false,true");
    expect_result("class R extends RegExp {} try { R.lastMatch; return 'no'; } "
                  "catch (e) { return e.name; }",
                  "TypeError");
    expect_result("class R extends RegExp {} new R('a').exec('a');"
                  "try { RegExp.lastMatch; return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
    expect_result("class R extends RegExp {} try { new R('a').compile('b'); return 'no'; } "
                  "catch (e) { return e.name; }",
                  "TypeError");
    expect_result("return /a/.compile('b').source;", "b");
    // Annex B (B.1.2): `\0` with octal digits is a legacy octal escape, and a
    // class escape at the far end of a dash is no range.
    expect_result("return [/\\07/.test('\\x07'), /\\0111/.exec('\\x091')[0].length,"
                  " /[%-\\d]+/.exec('&%0123456789-&')[0], /[--\\d]+/.exec('.-01-.')[0]].join();",
                  "true,2,%0123456789-,-01-");
    expect_result("try { new RegExp('[%-\\\\d]', 'u'); return 'no'; } catch (e) { return e.name; }",
                  "SyntaxError");
}

/// The matcher recurses and backtracks; its budget (rx_max_depth,
/// rx_max_steps) and the reader's nesting cap (rx_max_nesting) make a hostile
/// pattern or subject a failed match instead of a stack overflow or a hang.
void test_limits() {
    namespace rx = ctbrowser::script::rx;
    const auto search = [](std::string_view pattern, std::string_view flags,
                           const std::string & subject) {
        const rx::rx_prog p = rx::rx_compile(pattern, flags);
        rx::rx_match m;
        const bool hit = p.ok && rx::rx_search(p, subject, 0, m);
        return std::pair{hit, m.exhausted};
    };
    // 200k repetitions of a quantified group exhaust the budget instead of
    // overflowing the stack.
    CHECK((search("^(?:a|b)+$", "", std::string(200000, 'a')) == std::pair{false, true}));
    // Catastrophic backtracking: 2^40 ways to fail, stopped by the step budget.
    CHECK((search("(a+)+$", "", std::string(40, 'a') + "b") == std::pair{false, true}));
    // Given up inside a negative lookahead is not "the lookahead failed".
    CHECK((search("(?!(a+)+$)", "", std::string(40, 'a') + "b") == std::pair{false, true}));
    // A quantified character is a loop: one step however long the subject.
    CHECK((search("a*", "", std::string(std::size_t{1} << 20, 'a')) == std::pair{true, false}));
    CHECK(
        (search("^[ab]+$", "", std::string(std::size_t{1} << 20, 'a')) == std::pair{true, false}));
    CHECK((search("^(?:a|b)+$", "", std::string(500, 'b')) == std::pair{true, false}));
    CHECK((search("(a+)+$", "", std::string(20, 'a')) == std::pair{true, false}));
    // Nesting: 256 levels read, 30000 are refused rather than recursed into.
    const auto nested = [](char open, char close, std::size_t n) {
        return std::string(n, open) + "a" + std::string(n, close);
    };
    CHECK(rx::rx_compile(nested('(', ')', 250), "").ok);
    CHECK(rx::rx_compile(nested('[', ']', 250), "v").ok);
    CHECK(!rx::rx_compile(nested('(', ')', 30000), "").ok);
    CHECK(!rx::rx_compile(nested('(', ')', 30000), "v").ok);
    CHECK(!rx::rx_compile(nested('[', ']', 30000), "v").ok);
    // In script: a pattern too deep is a SyntaxError, and a search that gave
    // up has no answer: a RangeError, never null.
    expect_result("try { new RegExp('['.repeat(30000) + ']'.repeat(30000), 'v'); return 'no'; }"
                  " catch (e) { return e.name; }",
                  "SyntaxError");
    expect_result("try { new RegExp('('.repeat(30000) + ')'.repeat(30000)); return 'no'; }"
                  " catch (e) { return e.name; }",
                  "SyntaxError");
    expect_result("try { /^(?:a|b)+$/.test('a'.repeat(200000)); return 'no'; }"
                  " catch (e) { return e.name; }",
                  "RangeError");
    expect_result("try { /(a+)+$/.exec('a'.repeat(40) + 'b'); return 'no'; }"
                  " catch (e) { return e.name; }",
                  "RangeError");
    expect_result("try { 'a'.repeat(40).concat('b').replace(/(a+)+$/, ''); return 'no'; }"
                  " catch (e) { return e.name; }",
                  "RangeError");
    // A budget per search too: ~n steps at each of n starts is n^2/2 steps.
    CHECK((search("a*b", "", std::string(4000, 'a')) == std::pair{false, false}));
    CHECK((search("a*b", "", std::string(12000, 'a')) == std::pair{false, true}));
    CHECK((search("x", "", std::string(std::size_t{1} << 22, 'a')) == std::pair{false, false}));
    expect_result("return /a*/.exec('a'.repeat(1 << 20))[0].length;", "1048576");
}

} // namespace

int main() {
    test_legacy_statics();
    test_accessors();
    test_constructor();
    test_exec();
    test_symbol_methods();
    test_substitution_limits();
    test_escapes_and_repetition();
    test_limits();
    REPORT("regexp_model");
}
