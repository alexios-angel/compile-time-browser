//===- ctbrowser/unittests/js/vm/operators.cpp - VM operator tests --------===//
//
// The VM's expressions: operators, coercion, literals, and what a value says it
// is. Sibling files in this directory cover the rest of the VM; expect.hpp holds
// the shared assertions.
//
// This is the only VM test that links ctjs, the older tree-walk engine.
// `diff_vs_v1` checks each expression twice: once against a hand-written
// expectation and once against ctjs running the same source. The differential
// check lasts only while ctjs is still in the tree, which is why
// unittests/CMakeLists.txt adds the ctjs include directories to this target
// alone.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include "expect.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <ctjs.hpp> // The older engine, for the differential comparison.

using namespace ctbrowser::script;

namespace {

/// Runs the same source through ctjs's tree-walk interpreter for comparison.
///
/// The explicit gc::collect() is required: ctjs reference-counts scope
/// environments, which form cycles, so each run leaks its environment chain
/// until the cycle collector runs. ctjs normally collects on a frame counter;
/// a test that runs hundreds of scripts without collecting trips
/// LeakSanitizer. The bytecode VM traces from precise roots and needs none of
/// this.
[[nodiscard]] std::string run_ctjs(std::string_view source, bool * ok = nullptr) {
    std::string out;
    {
        ctjs::run_result r = ctjs::run_value(source, {});
        if (ok != nullptr) { *ok = r.ok(); }
        out = r.ok() ? r["__result"].to_string() : "<error>";
    } // The result holds roots, so it has to die before the collector runs.
    ctjs::gc::collect();
    return out;
}

/// Checks \p expression against \p expected in the VM, and the VM against
/// ctjs. The expression is wrapped so both engines expose its value: the VM
/// via `return`, ctjs via the global `__result`.
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
    // A disagreement with ctjs fails the test too, but is not necessarily the
    // VM's fault, so both results are printed.
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
    diff_vs_v1("1 + '2'", "12");     // A string operand wins...
    diff_vs_v1("1 + 2 + '3'", "33"); // ...but only once it appears.
    diff_vs_v1("'1' + 2 + 3", "123");
}

void test_comparison_and_logic() {
    diff_vs_v1("1 < 2", "true");
    diff_vs_v1("2 <= 2", "true");
    diff_vs_v1("3 > 4", "false");
    diff_vs_v1("1 === 1", "true");
    diff_vs_v1("1 === '1'", "false"); // Strict.
    diff_vs_v1("1 == '1'", "true");   // Loose equality coerces.
    diff_vs_v1("null == undefined", "true");
    diff_vs_v1("null === undefined", "false");
    diff_vs_v1("!true", "false");
    diff_vs_v1("true && false", "false");
    diff_vs_v1("false || 'fallback'", "fallback");
    // Short-circuit: the right side must not be evaluated at all.
    expect_result("let hit = 0; function bump() { hit = 1; return true; } "
                  "let r = false && bump(); return hit;",
                  "0");
}

/// `??` asks a different question from `||`: it falls back only on null or
/// undefined, so `0 ?? 5` is 0 and `"" ?? "x"` is "". A default that overrode
/// a real 0 would surface in p5.js as a drawing in the wrong place rather than
/// as an error.
void test_nullish_is_not_falsy() {
    diff_vs_v1("0 ?\? 5", "0");
    diff_vs_v1("'' ?\? 'x'", "");
    diff_vs_v1("false ?\? true", "false");
    diff_vs_v1("NaN ?\? 1", "NaN");
    diff_vs_v1("null ?\? 5", "5");
    diff_vs_v1("undefined ?\? 5", "5");
    // It still short-circuits: the right side is not evaluated at all.
    expect_result("let hit = 0; function bump() { hit = 1; return 9; } "
                  "let r = 0 ?\? bump(); return hit;",
                  "0");
    // The assignment form asks the same question.
    expect_result("let a = 0; a ?\?= 5; return a;", "0");
    expect_result("let b = null; b ?\?= 5; return b;", "5");
    expect_result("let c = ''; c ?\?= 'x'; return c;", "");
}

