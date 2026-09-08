// The VM's expressions: operators, coercion, literals, and what a value says it
// is. Carved out of js/vm_basics.cpp on 2026-09-08, when that file was 3,047
// lines; the five other vm_*.cpp files beside this one are the rest of it, and
// vm_expect.hpp is the assertion they all share.
//
// THIS IS THE FILE THAT STILL RUNS ctjs. `diff_vs_v1` sends an expression
// through the previous engine's interpreter as well as through the VM, and it
// is the only reason any test here links the ctjs headers - so every test that
// uses it lives here, and unittests/CMakeLists.txt adds those include
// directories to this target alone.
//
// ctjs the engine VM.
//
// Every case here is checked TWICE: once against a hand-written expectation,
// and once against the previous engine's tree-walk interpreter running the same source
// (diff_vs_v1). The second check is the valuable one and it exists only while
// both engines are in the tree - a rewrite that can be differentially tested
// against the thing it replaces should be, and the window closes when the previous engine goes.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include "vm_expect.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <ctjs.hpp> // the previous engine, for the differential comparison

using namespace ctbrowser::script;

namespace {

// The same source through the previous engine's interpreter, for comparison.
//
// The explicit collect() is the previous engine's requirement, not a courtesy: the previous engine
// refcounts scope environments and those form cycles, so a run leaks its environment chain until
// the cycle collector reclaims it. the previous engine's engine calls this on a frame counter; a
// test that runs hundreds of scripts and never does is what LeakSanitizer reported, and the leak is
// the previous engine's design rather than a the engine bug. The new VM does not need this - it
// traces from precise roots instead.
[[nodiscard]] std::string run_ctjs(std::string_view source, bool * ok = nullptr) {
    std::string out;
    {
        ctjs::run_result r = ctjs::run_value(source, {});
        if (ok != nullptr) { *ok = r.ok(); }
        out = r.ok() ? r["__result"].to_string() : "<error>";
    } // the result holds roots, so it has to die before the collector runs
    ctjs::gc::collect();
    return out;
}

// Wrap an expression so both engines expose its value the same way: the engine returns
// the last `return`, the previous engine exposes globals.
void diff_vs_v1(std::string_view expression, std::string_view expected) {
    const std::string vm_src = "return (" + std::string{expression} + ");";
    const std::string ctjs_src = "let __result = (" + std::string{expression} + ");";

    bool vm_ok = false;
    bool ctjs_ok = false;
    const std::string got_vm = run_vm(vm_src, &vm_ok);
    const std::string got_ctjs = run_ctjs(ctjs_src, &ctjs_ok);

    if (got_vm != expected) {
        std::printf("FAIL     %.60s => %s (want %s)\n", std::string{expression}.c_str(),
                    got_vm.c_str(), std::string{expected}.c_str());
        ++ctbrowser_test_failures;
    }
    // ctjs's own interpreter disagreeing is worth reporting but is not
    // automatically the VM's fault - print both so the difference is visible
    // rather than silently accepted.
    if (ctjs_ok && got_ctjs != got_vm) {
        std::printf("DIFF     %.60s => vm=%s ctjs=%s\n", std::string{expression}.c_str(),
                    got_vm.c_str(), got_ctjs.c_str());
        ++ctbrowser_test_failures;
    }
}

void test_arithmetic() {
    diff_vs_v1("1 + 2", "3");
    diff_vs_v1("10 - 4 * 2", "2");
    diff_vs_v1("(10 - 4) * 2", "12");
    diff_vs_v1("7 / 2", "3.5");
    diff_vs_v1("7 % 3", "1");
    diff_vs_v1("2 ** 10", "1024");
    diff_vs_v1("-5 + 3", "-2");
}

// `+` is the one operator whose meaning depends on its operand types.
void test_plus_is_overloaded() {
    diff_vs_v1("'a' + 'b'", "ab");
    diff_vs_v1("'n=' + 42", "n=42");
    diff_vs_v1("1 + '2'", "12");     // string wins
    diff_vs_v1("1 + 2 + '3'", "33"); // ...but only once it appears
    diff_vs_v1("'1' + 2 + 3", "123");
}

void test_comparison_and_logic() {
    diff_vs_v1("1 < 2", "true");
    diff_vs_v1("2 <= 2", "true");
    diff_vs_v1("3 > 4", "false");
    diff_vs_v1("1 === 1", "true");
    diff_vs_v1("1 === '1'", "false"); // strict
    diff_vs_v1("1 == '1'", "true");   // loose coerces
    diff_vs_v1("null == undefined", "true");
    diff_vs_v1("null === undefined", "false");
    diff_vs_v1("!true", "false");
    diff_vs_v1("true && false", "false");
    diff_vs_v1("false || 'fallback'", "fallback");
    // short-circuit: the right side must not be evaluated at all
    expect_result("let hit = 0; function bump() { hit = 1; return true; } "
                  "let r = false && bump(); return hit;",
                  "0");
}

// `??` ASKS A DIFFERENT QUESTION FROM `||`, and it used to be compiled as one.
//
// Every case below where the left side is falsy-but-present returned the RIGHT
// side, silently: `0 ?? 5` was 5 and `"" ?? "x"` was "x". p5.js has 168 of
// these, and a default that overrides a real 0 is the kind of wrong that
// surfaces as a drawing being in the wrong place rather than as an error.
void test_nullish_is_not_falsy() {
    diff_vs_v1("0 ?\? 5", "0");
    diff_vs_v1("'' ?\? 'x'", "");
    diff_vs_v1("false ?\? true", "false");
    diff_vs_v1("NaN ?\? 1", "NaN");
    diff_vs_v1("null ?\? 5", "5");
    diff_vs_v1("undefined ?\? 5", "5");
    // and it still short-circuits: the right side is not evaluated at all
    expect_result("let hit = 0; function bump() { hit = 1; return 9; } "
                  "let r = 0 ?\? bump(); return hit;",
                  "0");
    // the assignment form asks the same question
    expect_result("let a = 0; a ?\?= 5; return a;", "0");
    expect_result("let b = null; b ?\?= 5; return b;", "5");
    expect_result("let c = ''; c ?\?= 'x'; return c;", "");
}

// A RADIX PREFIX USED TO EVALUATE TO ZERO.
//
// std::from_chars in `general` format stops at the `x`, so `0xFF` parsed as 0
// and the rest was discarded - with no error at any stage. There are 734 hex
// literals in p5.js, in colour maths, bit masks and font tables.
void test_radix_literals() {
    diff_vs_v1("0xFF", "255");
    diff_vs_v1("0x0", "0");
    diff_vs_v1("0xdeadbeef", "3735928559");
    diff_vs_v1("0XAB", "171");
    diff_vs_v1("0xff & 0x0f", "15");
    // and the ordinary forms still work
    diff_vs_v1("255", "255");
    diff_vs_v1("1.5e3", "1500");
    diff_vs_v1("0.5", "0.5");
}

void test_bitwise_compound_assignment() {
    expect_result("let x = 1; x <<= 3; return x;", "8");
    expect_result("let x = 16; x >>= 2; return x;", "4");
    expect_result("let x = -1; x >>>= 28; return x;", "15");
    expect_result("let x = 0xF0; x &= 0x3C; return x;", "48");
    expect_result("let x = 0xF0; x |= 0x0F; return x;", "255");
    expect_result("let x = 0xFF; x ^= 0x0F; return x;", "240");
}

// AN OPTIONAL CHAIN SHORT-CIRCUITS THE WHOLE CHAIN, not one link.
//
// Each `?.` used to jump only past itself, leaving undefined in the register
// for the REST of the chain to run on - so `o?.m()` with a null `o` called
// undefined. That is how p5.js stopped, four thousand instructions into the
// bundle.
void test_optional_chain_short_circuits() {
    expect_result("const o = null; return typeof o?.a;", "undefined");
    expect_result("const o = null; return typeof o?.a.b.c;", "undefined");
    expect_result("const o = null; return typeof o?.m();", "undefined");
    expect_result("const o = null; return typeof o?.[0];", "undefined");
    expect_result("const o = null; return typeof o?.a[0].b();", "undefined");
    // and the present cases still evaluate the whole chain
    expect_result("const o = { a: { b: 5 } }; return o?.a.b;", "5");
    expect_result("const o = { m() { return 7; } }; return o.m?.();", "7");
    expect_result("const o = { a: [9] }; return o?.a[0];", "9");
    expect_result("const o = { a: { m() { return 'deep'; } } }; return o?.a.m();", "deep");
    // undefined short-circuits as well as null, and nothing else does
    expect_result("const o = undefined; return typeof o?.a;", "undefined");
    expect_result("const o = 0; return typeof o?.toFixed;", "function");
    // a chain inside an ARGUMENT is its own chain, not part of the outer one
    expect_result("const o = { f(x) { return x === undefined ? 'inner' : x; } }; "
                  "const n = null; return o.f(n?.a);",
                  "inner");
}

void test_var_declarators_run_left_to_right() {
    expect_result("var a = 1, b = a + 1; return b;", "2");
    expect_result("var s = 'ab', n = s.length; return n;", "2");
    expect_result("var xs = [3,4], first = xs[0]; return first;", "3");
    expect_result("let a = 1, b = a + 1; return b;", "2");
    expect_result("const a = 1, b = a + 1; return b;", "2");
    expect_result("function f(x) { return x * 2; } var a = 2, b = f(a); return b;", "4");
}

// A PRIMITIVE BOXES ON PROPERTY ACCESS, so `Number.prototype`'s own prototype
// is `Object.prototype` and `(5).hasOwnProperty(...)` is a perfectly ordinary
// thing to write. Numbers and booleans stopped at their own prototype table
// here and answered undefined past it; arrays and strings already chained
// correctly, which is why nothing noticed.
//
// Found by the Phaser API probe: its tween manager asks `hasOwnProperty` of a
// number while working out which properties of a target to animate. Library
// code does this constantly on values whose type it has not checked.
void test_primitives_reach_object_prototype() {
    expect_result("return typeof (5).hasOwnProperty;", "function");
    expect_result("return String((5).hasOwnProperty('x'));", "false");
    expect_result("return typeof true.hasOwnProperty;", "function");
    expect_result("return String(true.hasOwnProperty('x'));", "false");
    expect_result("return typeof (5).isPrototypeOf;", "function");
    expect_result("return typeof (5).propertyIsEnumerable;", "function");
    // The own tables still WIN, or this fix would have shadowed them.
    expect_result("return (255).toString(16);", "ff");
    expect_result("return (1.5).toFixed(2);", "1.50");
    expect_result("return true.toString();", "true");
    // And the chain arrives at the same place a string's and an array's do.
    expect_result("return typeof 'a'.hasOwnProperty;", "function");
    expect_result("return typeof [].hasOwnProperty;", "function");
}

// `+x` IS ToNumber, and it compiled to a plain register copy - so `+"2"` was
// still the string "2".
//
// THE TEST THAT MISSED IT FIRST is instructive enough to keep the shape of:
// `var x = +xy[0]; return x + '/' + y;` passed with the bug in, because "2" and
// 2 concatenate identically. So every assertion here either asks `typeof` or
// puts the result somewhere only a number works - which is exactly how the bug
// surfaced in the first place, indexing pixel data with `(+y * 8 + +x) * 4`.
void test_unary_plus_converts() {
    expect_result("return typeof (+'2');", "number");
    expect_result("return typeof (+'abc');", "number");
    expect_result("var xs = [10,20,30]; var i = '1'; return xs[(+i) * 1];", "20");
    // The arithmetic a copy would get wrong: `+` on two strings concatenates,
    // so the conversion has to happen before the addition.
    expect_result("var a = '2', b = '3'; return (+a) + (+b);", "5");
    expect_result("return String(+'');", "0");
    expect_result("return String(+'  7  ');", "7");
    expect_result("return String(+'0x10');", "16");
    expect_result("return String(+true);", "1");
    expect_result("return String(+null);", "0");
    expect_result("return String(+[]);", "0");
    expect_result("return String(+'abc');", "NaN");
    expect_result("return String(+undefined);", "NaN");
    // Already a number: unchanged, and still a number.
    expect_result("return String(+42);", "42");
    expect_result("return String(+-3.5);", "-3.5");
    // The idiom that found it, end to end.
    expect_result("var d = [0,1,2,3,4,5,6,7,8];"
                  "var k = '1,2', xy = k.split(',');"
                  "return d[+xy[1] * 2 + +xy[0]];",
                  "5");
}

// A function body is not part of the optional chain that encloses it.
// `a?.b(() => c?.d)` compiles the arrow while the outer chain is open, and the
// arrow's own short-circuit must not be patched into the enclosing function's
// code - where that index means something else entirely.
void test_chain_state_does_not_leak_into_a_nested_function() {
    expect_result("const o = { m(f) { return f(); } }; const inner = null; "
                  "return typeof o?.m(() => inner?.x);",
                  "undefined");
    expect_result("const o = { m(f) { return f(); } }; const inner = { x: 3 }; "
                  "return o?.m(() => inner?.x);",
                  "3");
    expect_result("const a = { b: { c(f) { return f(); } } }; const n = null; "
                  "return a?.b.c(() => (n?.p.q ? 'yes' : 'no'));",
                  "no");
}

// THE COMMA OPERATOR HAS EFFECTS. It was added to the parser with a test that
// it PARSED and none that it ran, and it did nothing at all: the parser builds
// `a, b, c` as seq(seq(a, b), c) - binary, left-nested - and the compiler read
// a child LIST, which for such a node is empty. Every comma expression
// evaluated nothing and produced undefined.
//
// It silently emptied every Babel-transpiled class in p5.js, because
// `_createClass(e, r) { return r && _defineProperties(e.prototype, r), ..., e; }`
// is how one installs its methods.
void test_comma_operator_evaluates_everything() {
    expect_result("let n = 0; function f() { n++; } const x = (f(), f(), 5); return n + '|' + x;",
                  "2|5");
    // the value is the LAST operand
    expect_result("return (1, 2, 3);", "3");
    // it composes with && short-circuits, which is the shape Babel emits
    expect_result("let hit = 0; function f() { hit = 1; return 1; } "
                  "function g(a) { return a && f(), 9; } const r = g(1); return hit + '|' + r;",
                  "1|9");
    expect_result("let hit = 0; function f() { hit = 1; return 1; } "
                  "function g(a) { return a && f(), 9; } const r = g(0); return hit + '|' + r;",
                  "0|9");
    // and in a for-update clause, which is where p5 needed it first
    expect_result("let s = ''; for (let i = 0, j = 5; i < 2; i++, j--) { s += i + ':' + j + ' '; } "
                  "return s;",
                  "0:5 1:4 ");
}

// A CODE-POINT ESCAPE IS NOT ITS OWN TEXT.
//
// `\\xHH` and `\\uHHHH` fell through the escape decoder's default case, which
// pushes the character after the backslash - so '\\x41' was the three-character
// string x41 rather than "A", silently. It surfaced through base64: btoa('\\x00')
// encoded the letter x, and a page hand-writing binary got a corrupt image with
// nothing anywhere reporting a problem.
//
// A code point becomes its UTF-8 here, the same choice String.fromCharCode
// makes, because strings are bytes.
void test_string_escapes() {
    expect_result("return '\\x41'.length;", "1");
    expect_result("return '\\x41';", "A");
    expect_result("return '\\x00'.charCodeAt(0);", "0");
    expect_result("return '\\xff'.length;", "2"); // U+00FF is two bytes of UTF-8
    expect_result("return '\\u0041';", "A");
    expect_result("return '\\u{41}';", "A");
    expect_result("return '\\u00e9'.length;", "2");
    // A surrogate PAIR is one code point: an escaped emoji must equal the same
    // emoji written literally, which encoding the halves separately would break.
    expect_result("return '\\ud83d\\ude00'.length;", "4");
    expect_result("return '\\ud83d\\ude00' === '\\u{1f600}';", "true");
    expect_result("return '\\u4e2d'.length;", "3");
    expect_result("return '\\q';", "q");     // an unknown escape is the character itself
    expect_result("return 'a\\\nb';", "ab"); // a backslash before a real newline joins the lines
    expect_result("return '\\0'.charCodeAt(0);", "0");
}

void test_typeof() {
    diff_vs_v1("typeof 1", "number");
    diff_vs_v1("typeof 'x'", "string");
    diff_vs_v1("typeof true", "boolean");
    diff_vs_v1("typeof undefined", "undefined");
    diff_vs_v1("typeof null", "object"); // the famous wart
}

void test_template_literals() {
    expect_result("return `plain`;", "plain");
    expect_result("var n = 3; return `n is ${n}`;", "n is 3");
    expect_result("var a = 1; var b = 2; return `${a}+${b}=${a + b}`;", "1+2=3");
    // The interpolation coerces, which is most of what a template is for.
    expect_result("return `list: ${[1,2].join('-')}`;", "list: 1-2");
    expect_result("var o = {n: 5}; return `${o.n * 2}`;", "10");
    expect_result("return `a\\nb`.length;", "3"); // the escape is one character
}

void test_optional_chaining() {
    expect_result("var o = {a: {b: 5}}; return o?.a?.b;", "5");
    expect_result("var o = null; return o?.a;", "undefined");
    // THE point: it short-circuits. Without that, `.b` on undefined would be
    // evaluated and the whole chain would fail rather than yield undefined.
    expect_result("var o = {a: null}; return o?.a?.b?.c;", "undefined");
    expect_result("var o = {}; return o.missing?.deep;", "undefined");
    expect_result("var o = {f: function () { return 3; }}; return o.f?.();", "3");
}

void test_spread() {
    expect_result("var a = [1,2]; var b = [0, ...a, 3]; return b.join('');", "0123");
    expect_result("var a = [1,2]; var b = [...a]; b.push(3); return a.length;", "2"); // a copy
    expect_result("return [...'abc'].length;", "3");
}

// AN OBJECT CONVERTS THROUGH ITS OWN `toString` AND `valueOf`.
//
// That is ToPrimitive, and a class defines them precisely because it expects
// `'' + x`, a template hole and String(x) to use them. Returning the tag
// regardless turned every such object into "[object Object]" - which made an
// error message from colorjs name the wrong thing and sent a whole diagnosis
// down the wrong path.
void test_to_primitive() {
    expect_result("class P { constructor(n) { this.n = n; } toString() { return 'P' + this.n; } }"
                  "return '' + new P(5);",
                  "P5");
    expect_result("class P { toString() { return 'tmpl'; } } return `${new P()}`;", "tmpl");
    expect_result("class P { toString() { return 'str'; } } return String(new P());", "str");
    expect_result("const o = { toString: function () { return 'plain'; } }; return '' + o;",
                  "plain");
    // The numeric hint takes valueOf FIRST, so `+` decides what it means from
    // what the object hands back.
    expect_result("const v = { valueOf: function () { return 42; } }; return v + 1;", "43");
    expect_result("const v = { valueOf: function () { return 6; } }; return v * 7;", "42");
    expect_result("const s = { toString: function () { return 'x'; } }; return s + 1;", "x1");
    // An array still stringifies as its elements, which is Array.prototype's
    // own toString rather than the object tag.
    expect_result("return [1, 2] + '';", "1,2");
    expect_result("return String([1, [2, 3]]);", "1,2,3");
    // A method that hands back another object falls through rather than
    // recursing.
    expect_result("const bad = { toString: function () { return {}; },"
                  "              valueOf: function () { return {}; } }; return '' + bad;",
                  "[object Object]");
}

// IDENTIFYING A VALUE WITHOUT `instanceof`.
//
// `Object.getPrototypeOf(x).constructor.name` is the standard walk - it works
// where instanceof does not, and a page cannot defeat it by reassigning a
// constructor. Three separate pieces were missing, and the way they failed is
// the point: getPrototypeOf returned null for a primitive, a prototype had no
// `constructor`, and a class had no `name`. Each hole yields UNDEFINED, and
// undefined compares equal to the other undefined it is being tested against -
// so a plain string reported itself as an instance of a colour space, and every
// conversion through it silently handed the string straight back.
void test_type_identification() {
    expect_result("return Object.getPrototypeOf('x') === String.prototype;", "true");
    expect_result("return Object.getPrototypeOf(1) === Number.prototype;", "true");
    expect_result("return Object.getPrototypeOf(true) === Boolean.prototype;", "true");
    expect_result("return Object.getPrototypeOf([]) === Array.prototype;", "true");
    expect_result("return String.prototype.constructor.name;", "String");
    expect_result("return Object.getPrototypeOf(1).constructor.name;", "Number");
    expect_result("return [].constructor.name + ',' + ({}).constructor.name;", "Array,Object");
    // A class's name, including one with no constructor of its own - that gets
    // a synthesised one, which used to arrive anonymous.
    expect_result("class K { constructor() {} } return K.name;", "K");
    expect_result("class K {} return K.name;", "K");
    expect_result("class B {} class D extends B {} return D.name + ',' + B.name;", "D,B");
    expect_result("function f(a, b) {} return f.name + ',' + f.length;", "f,2");
    // The whole reason the above matters: this walk must NOT match.
    expect_result("class Space {}"
                  "function looksLike(v, k) {"
                  "  const p = Object.getPrototypeOf(v);"
                  "  return (p && p.constructor && p.constructor.name) === k.name;"
                  "}"
                  "return looksLike('srgb', Space);",
                  "false");
}

// `Object.prototype.toString.call(x)` is THE type tag - the one way to tell an
// array from a plain object from a null without trusting a constructor. It
// returned "[object Object]" for everything, and libraries PARSE the result:
// colorjs does `str.match(/^\[object\s+(.*?)\]$/)[1]`, which against a
// string not in that shape indexes null.
void test_type_tags() {
    expect_result("return Object.prototype.toString.call('x');", "[object String]");
    expect_result("return Object.prototype.toString.call(1);", "[object Number]");
    expect_result("return Object.prototype.toString.call(true);", "[object Boolean]");
    expect_result("return Object.prototype.toString.call([]);", "[object Array]");
    expect_result("return Object.prototype.toString.call({});", "[object Object]");
    expect_result("return Object.prototype.toString.call(null);", "[object Null]");
    expect_result("return Object.prototype.toString.call(undefined);", "[object Undefined]");
    expect_result("return Object.prototype.toString.call(function () {});", "[object Function]");
}

// `toString(radix)`, and `at` on a string.
void test_number_and_string_conversions() {
    // Dropping the radix returned the DECIMAL digits, so `(220).toString(16)`
    // was "220" - a string a colour parser can neither reject nor read.
    expect_result("return (220).toString(16);", "dc");
    expect_result("return (5).toString(2);", "101");
    expect_result("return (35).toString(36);", "z");
    expect_result("return (-10).toString(16);", "-a");
    expect_result("return (0).toString(16);", "0");
    expect_result("return (0.5).toString(2);", "0.1");
    expect_result("return (220).toString();", "220"); // no radix is still decimal
    // `at` counts from the end for a negative index; arrays had it, strings did not.
    expect_result("return 'abc'.at(-1);", "c");
    expect_result("return 'abc'.at(0);", "a");
    expect_result("return typeof 'abc'.at(9);", "undefined");
}

void test_bitwise_and_friends() {
    // JavaScript's bitwise operators work on ToInt32 of a double, so they are
    // not the C operators on the stored number.
    expect_result("return 6 & 3;", "2");
    expect_result("return 6 | 3;", "7");
    expect_result("return 6 ^ 3;", "5");
    expect_result("return 1 << 4;", "16");
    expect_result("return -16 >> 2;", "-4");
    expect_result("return -1 >>> 0;", "4294967295"); // unsigned: NOT -1
    expect_result("return ~5;", "-6");
    expect_result("return 2.7 | 0;", "2"); // truncates toward zero
    expect_result("return -2.7 | 0;", "-2");
    expect_result("return (1 << 33);", "2"); // the shift count is taken mod 32

    // `!=` is LOOSE. Compiled as `!==` it made `1 != '1'` true, which is the
    // opposite of what the operator means.
    expect_result("return 1 != '1';", "false");
    expect_result("return 1 !== '1';", "true");
    expect_result("return null == undefined;", "true");
}

void test_delete_in_instanceof() {
    expect_result("var o = {a: 1, b: 2}; delete o.a; return typeof o.a;", "undefined");
    // The remaining properties must still be findable - the name-to-position
    // index has to be rebuilt, not just have one entry removed.
    expect_result("var o = {a: 1, b: 2, c: 3}; delete o.b; return o.a + o.c;", "4");
    expect_result("var o = {a: 1, b: 2}; delete o.a; var keys = '';"
                  "for (const k in o) { keys += k; } return keys;",
                  "b");
    expect_result("var o = {a: 1}; delete o['a']; return typeof o.a;", "undefined");

    expect_result("var o = {a: 1}; return 'a' in o;", "true");
    expect_result("var o = {a: 1}; return 'b' in o;", "false");
    expect_result("return 1 in [7, 8];", "true");

    expect_result("class A { } return new A() instanceof A;", "true");
    expect_result("class A { } class B { } return new A() instanceof B;", "false");
    // instanceof walks the WHOLE chain, so a subclass instance is also an
    // instance of its parent.
    expect_result("class A { } class B extends A { } return new B() instanceof A;", "true");
    expect_result("return 3 instanceof Object;", "false");
}

void test_string_identity() {
    // `===` on strings compares CONTENT. Comparing the values bit for bit is
    // right for objects and for the singletons, and wrong for strings - two
    // strings with the same characters are almost never the same allocation.
    // Everything below returned the wrong answer when it was: `e.code ===
    // "Space"` was false for every event, so the game never fired.
    expect_result("return 'Space' === 'Space';", "true");
    expect_result("var a = 'Spa'; var b = 'ce'; return (a + b) === 'Space';", "true");
    expect_result("var a = 'Spa'; var b = 'ce'; return (a + b) !== 'Space';", "false");
    expect_result("return 'a' === 'b';", "false");
    expect_result("return '' === '';", "true");
    // Still STRICT about types - no coercion.
    expect_result("return '1' === 1;", "false");
    expect_result("return 'true' === true;", "false");

    // Objects keep IDENTITY semantics: same characters is not same object.
    expect_result("var a = {}; var b = {}; return a === b;", "false");
    expect_result("var a = {}; var b = a; return a === b;", "true");
    expect_result("var a = [1]; var b = [1]; return a === b;", "false");

    // Numbers keep their own rules, which the bit comparison got wrong in the
    // other direction.
    expect_result("return NaN === NaN;", "false");
    expect_result("return 0 === -0;", "true");

    // The three places this reached beyond the operator itself.
    expect_result("var r = ''; switch ('b') { case 'a': r = 'A'; break; case 'b': r = 'B'; break; }"
                  "return r;",
                  "B");
    expect_result("var k = 'b'; return ['a', 'b', 'c'].indexOf(k);", "1");
    expect_result("var k = 'b'; return ['a', 'b'].includes(k);", "true");
    expect_result("var o = {}; o['x' + 'y'] = 3; return o.xy;", "3");
}

} // namespace

int main() {
    test_arithmetic();
    test_plus_is_overloaded();
    test_comparison_and_logic();
    test_nullish_is_not_falsy();
    test_radix_literals();
    test_bitwise_compound_assignment();
    test_optional_chain_short_circuits();
    test_var_declarators_run_left_to_right();
    test_unary_plus_converts();
    test_primitives_reach_object_prototype();
    test_chain_state_does_not_leak_into_a_nested_function();
    test_comma_operator_evaluates_everything();
    test_string_escapes();
    test_typeof();
    test_template_literals();
    test_optional_chaining();
    test_spread();
    test_string_identity();
    test_bitwise_and_friends();
    test_delete_in_instanceof();
    test_to_primitive();
    test_type_identification();
    test_type_tags();
    test_number_and_string_conversions();
    REPORT("vm_operators");
}