/// Hex literals. std::from_chars in `general` format stops at the `x`, so a
/// lexer that used it would read `0xFF` as 0 with no error. p5.js has hundreds
/// of hex literals, in colour maths, bit masks and font tables.
void test_radix_literals() {
    diff_vs_v1("0xFF", "255");
    diff_vs_v1("0x0", "0");
    diff_vs_v1("0xdeadbeef", "3735928559");
    diff_vs_v1("0XAB", "171");
    diff_vs_v1("0xff & 0x0f", "15");
    // The ordinary forms still work.
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

/// An optional chain short-circuits the whole chain, not one link: with a
/// null `o`, `o?.m()` is undefined and never calls anything. A `?.` that
/// jumped only past itself would leave undefined for the rest of the chain to
/// run on.
void test_optional_chain_short_circuits() {
    expect_result("const o = null; return typeof o?.a;", "undefined");
    expect_result("const o = null; return typeof o?.a.b.c;", "undefined");
    expect_result("const o = null; return typeof o?.m();", "undefined");
    expect_result("const o = null; return typeof o?.[0];", "undefined");
    expect_result("const o = null; return typeof o?.a[0].b();", "undefined");
    // The present cases still evaluate the whole chain.
    expect_result("const o = { a: { b: 5 } }; return o?.a.b;", "5");
    expect_result("const o = { m() { return 7; } }; return o.m?.();", "7");
    expect_result("const o = { a: [9] }; return o?.a[0];", "9");
    expect_result("const o = { a: { m() { return 'deep'; } } }; return o?.a.m();", "deep");
    // Undefined short-circuits as well as null, and nothing else does.
    expect_result("const o = undefined; return typeof o?.a;", "undefined");
    expect_result("const o = 0; return typeof o?.toFixed;", "function");
    // A chain inside an argument is its own chain, not part of the outer one.
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

/// A primitive boxes on property access, and `Number.prototype`'s own prototype
/// is `Object.prototype`, so `(5).hasOwnProperty(...)` works. Phaser's tween
/// manager asks `hasOwnProperty` of a number while working out which
/// properties of a target to animate; library code does this constantly on
/// values whose type it has not checked.
void test_primitives_reach_object_prototype() {
    expect_result("return typeof (5).hasOwnProperty;", "function");
    expect_result("return String((5).hasOwnProperty('x'));", "false");
    expect_result("return typeof true.hasOwnProperty;", "function");
    expect_result("return String(true.hasOwnProperty('x'));", "false");
    expect_result("return typeof (5).isPrototypeOf;", "function");
    expect_result("return typeof (5).propertyIsEnumerable;", "function");
    // A primitive's own prototype still comes first.
    expect_result("return (255).toString(16);", "ff");
    expect_result("return (1.5).toFixed(2);", "1.50");
    expect_result("return true.toString();", "true");
    // And the chain arrives at the same place a string's and an array's do.
    expect_result("return typeof 'a'.hasOwnProperty;", "function");
    expect_result("return typeof [].hasOwnProperty;", "function");
}

/// Unary `+x` is ToNumber, not a register copy: `+"2"` is the number 2.
///
/// Every assertion here either asks `typeof` or puts the result somewhere only
/// a number works, because "2" and 2 concatenate identically: a test like
/// `return +x + '/'` would pass even if `+` did nothing. Real code indexes
/// pixel data with `(+y * 8 + +x) * 4`.
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
    // The pixel-indexing idiom, end to end.
    expect_result("var d = [0,1,2,3,4,5,6,7,8];"
                  "var k = '1,2', xy = k.split(',');"
                  "return d[+xy[1] * 2 + +xy[0]];",
                  "5");
}

/// A function body is not part of the optional chain that encloses it.
/// `a?.b(() => c?.d)` compiles the arrow while the outer chain is open, and the
/// arrow's own short-circuit jump must not be patched into the enclosing
/// function's code, where that instruction index means something else.
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

/// The comma operator evaluates every operand and yields the last. The parser
/// builds `a, b, c` as seq(seq(a, b), c), binary and left-nested, so the
/// compiler must walk that shape rather than expect a child list. Babel's
/// `_createClass(e, r) { return r && _defineProperties(e.prototype, r), ..., e; }`
/// installs class methods this way.
void test_comma_operator_evaluates_everything() {
    expect_result("let n = 0; function f() { n++; } const x = (f(), f(), 5); return n + '|' + x;",
                  "2|5");
    // The value is the last operand.
    expect_result("return (1, 2, 3);", "3");
    // It composes with && short-circuits, the shape Babel emits.
    expect_result("let hit = 0; function f() { hit = 1; return 1; } "
                  "function g(a) { return a && f(), 9; } const r = g(1); return hit + '|' + r;",
                  "1|9");
    expect_result("let hit = 0; function f() { hit = 1; return 1; } "
                  "function g(a) { return a && f(), 9; } const r = g(0); return hit + '|' + r;",
                  "0|9");
    // It also works in a for-update clause.
    expect_result("let s = ''; for (let i = 0, j = 5; i < 2; i++, j--) { s += i + ':' + j + ' '; } "
                  "return s;",
                  "0:5 1:4 ");
}

/// `\xHH`, `\uHHHH` and `\u{...}` escapes decode to the code point they
/// name, not to the text after the backslash ('\x41' is "A", not "x41"). A
/// page hand-writing binary for btoa depends on this. Lengths count UTF-16
/// code units.
void test_string_escapes() {
    expect_result("return '\\x41'.length;", "1");
    expect_result("return '\\x41';", "A");
    expect_result("return '\\x00'.charCodeAt(0);", "0");
    expect_result("return '\\xff'.length;", "1"); // One UTF-16 code unit.
    expect_result("return '\\u0041';", "A");
    expect_result("return '\\u{41}';", "A");
    expect_result("return '\\u00e9'.length;", "1");
    // A surrogate pair is one code point: an escaped emoji must equal the same
    // emoji written literally, which encoding the halves separately would break.
    expect_result("return '\\ud83d\\ude00'.length;", "2"); // A pair: two units.
    expect_result("return '\\ud83d\\ude00' === '\\u{1f600}';", "true");
    expect_result("return '\\u4e2d'.length;", "1");
    expect_result("return '\\q';", "q");     // An unknown escape is the character itself.
    expect_result("return 'a\\\nb';", "ab"); // A backslash before a newline joins the lines.
    expect_result("return '\\0'.charCodeAt(0);", "0");
}

void test_typeof() {
    diff_vs_v1("typeof 1", "number");
    diff_vs_v1("typeof 'x'", "string");
    diff_vs_v1("typeof true", "boolean");
    diff_vs_v1("typeof undefined", "undefined");
    diff_vs_v1("typeof null", "object"); // The famous wart.
}

void test_template_literals() {
    expect_result("return `plain`;", "plain");
    expect_result("var n = 3; return `n is ${n}`;", "n is 3");
    expect_result("var a = 1; var b = 2; return `${a}+${b}=${a + b}`;", "1+2=3");
    // The interpolation coerces, which is most of what a template is for.
    expect_result("return `list: ${[1,2].join('-')}`;", "list: 1-2");
    expect_result("var o = {n: 5}; return `${o.n * 2}`;", "10");
    expect_result("return `a\\nb`.length;", "3"); // The escape is one character.
}

void test_optional_chaining() {
    expect_result("var o = {a: {b: 5}}; return o?.a?.b;", "5");
    expect_result("var o = null; return o?.a;", "undefined");
    // It short-circuits: otherwise `.b` on undefined would be evaluated and
    // the whole chain would throw rather than yield undefined.
    expect_result("var o = {a: null}; return o?.a?.b?.c;", "undefined");
    expect_result("var o = {}; return o.missing?.deep;", "undefined");
    expect_result("var o = {f: function () { return 3; }}; return o.f?.();", "3");
}

void test_spread() {
    expect_result("var a = [1,2]; var b = [0, ...a, 3]; return b.join('');", "0123");
    expect_result("var a = [1,2]; var b = [...a]; b.push(3); return a.length;", "2"); // A copy.
    expect_result("return [...'abc'].length;", "3");
}

/// An object converts through its own `toString` and `valueOf` (ToPrimitive):
/// a class defines them because it expects `'' + x`, a template hole and
/// String(x) to use them, rather than "[object Object]".
void test_to_primitive() {
    expect_result("class P { constructor(n) { this.n = n; } toString() { return 'P' + this.n; } }"
                  "return '' + new P(5);",
                  "P5");
    expect_result("class P { toString() { return 'tmpl'; } } return `${new P()}`;", "tmpl");
    expect_result("class P { toString() { return 'str'; } } return String(new P());", "str");
    expect_result("const o = { toString: function () { return 'plain'; } }; return '' + o;",
                  "plain");
    // The default and numeric hints try valueOf first, so `+` decides what it
    // means from what the object returns.
    expect_result("const v = { valueOf: function () { return 42; } }; return v + 1;", "43");
    expect_result("const v = { valueOf: function () { return 6; } }; return v * 7;", "42");
    expect_result("const s = { toString: function () { return 'x'; } }; return s + 1;", "x1");
    // An array still stringifies as its elements, which is Array.prototype's
    // own toString rather than the object tag.
    expect_result("return [1, 2] + '';", "1,2");
    expect_result("return String([1, [2, 3]]);", "1,2,3");
    // A method that returns another object falls through rather than
    // recursing, and when both do it is a TypeError (ECMA-262 7.1.1.1 step 4).
    expect_result("const bad = { toString: function () { return {}; },"
                  "              valueOf: function () { return {}; } };"
                  "try { return '' + bad; } catch (e) { return e instanceof TypeError; }",
                  "true");
    // Symbol.toPrimitive wins over both, and sees the hint (ECMA-262 7.1.1
    // step 1.b).
    expect_result("const o = { [Symbol.toPrimitive](h) { return h; },"
                  "            valueOf() { return 1; } };"
                  "return [o + '', `${o}`, o * 1].join();",
                  "default,string,NaN");
    expect_result("const o = { [Symbol.toPrimitive]: 1 };"
                  "try { return '' + o; } catch (e) { return e instanceof TypeError; }",
                  "true");
}

/// Identifying a value without `instanceof`.
///
/// `Object.getPrototypeOf(x).constructor.name` is the standard walk; it works
/// where instanceof does not. It needs three pieces: getPrototypeOf on a
/// primitive, `constructor` on every prototype, and `name` on every class. A
/// missing piece yields undefined, which compares equal to another undefined,
/// so a plain string would report itself as an instance of a colorjs colour
/// space.
void test_type_identification() {
    expect_result("return Object.getPrototypeOf('x') === String.prototype;", "true");
    expect_result("return Object.getPrototypeOf(1) === Number.prototype;", "true");
    expect_result("return Object.getPrototypeOf(true) === Boolean.prototype;", "true");
    expect_result("return Object.getPrototypeOf([]) === Array.prototype;", "true");
    expect_result("return String.prototype.constructor.name;", "String");
    expect_result("return Object.getPrototypeOf(1).constructor.name;", "Number");
    expect_result("return [].constructor.name + ',' + ({}).constructor.name;", "Array,Object");
    // A class's name, including one with no constructor of its own (its
    // synthesised constructor must still carry the name).
    expect_result("class K { constructor() {} } return K.name;", "K");
    expect_result("class K {} return K.name;", "K");
    expect_result("class B {} class D extends B {} return D.name + ',' + B.name;", "D,B");
    expect_result("function f(a, b) {} return f.name + ',' + f.length;", "f,2");
    // The walk must not match a string against a class.
    expect_result("class Space {}"
                  "function looksLike(v, k) {"
                  "  const p = Object.getPrototypeOf(v);"
                  "  return (p && p.constructor && p.constructor.name) === k.name;"
                  "}"
                  "return looksLike('srgb', Space);",
                  "false");
}

/// `Object.prototype.toString.call(x)` is the type tag: the one way to tell an
/// array from a plain object from a null without trusting a constructor.
/// Libraries parse the result: colorjs does
/// `str.match(/^\[object\s+(.*?)\]$/)[1]`, which indexes null if the tag is
/// not in that shape.
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

/// `toString(radix)`, and `at` on a string.
void test_number_and_string_conversions() {
    // The radix is honoured: `(220).toString(16)` is "dc", not "220", which a
    // colour parser could neither reject nor read.
    expect_result("return (220).toString(16);", "dc");
    expect_result("return (5).toString(2);", "101");
    expect_result("return (35).toString(36);", "z");
    expect_result("return (-10).toString(16);", "-a");
    expect_result("return (0).toString(16);", "0");
    expect_result("return (0.5).toString(2);", "0.1");
    expect_result("return (220).toString();", "220"); // No radix is still decimal.
    // `at` counts from the end for a negative index, on strings as on arrays.
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
    expect_result("return -1 >>> 0;", "4294967295"); // Unsigned, not -1.
    expect_result("return ~5;", "-6");
    expect_result("return 2.7 | 0;", "2"); // Truncates toward zero.
    expect_result("return -2.7 | 0;", "-2");
    expect_result("return (1 << 33);", "2"); // The shift count is taken mod 32.

    // `!=` is loose equality negated, so `1 != '1'` is false.
    expect_result("return 1 != '1';", "false");
    expect_result("return 1 !== '1';", "true");
    expect_result("return null == undefined;", "true");
}

void test_delete_in_instanceof() {
    expect_result("var o = {a: 1, b: 2}; delete o.a; return typeof o.a;", "undefined");
    // The remaining properties must still be findable: the name-to-position
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
    // instanceof walks the whole chain, so a subclass instance is also an
    // instance of its parent.
    expect_result("class A { } class B extends A { } return new B() instanceof A;", "true");
    expect_result("return 3 instanceof Object;", "false");
}

void test_in_operator_validation_and_keys() {
    expect_result("let caught = 0, coerced = 0;"
                  "const key = { [Symbol.toPrimitive]() { coerced++; return 'x'; } };"
                  "for (const rhs of [undefined, null, false, 1, 'x', Symbol(), 1n]) {"
                  "  try { key in rhs; } catch (e) { if (e instanceof TypeError) caught++; }"
                  "} return caught + ',' + coerced;",
                  "7,0");
    expect_result(
        "let log = '';"
        "function left() { log += 'L'; return { toString() { log += 'K'; return 'x'; } }; }"
        "function right() { log += 'R'; return 0; }"
        "try { left() in right(); } catch (e) { log += e.name; } return log;",
        "LRTypeError");
    expect_result("const intrinsic = TypeError; TypeError = function Fake() {};"
                  "try { 'x' in 1; } catch (e) { return e instanceof intrinsic; }",
                  "true");
    expect_result("const symbol = Symbol('key'); let log = '';"
                  "const key = { [Symbol.toPrimitive](hint) { log += hint; return symbol; } };"
                  "const target = new Proxy({ [symbol]: 1 }, {"
                  "  get has() { log += ',trap'; return function(t, k) {"
                  "    log += ',' + (k === symbol); return k in t; }; }"
                  "}); return (key in target) + ',' + log;",
                  "true,string,trap,true");
    expect_result("let log = ''; const target = new Proxy({}, {"
                  "  has(t, k) { log += typeof k + ':' + k + ','; return true; }"
                  "}); null in target; false in target; 1n in target; return log;",
                  "string:null,string:false,string:1,");
    expect_result("let traps = 0; const sentinel = {};"
                  "const key = { [Symbol.toPrimitive]() { throw sentinel; } };"
                  "const target = new Proxy({}, { get has() { traps++; return () => true; } });"
                  "try { key in target; } catch (e) { return (e === sentinel) + ',' + traps; }",
                  "true,0");
    expect_result("let coerced = 0; const revoked = Proxy.revocable({}, {}); revoked.revoke();"
                  "const key = { toString() { coerced++; return 'x'; } };"
                  "try { key in revoked.proxy; } catch (e) { return e.name + ',' + coerced; }",
                  "TypeError,1");
    expect_result("const target = Object.create({ get x() { throw 1; } });"
                  "return ['x' in target, 'length' in function() {}, 'length' in Object('x'),"
                  "        0 in new Uint8Array(1)].join();",
                  "true,true,true,true");
    // Generic HasProperty still sees a primitive receiver's string indices.
    expect_result("let outer = 9; with ('abc') { return length + ',' + outer; }", "3,9");
    expect_result("return Array.prototype.map.call('ab', x => x.toUpperCase()).join('');", "AB");
}

void test_string_identity() {
    // `===` on strings compares content. Comparing the values bit for bit is
    // right for objects and the singletons, and wrong for strings: two strings
    // with the same characters are rarely the same allocation, and `e.code ===
    // "Space"` must be true in a key handler.
    expect_result("return 'Space' === 'Space';", "true");
    expect_result("var a = 'Spa'; var b = 'ce'; return (a + b) === 'Space';", "true");
    expect_result("var a = 'Spa'; var b = 'ce'; return (a + b) !== 'Space';", "false");
    expect_result("return 'a' === 'b';", "false");
    expect_result("return '' === '';", "true");
    // Still strict about types: no coercion.
    expect_result("return '1' === 1;", "false");
    expect_result("return 'true' === true;", "false");

    // Objects keep identity semantics: equal contents are not the same object.
    expect_result("var a = {}; var b = {}; return a === b;", "false");
    expect_result("var a = {}; var b = a; return a === b;", "true");
    expect_result("var a = [1]; var b = [1]; return a === b;", "false");

    // Numbers keep their own rules, which a bit comparison would get wrong
    // (NaN is not equal to itself; 0 and -0 are equal).
    expect_result("return NaN === NaN;", "false");
    expect_result("return 0 === -0;", "true");

    // Other places that compare strings by content: switch cases, indexOf,
    // includes, and computed property keys.
    expect_result("var r = ''; switch ('b') { case 'a': r = 'A'; break; case 'b': r = 'B'; break; }"
                  "return r;",
                  "B");
    expect_result("var k = 'b'; return ['a', 'b', 'c'].indexOf(k);", "1");
    expect_result("var k = 'b'; return ['a', 'b'].includes(k);", "true");
    expect_result("var o = {}; o['x' + 'y'] = 3; return o.xy;", "3");
}

/// context::binary_op answers the six bitwise opcodes as the interpreter's
/// handlers do: ToNumeric first, so an object's valueOf runs and a Symbol is a
/// TypeError. Compiled code that lowers them through ct_aot_binary_op relies on
/// this.
void test_binary_op_takes_the_bitwise_six() {
    context cx;
    install_builtins(cx);
    const auto number = [&](op kind, value l, value r) {
        const value got = cx.binary_op(kind, l, r);
        return got.is_number() ? got.as_number() : -12345.0;
    };
    CHECK(number(op::bit_and, value::number(6), value::number(3)) == 2);
    CHECK(number(op::bit_or, value::number(4), value::number(1)) == 5);
    CHECK(number(op::bit_xor, value::number(5), value::number(1)) == 4);
    CHECK(number(op::shl, value::number(1), value::number(3)) == 8);
    CHECK(number(op::shr, value::number(-8), value::number(1)) == -4);
    CHECK(number(op::ushr, value::number(-1), value::number(28)) == 15);
    const program & made =
        cx.own_program(compiler::compile("globalThis.four = {valueOf() { return 4; }};"
                                         "globalThis.sym = Symbol();"));
    CHECK(cx.run(made).ok);
    CHECK(number(op::bit_or, cx.global("four"), value::number(1)) == 5);
    const program & probe = cx.own_program(compiler::compile("try { sym & 1; return 'no'; }"
                                                             " catch (e) { return e.name; }"));
    const run_result ran = cx.run(probe);
    CHECK(ran.ok && cx.to_string(ran.returned) == "TypeError");
}

} // namespace

int main() {
    test_binary_op_takes_the_bitwise_six();
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
    test_in_operator_validation_and_keys();
    test_to_primitive();
    test_type_identification();
    test_type_tags();
    test_number_and_string_conversions();
    REPORT("vm_operators");
}
