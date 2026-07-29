// ctjs the engine VM.
//
// Every case here is checked TWICE: once against a hand-written expectation,
// and once against the previous engine's tree-walk interpreter running the same source
// (diff_vs_v1). The second check is the valuable one and it exists only while
// both engines are in the tree - a rewrite that can be differentially tested
// against the thing it replaces should be, and the window closes when the previous engine goes.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <ctjs.hpp> // the previous engine, for the differential comparison

using namespace ctbrowser::script;

namespace {

// Run through the the engine VM and render the result the way JS would print it.
[[nodiscard]] std::string run_vm(std::string_view source, bool * ok = nullptr) {
    const program prog = compiler::compile(source);
    context cx;
    install_builtins(cx);
    const run_result r = cx.run(prog);
    if (ok != nullptr) { *ok = r.ok; }
    if (!r.ok) { return "<error: " + r.error + ">"; }
    return cx.to_string(r.returned);
}

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

// A whole program, run as written. The stage-2 tests are about STATEMENTS -
// loops, labels, try blocks - so they are written as programs ending in an
// explicit `return`, not as expressions to be wrapped.
void expect_result(std::string_view source, std::string_view want) {
    const std::string got = run_vm(std::string{source});
    if (got != want) {
        std::printf("FAIL     %.70s => %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
                    std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

void expect(std::string_view source, std::string_view want) {
    const std::string got = run_vm(source);
    if (got != want) {
        std::printf("FAIL     %.70s => %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
                    std::string{want}.c_str());
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
    expect("let hit = 0; function bump() { hit = 1; return true; } "
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
    expect("let hit = 0; function bump() { hit = 1; return 9; } "
           "let r = 0 ?\? bump(); return hit;",
           "0");
    // the assignment form asks the same question
    expect("let a = 0; a ?\?= 5; return a;", "0");
    expect("let b = null; b ?\?= 5; return b;", "5");
    expect("let c = ''; c ?\?= 'x'; return c;", "");
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

// HALF OF A SIGNATURE USED TO BE DROPPED.
//
// The parser has always carried both - a default is the parameter node's `a`
// child and a rest is `d == 1` - and the compiler read neither. So an omitted
// argument stayed undefined instead of taking its default, and `...rest` bound
// the one positional argument in that slot rather than an array of what was
// left. Silent in both directions.
void test_default_parameters() {
    expect("function f(a, b = 2) { return a + b; } return f(1);", "3");
    expect("function f(a, b = 2) { return a + b; } return f(1, 10);", "11");
    // undefined takes the default; null does NOT - they are different questions
    expect("function f(a = 5) { return a; } return f(undefined);", "5");
    expect("function f(a = 5) { return a; } return f(null);", "null");
    // a default may be an expression, and may see earlier parameters
    expect("function f(a, b = a * 2) { return b; } return f(4);", "8");
    // and it is only evaluated when it is needed
    expect("let hit = 0; function d() { hit = 1; return 1; } "
           "function f(a = d()) { return a; } f(9); return hit;",
           "0");
    // arrow functions take the same path
    expect("const f = (a, b = 3) => a + b; return f(1);", "4");
}

void test_rest_parameters() {
    expect("function f(...rest) { return rest.length; } return f(1, 2, 3);", "3");
    expect("function f(...rest) { return rest[1]; } return f('a', 'b');", "b");
    expect("function f(a, ...rest) { return rest.join('-'); } return f(1, 2, 3, 4);", "2-3-4");
    // no extra arguments is an EMPTY array, not undefined
    expect("function f(a, ...rest) { return rest.length; } return f(1);", "0");
    expect("function f(...rest) { return Array.isArray(rest) && rest.length === 0; } return f();",
           "true");
    // the rest array is a real array and the locals after it are undisturbed
    expect("function f(a, ...rest) { let x = 7; return a + rest.length + x; } return f(1, 2, 3);",
           "10");
    // and a body long enough to reuse the registers the arguments arrived in
    expect("function f(a, ...rest) { let p = 1, q = 2, r = 3, s = 4, t = 5; "
           "return rest.join(',') + '|' + (p + q + r + s + t); } return f(0, 8, 9);",
           "8,9|15");
}

// A NESTED FUNCTION DECLARATION IS A BINDING IN ITS SCOPE, and this used to
// emit set_global at every depth. Two helpers with the same name in two
// different closures collided in one table, and a nested function meant to
// capture an enclosing local read a global instead - which is the point of an
// IIFE, silently undone, in a tree where every bundle is one.
void test_nested_function_declarations_are_local() {
    // the inner name does not escape
    expect("function outer() { function helper() { return 1; } return helper(); } "
           "outer(); return typeof helper;",
           "undefined");
    // two scopes, same name, no collision
    expect("function a() { function h() { return 'a'; } return h(); } "
           "function b() { function h() { return 'b'; } return h(); } "
           "return a() + b();",
           "ab");
    // and it still captures the enclosing local rather than a global
    expect("var n = 'global'; "
           "function outer() { let n = 'local'; function read() { return n; } return read(); } "
           "return outer();",
           "local");
    // recursion resolves to itself, not to a global of the same name
    expect("function outer() { function fact(k) { return k <= 1 ? 1 : k * fact(k - 1); } "
           "return fact(5); } return outer();",
           "120");
    // a top-level declaration is STILL a global - pages define functions the
    // host calls by name, and that is deliberate
    expect("function top() { return 7; } return typeof top;", "function");
}

// AN ARROW SEES THE `this` WHERE IT WAS WRITTEN. It used to get its own frame
// with an undefined receiver, so `this` inside an arrow inside a method - which
// is where arrows are usually written - was undefined.
void test_arrow_this_is_lexical() {
    expect("const o = { n: 5, get() { const f = () => this.n; return f(); } }; return o.get();",
           "5");
    // through two levels of arrow
    expect("const o = { n: 6, get() { const f = () => () => this.n; return f()(); } }; "
           "return o.get();",
           "6");
    // an arrow inside a callback still sees the method's object
    expect("const o = { n: 3, sum() { return [1, 2].map(x => x * this.n).join(','); } }; "
           "return o.sum();",
           "3,6");
    // an ordinary function still gets its OWN receiver
    expect("const o = { n: 1, get() { return this.n; } }; return o.get();", "1");
    // and a class method's arrow sees the instance
    expect("class C { constructor() { this.v = 9; } read() { const f = () => this.v; "
           "return f(); } } return new C().read();",
           "9");
}

void test_bitwise_compound_assignment() {
    expect("let x = 1; x <<= 3; return x;", "8");
    expect("let x = 16; x >>= 2; return x;", "4");
    expect("let x = -1; x >>>= 28; return x;", "15");
    expect("let x = 0xF0; x &= 0x3C; return x;", "48");
    expect("let x = 0xF0; x |= 0x0F; return x;", "255");
    expect("let x = 0xFF; x ^= 0x0F; return x;", "240");
}

// AN INSTANCE FIELD BELONGS TO THE INSTANCE.
//
// Field initialisers used to be evaluated ONCE, at class-definition time, and
// stored on the prototype - so every instance of `class A { items = [] }`
// shared one array. Push to one and it appeared in all of them, with nothing
// wrong at any stage. It is the nastiest shape in this batch, because the
// symptom shows up arbitrarily far from the cause.
void test_class_fields_are_per_instance() {
    expect("class A { n = 1; } const x = new A(); const y = new A(); x.n = 99; return y.n;", "1");
    // the case that actually bites: a mutable field
    expect("class A { items = []; } const x = new A(); const y = new A(); "
           "x.items.push(1); return y.items.length;",
           "0");
    // a field with no initialiser is still a field
    expect("class A { x; } const a = new A(); return typeof a.x;", "undefined");
    // an initialiser is an expression, evaluated per instance
    expect("let made = 0; class A { id = ++made; } new A(); new A(); return new A().id;", "3");
    // it may capture an enclosing local
    expect("function build(v) { class A { val = v; } return new A().val; } return build(42);",
           "42");
    // fields arrive before the constructor body, which may then use them
    expect("class A { n = 2; constructor() { this.n = this.n * 5; } } return new A().n;", "10");
    // INHERITED fields are initialised too, base first
    expect("class B { b = 'base'; } class D extends B { d = 'derived'; } "
           "const o = new D(); return o.b + '/' + o.d;",
           "base/derived");
    // and a STATIC field is the opposite case - one value on the class, which
    // is what it should always have been
    expect("class A { static count = 0; } A.count = 5; return A.count;", "5");
    // methods still live on the prototype and are shared
    expect("class A { n = 1; get2() { return 2; } } "
           "return new A().get2() + new A().get2();",
           "4");
}

// `f(...args)` - the single construct that stopped more of p5.js than any
// other. `nk::spread` was not a case in compile_expr at all, so it reached the
// default arm and refused the whole call.
void test_spread_in_a_call() {
    expect("function f(a, b, c) { return a + b + c; } return f(...[1, 2, 3]);", "6");
    // mixed with ordinary arguments, in any position
    expect("function f(a, b, c) { return a + '-' + b + '-' + c; } "
           "return f(1, ...[2, 3]);",
           "1-2-3");
    expect("function f(a, b, c) { return a + '-' + b + '-' + c; } "
           "return f(...[1, 2], 3);",
           "1-2-3");
    expect("function f(a, b, c, d) { return a + b + c + d; } "
           "return f(...[1, 2], ...[3, 4]);",
           "10");
    // a METHOD call keeps its receiver
    expect("const o = { n: 10, add(a, b) { return this.n + a + b; } }; return o.add(...[1, 2]);",
           "13");
    // and so does a computed one
    expect("const o = { n: 10, add(a, b) { return this.n + a + b; } }; "
           "const k = 'add'; return o[k](...[1, 2]);",
           "13");
    // it reaches natives too
    expect("return Math.max(...[3, 9, 4]);", "9");
    // spread of a computed array, not just a literal
    expect("function f(a, b) { return a * b; } const xs = [3, 4]; return f(...xs);", "12");
    // ...and it composes with a rest parameter on the other side
    expect("function f(...rest) { return rest.length; } return f(...[1, 2, 3], 4);", "4");
    expect("function f(a, ...rest) { return rest.join(','); } return f(...[1, 2, 3]);", "2,3");
    // `new C(...args)`
    expect("class P { constructor(a, b) { this.v = a + b; } } return new P(...[2, 3]).v;", "5");
    expect("class P { constructor(a, b, c) { this.v = a + b + c; } } "
           "return new P(1, ...[2, 3]).v;",
           "6");
    // a spread of an empty array passes nothing
    expect("function f(a) { return typeof a; } return f(...[]);", "undefined");
}

// A PRIVATE NAME IS A DISTINCT NAME. The `#` used to be skipped as an unknown
// byte, so `this.#count` became `this.count`: a private field silently aliased
// a public one, and every later stage agreed with the wrong reading. p5.js
// declares 174 of them. Real brand-check privacy is not modelled - what is
// fixed is that the two names are no longer the same name.
void test_private_names_are_distinct() {
    expect("class C { #n = 1; n = 2; read() { return this.#n + ',' + this.n; } } "
           "return new C().read();",
           "1,2");
    expect("class C { #v = 7; get() { return this.#v; } } return new C().get();", "7");
    expect("class C { static #hidden = 3; static read() { return C.#hidden; } } return C.read();",
           "3");
    // a private method
    expect("class C { #twice(x) { return x * 2; } run() { return this.#twice(4); } } "
           "return new C().run();",
           "8");
    // and the public field of the same name is untouched from outside
    expect("class C { #n = 1; n = 2; } const c = new C(); c.n = 9; return c.n;", "9");
}

// DESTRUCTURING. A binding position may hold a SHAPE, and every one of these
// used to be a PARSE ERROR: the declarator read `{` as the variable's name and
// the parser desynchronised from there. It stopped seventeen of p5.js's
// seventy-one modules, each at the first destructuring in the file.
void test_destructuring_declarations() {
    expect("const {a, b} = {a: 1, b: 2}; return a + b;", "3");
    expect("const [x, y] = [3, 4]; return x * y;", "12");
    expect("const {a: renamed} = {a: 7}; return renamed;", "7");
    // defaults, and only for undefined
    expect("const {a = 5} = {}; return a;", "5");
    expect("const {a = 5} = {a: 0}; return a;", "0");
    expect("const [p = 1, q = 2] = [9]; return p + ',' + q;", "9,2");
    // holes
    expect("const [, second] = ['a', 'b']; return second;", "b");
    // rest, in both shapes
    expect("const [head, ...tail] = [1, 2, 3]; return head + '|' + tail.join(',');", "1|2,3");
    expect("const {a, ...rest} = {a: 1, b: 2, c: 3}; "
           "return a + '|' + Object.keys(rest).join(',');",
           "1|b,c");
    // nested
    expect("const {a: {b}} = {a: {b: 'deep'}}; return b;", "deep");
    expect("const [[m], [n]] = [[1], [2]]; return m + n;", "3");
    expect("const {list: [first]} = {list: ['x']}; return first;", "x");
    // a computed key
    expect("const k = 'dyn'; const {[k]: got} = {dyn: 42}; return got;", "42");
    // a missing property is undefined, not an error
    expect("const {nope} = {}; return typeof nope;", "undefined");
    // inside a function, so the binding is a local rather than a global
    expect("function f(o) { const {a, b} = o; return a - b; } return f({a: 9, b: 4});", "5");
    // and a nested function can capture a name a pattern bound
    expect("function f(o) { const {v} = o; const get = () => v; return get(); } "
           "return f({v: 'captured'});",
           "captured");
}

void test_destructuring_parameters() {
    expect("function f({x, y}) { return x + y; } return f({x: 1, y: 2});", "3");
    expect("function f([a, b]) { return a * b; } return f([3, 4]);", "12");
    expect("function f({x = 10}) { return x; } return f({});", "10");
    expect("const f = ({n}) => n * 2; return f({n: 21});", "42");
    // mixed with ordinary parameters and with a rest
    expect("function f(a, {b}, ...rest) { return a + b + rest.length; } "
           "return f(1, {b: 2}, 9, 9);",
           "5");
    // a whole-parameter default alongside a pattern
    expect("function f({x} = {x: 'fallback'}) { return x; } return f();", "fallback");
}

void test_destructuring_assignment() {
    expect("let a, b; [a, b] = [1, 2]; return a + b;", "3");
    expect("let a = 1, b = 2; [a, b] = [b, a]; return a + ',' + b;", "2,1");
    expect("let x; ({x} = {x: 'set'}); return x;", "set");
    expect("let x, y; ({x, y: y} = {x: 1, y: 2}); return x + y;", "3");
    // a hole skips a position - which is why array literals had to learn them
    expect("let second; [, second] = ['a', 'b']; return second;", "b");
    // a member expression is a legal target
    expect("const o = {}; [o.first] = ['here']; return o.first;", "here");
}

void test_destructuring_in_for_of() {
    expect("let sum = 0; for (const [a, b] of [[1, 2], [3, 4]]) { sum += a * b; } return sum;",
           "14");
    expect("let names = ''; for (const {n} of [{n: 'a'}, {n: 'b'}]) { names += n; } return names;",
           "ab");
    // for-in with no declaration keyword, assigning to an existing binding
    expect("let k, seen = ''; for (k in {a: 1, b: 2}) { seen += k; } return seen;", "ab");
}

// A REGEX ENGINE. Literals were rejected by name; there was none. Ported from
// ctjs's backtracking matcher, which was already self-contained and coupled to
// its host by exactly two calls, then extended with what p5.js actually uses:
// lookahead, the sticky flag and named groups. Lookbehind and backreferences
// are REFUSED rather than mis-matched - neither appears in p5.js, and a
// matcher that silently ignores an assertion is worse than one that says no.
void test_regex() {
    expect("return /a(b+)c/.exec('xxabbbcyy')[0];", "abbbc");
    expect("return /a(b+)c/.exec('xxabbbcyy')[1];", "bbb");
    // .index is the most-used feature of all, at 143 sites in p5.js
    expect("return /a(b+)c/.exec('xxabbbcyy').index;", "2");
    expect("return /nope/.exec('abc') === null;", "true");
    expect("return /^\\d+$/.test('4711');", "true");
    expect("return /^\\d+$/.test('47a1');", "false");
    expect("return /x/i.test('X');", "true");
    expect("return /[a-f0-9]{2}/i.exec('zz A9 zz')[0];", "A9");
    // the constructor and the literal build the same thing
    expect("return new RegExp('b+', '').exec('abbbc')[0];", "bbb");
    expect("return /ab/g.source + '|' + /ab/gi.flags;", "ab|gi");
    // lookahead, positive and negative
    expect("return /foo(?=bar)/.test('foobar');", "true");
    expect("return /foo(?=bar)/.test('foobaz');", "false");
    expect("return /foo(?!bar)/.test('foobaz');", "true");
    // a named group is an ordinary capture that also answers to a name
    expect("return /(?<n>\\d+)/.exec('x42').groups.n;", "42");
    // `g` resumes from lastIndex and writes it back
    expect("const re = /\\d/g; const s = 'a1b2'; re.exec(s); return re.exec(s)[0];", "2");
    expect("const re = /\\d/g; re.exec('a1'); re.exec('a1'); return re.lastIndex;", "0");
    // and a pattern that cannot compile does not match rather than crashing
    expect("return /(?<=x)y/.test('xy');", "false");
}

// ACCESSORS. A property that runs code when it is read, which is a different
// thing from a property that HOLDS a function - installing a getter as a data
// property made `obj.v` be the function rather than call it, and a setter of
// the same name overwrote the getter outright.
//
// They live in a table BESIDE the data properties rather than widening every
// property into a descriptor, so the dozen places that iterate props - the DOM
// bindings among them - were untouched, and an object with no accessors pays
// one bool.
void test_accessors() {
    expect("const o = { get v() { return 42; } }; return o.v;", "42");
    expect("const o = { n: 0, set v(x) { this.n = x * 2; } }; o.v = 21; return o.n;", "42");
    expect("const o = { n: 1, get v() { return this.n; }, set v(x) { this.n = x; } }; "
           "o.v = 9; return o.v;",
           "9");
    // a getter is CALLED, not returned
    expect("const o = { get v() { return 1; } }; return typeof o.v;", "number");
    // on a class, and on its prototype so every instance sees it
    expect("class C { constructor() { this.n = 3; } get double() { return this.n * 2; } } "
           "return new C().double;",
           "6");
    expect("class C { constructor() { this.n = 0; } set v(x) { this.n = x + 1; } } "
           "const c = new C(); c.v = 4; return c.n;",
           "5");
    // a static accessor lives on the constructor
    expect("class C { static get name2() { return 'C'; } } return C.name2;", "C");
    // an accessor INHERITED through extends still reads the instance
    expect("class B { get kind() { return 'b:' + this.n; } } "
           "class D extends B { constructor() { super(); this.n = 7; } } "
           "return new D().kind;",
           "b:7");
    // a write with no setter is discarded rather than shadowing the getter
    expect("const o = { get v() { return 1; } }; o.v = 99; return o.v;", "1");
    // an own DATA property wins over an inherited accessor
    expect("class B { get v() { return 'proto'; } } class D extends B {} "
           "const d = new D(); return d.v;",
           "proto");
    // and Object.keys sees an accessor, because it is a property
    expect("const o = { a: 1, get b() { return 2; } }; return Object.keys(o).join(',');", "a,b");
}

void test_object_descriptors() {
    expect("const o = {}; Object.defineProperty(o, 'x', { value: 5 }); return o.x;", "5");
    expect("const o = { n: 2 }; Object.defineProperty(o, 'x', { get() { return this.n * 3; } }); "
           "return o.x;",
           "6");
    expect("const o = {}; Object.defineProperty(o, 'x', { get() { return 1; } }); "
           "const d = Object.getOwnPropertyDescriptor(o, 'x'); return typeof d.get;",
           "function");
    // create + getPrototypeOf round-trip
    expect("const base = { greet() { return 'hi'; } }; const o = Object.create(base); "
           "return o.greet();",
           "hi");
    expect("const base = {}; const o = Object.create(base); "
           "return Object.getPrototypeOf(o) === base;",
           "true");
    expect("const o = {}; Object.setPrototypeOf(o, { v: 8 }); return o.v;", "8");
    expect("const o = { a: 1, get b() { return 2; } }; "
           "return Object.getOwnPropertyNames(o).join(',');",
           "a,b");
    expect("return Object.fromEntries([['a', 1], ['b', 2]]).b;", "2");
}

// AN OPTIONAL CHAIN SHORT-CIRCUITS THE WHOLE CHAIN, not one link.
//
// Each `?.` used to jump only past itself, leaving undefined in the register
// for the REST of the chain to run on - so `o?.m()` with a null `o` called
// undefined. That is how p5.js stopped, four thousand instructions into the
// bundle.
void test_optional_chain_short_circuits() {
    expect("const o = null; return typeof o?.a;", "undefined");
    expect("const o = null; return typeof o?.a.b.c;", "undefined");
    expect("const o = null; return typeof o?.m();", "undefined");
    expect("const o = null; return typeof o?.[0];", "undefined");
    expect("const o = null; return typeof o?.a[0].b();", "undefined");
    // and the present cases still evaluate the whole chain
    expect("const o = { a: { b: 5 } }; return o?.a.b;", "5");
    expect("const o = { m() { return 7; } }; return o.m?.();", "7");
    expect("const o = { a: [9] }; return o?.a[0];", "9");
    expect("const o = { a: { m() { return 'deep'; } } }; return o?.a.m();", "deep");
    // undefined short-circuits as well as null, and nothing else does
    expect("const o = undefined; return typeof o?.a;", "undefined");
    expect("const o = 0; return typeof o?.toFixed;", "function");
    // a chain inside an ARGUMENT is its own chain, not part of the outer one
    expect("const o = { f(x) { return x === undefined ? 'inner' : x; } }; "
           "const n = null; return o.f(n?.a);",
           "inner");
}

void test_symbol() {
    expect("return typeof Symbol('x');", "symbol");
    expect("return typeof Symbol.iterator;", "symbol");
    expect("return Symbol('tag').description;", "tag");
    expect("return Symbol('a') === Symbol('a');", "false");
    expect("return Symbol.iterator === Symbol.iterator;", "true");
    // usable as a property key, which is the whole point
    expect("const s = Symbol('k'); const o = {}; o[s] = 5; return o[s];", "5");
    expect("const s = Symbol('k'); const o = {}; o[s] = 5; return o.k;", "undefined");
    expect("const o = {}; o[Symbol.iterator] = 1; return o[Symbol.iterator];", "1");
    expect("return Symbol('x').toString();", "Symbol(x)");
}

void test_collections() {
    expect("const s = new Set([1, 2, 3]); return s.has(2) + '|' + s.size;", "true|3");
    expect("const s = new Set(); s.add(1); s.add(1); return s.size;", "1");
    expect("const s = new Set([1, 2]); s.delete(1); return s.size;", "1");
    expect("const s = new Set([1, 2]); let sum = 0; s.forEach(v => { sum += v; }); return sum;",
           "3");
    expect("const m = new Map([['a', 1]]); m.set('b', 2); return m.get('b') + '|' + m.size;",
           "2|2");
    expect("const m = new Map(); m.set('k', 1); m.set('k', 9); return m.get('k') + '|' + m.size;",
           "9|1");
    expect("const m = new Map([['a', 1], ['b', 2]]); return m.keys().join(',');", "a,b");
    expect("const m = new Map([['a', 1]]); return m.has('z');", "false");
    // NaN matches NaN as a key, which === does not
    expect("const s = new Set([NaN]); return s.has(NaN);", "true");
    // and a class may extend one - which means super() has to initialise the
    // RECEIVER rather than making a fresh object
    expect("class S extends Set {} const s = new S(); s.add(5); return s.has(5) + '|' + s.size;",
           "true|1");
}

void test_errors() {
    expect("const e = new Error('boom'); return e.message;", "boom");
    expect("const e = new TypeError('t'); return e.name;", "TypeError");
    expect("return new RangeError('r') instanceof Error;", "true");
    expect("try { throw new TypeError('t'); } catch (e) { return e.name + ':' + e.message; }",
           "TypeError:t");
    // a page's own error type, which is the shape 239 `throw new` in p5 rely on
    expect("class MyError extends Error { constructor(m) { super(m); this.name = 'MyError'; } } "
           "try { throw new MyError('x'); } catch (e) { return e.name + '/' + e.message + '/' + "
           "(e instanceof Error); }",
           "MyError/x/true");
    expect("return new Error('z').toString();", "Error: z");
}

// A DERIVED CLASS WITH NO CONSTRUCTOR still runs its parent's. An empty one was
// synthesised instead, so the parent never ran and the instance had none of its
// state - and the failure surfaced at the first method that needed it.
void test_implicit_super() {
    expect("class B { constructor(v) { this.v = v; } } class D extends B {} return new D(7).v;",
           "7");
    expect("class B { constructor() { this.n = 1; } } class M extends B {} class D extends M {} "
           "return new D().n;",
           "1");
    expect("class B { constructor(a, b) { this.s = a + b; } } class D extends B {} "
           "return new D(2, 3).s;",
           "5");
}

// A CLASS EXPRESSION is as ordinary as a function expression. `class` had no
// case in primary(), so `const X = class {...}` read a global named `class` and
// the body's members leaked out as top-level statements - silently.
void test_class_expressions() {
    expect("const X = class { constructor() { this.v = 1; } }; return new X().v;", "1");
    expect("const X = class Named { m() { return 'ok'; } }; return new X().m();", "ok");
    expect("const make = () => class { get v() { return 5; } }; return new (make())().v;", "5");
}

void test_stdlib_additions() {
    expect("return Array.isArray([]) + '|' + Array.isArray({});", "true|false");
    expect("return Array.from('abc').join('-');", "a-b-c");
    expect("return Array.from([1, 2], x => x * 2).join(',');", "2,4");
    expect("return Array.of(1, 2, 3).length;", "3");
    expect("return [1, 2, 3].at(-1);", "3");
    expect("return [1, 2, 3, 4].fill(0, 1, 3).join(',');", "1,0,0,4");
    expect("return [1, [2, [3]]].flat().length;", "3");
    expect("return [1, [2, [3]]].flat(2).join(',');", "1,2,3");
    expect("return [1, 2].flatMap(x => [x, x]).join(',');", "1,1,2,2");
    expect("return [1, 2, 3].findLast(x => x < 3);", "2");
    expect("return Math.cbrt(27) + '|' + Math.log2(8) + '|' + Math.log10(1000);", "3|3|3");
    expect("return Number.isInteger(2) + '|' + Number.isInteger(2.5);", "true|false");
    // these do NOT coerce, and the difference is used deliberately
    expect("return Number.isFinite('1') + '|' + isFinite('1');", "false|true");
    expect("return Number.MAX_SAFE_INTEGER;", "9007199254740991");
}

// A NAMED CLASS EXPRESSION BINDS ITS OWN NAME, and its methods see it.
// `let X = class Inner { static m() { return Inner; } }` - `Inner` inside those
// methods is the class, not any outer binding. It is how p5.js declares itself
// (`let p5$2 = class p5 {...}`), and without it every static method that named
// the class read an undefined global.
void test_named_class_expression_binds_itself() {
    expect("let X = class Inner { static who() { return typeof Inner; } }; return X.who();",
           "function");
    expect("let X = class Inner { static tag = 'i'; static get() { return Inner.tag; } }; "
           "return X.get();",
           "i");
    expect("let X = class Inner { constructor() { this.self = Inner; } }; "
           "return new X().self === X;",
           "true");
    // TODO: scope the name to the class BODY. It is bound in the enclosing
    // scope because that is the only scope this compiler has to put it in;
    // a per-class scope would fix it.
    // APPROXIMATION, and worth knowing: the inner name is bound in the
    // ENCLOSING scope rather than only inside the class body, because that is
    // the only scope this compiler has to put it in. Real JavaScript would
    // leave `Inner` undefined out here. Nothing in p5.js depends on the
    // difference, and a leaked binding is visible rather than wrong.
    expect("let Outer = class Inner { static who() { return 'inner'; } }; "
           "return Outer.who() + '|' + typeof Inner;",
           "inner|function");
}

// A function body is not part of the optional chain that encloses it.
// `a?.b(() => c?.d)` compiles the arrow while the outer chain is open, and the
// arrow's own short-circuit must not be patched into the enclosing function's
// code - where that index means something else entirely.
void test_chain_state_does_not_leak_into_a_nested_function() {
    expect("const o = { m(f) { return f(); } }; const inner = null; "
           "return typeof o?.m(() => inner?.x);",
           "undefined");
    expect("const o = { m(f) { return f(); } }; const inner = { x: 3 }; "
           "return o?.m(() => inner?.x);",
           "3");
    expect("const a = { b: { c(f) { return f(); } } }; const n = null; "
           "return a?.b.c(() => (n?.p.q ? 'yes' : 'no'));",
           "no");
}

// A CLASS DECLARATION IS A HOISTED BINDING, and the reason is register
// allocation rather than semantics: a local first declared while an EXPRESSION
// is being compiled sits above the statement's register mark, so the statement
// releases it and the next statement's temporaries reuse the slot. `class S {}`
// followed by two `new S()` therefore worked once and found an object the
// second time.
void test_class_declaration_survives_the_statement() {
    expect("class S { constructor(x) { this.id = x; } } "
           "const a = new S('a'); const b = new S('b'); return a.id + b.id;",
           "ab");
    expect("function f() { class S { constructor(x) { this.id = x; } } "
           "const a = new S('a'); const b = new S('b'); const c = new S('c'); "
           "return a.id + b.id + c.id; } return f();",
           "abc");
    // with a static field, which is what put a fresh object in the register
    expect("function f() { class S { constructor(x) { this.id = x; } static r = new Map(); } "
           "const a = new S('a'); const b = new S('b'); return a.id + b.id; } return f();",
           "ab");
    // and a class used before its declaration in the same scope still binds
    expect("function f() { function make() { return new S(1); } class S { constructor(v) "
           "{ this.v = v; } } return make().v; } return f();",
           "1");
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
    expect("let n = 0; function f() { n++; } const x = (f(), f(), 5); return n + '|' + x;", "2|5");
    // the value is the LAST operand
    expect("return (1, 2, 3);", "3");
    // it composes with && short-circuits, which is the shape Babel emits
    expect("let hit = 0; function f() { hit = 1; return 1; } "
           "function g(a) { return a && f(), 9; } const r = g(1); return hit + '|' + r;",
           "1|9");
    expect("let hit = 0; function f() { hit = 1; return 1; } "
           "function g(a) { return a && f(), 9; } const r = g(0); return hit + '|' + r;",
           "0|9");
    // and in a for-update clause, which is where p5 needed it first
    expect("let s = ''; for (let i = 0, j = 5; i < 2; i++, j--) { s += i + ':' + j + ' '; } "
           "return s;",
           "0:5 1:4 ");
}

// EVERY FUNCTION HAS A `prototype`, made on first use. `function F() {}; new F()
// instanceof F` is the constructor-function pattern every transpiler emits -
// Babel's own `_classCallCheck` guard is exactly that test - and a plain
// function had none, so `new F()` produced an object with no prototype.
void test_functions_have_a_prototype() {
    expect("function F() {} return typeof F.prototype;", "object");
    expect("function F() {} return F.prototype.constructor === F;", "true");
    expect("function F() {} return new F() instanceof F;", "true");
    // the same object every time, or nothing built on it would hold
    expect("function F() {} return F.prototype === F.prototype;", "true");
    // methods installed on it are found by an instance
    expect("function F() {} F.prototype.m = function () { return 4; }; return new F().m();", "4");
    // an arrow is not a constructor and gets none
    expect("const a = () => 1; return typeof a.prototype;", "undefined");
}

void test_proxy() {
    expect("const p = new Proxy({ v: 1 }, {}); return p.v;", "1");
    expect("const p = new Proxy({}, { get(t, k) { return 'got:' + k; } }); return p.anything;",
           "got:anything");
    expect("const p = new Proxy({ a: 1 }, { has(t, k) { return k === 'z'; } }); "
           "return ('z' in p) + '|' + ('a' in p);",
           "true|false");
    // the trap p5.js needs at its top level
    expect("class B { constructor(v) { this.v = v; } } "
           "const P = new Proxy(B, { construct(t, args) { return new t(args[0] * 2); } }); "
           "return new P(4).v;",
           "8");
    // an absent trap falls through to the target rather than being skipped
    expect("class B { constructor() { this.v = 'base'; } } "
           "const P = new Proxy(B, {}); return new P().v;",
           "base");
    expect("return typeof Reflect.get({ a: 5 }, 'a');", "number");
    expect("return Reflect.get({ a: 5 }, 'a');", "5");
}

// `Object.defineProperty` with a descriptor that has no value, get or set
// describes ATTRIBUTES ONLY and must leave the existing value alone. Writing
// undefined instead is how `Object.defineProperty(C, "prototype",
// {writable: false})` - which every Babel class emits - wiped the prototype it
// had just filled in.
void test_define_property_attributes_only() {
    expect("const o = { v: 1 }; Object.defineProperty(o, 'v', { writable: false }); return o.v;",
           "1");
    expect("function F() {} F.prototype.m = function () { return 1; }; "
           "Object.defineProperty(F, 'prototype', { writable: false }); "
           "return typeof F.prototype.m;",
           "function");
    // a FUNCTION is an object: Babel defines onto the constructor as well
    expect("function F() {} Object.defineProperty(F, 'tag', { value: 'x' }); return F.tag;", "x");
}

void test_function_prototype() {
    expect("function f(a) { return this.v + a; } return f.call({ v: 1 }, 2);", "3");
    expect("function f(a) { return this.v + a; } return f.apply({ v: 1 }, [2]);", "3");
    expect("function f(a) { return this.v + a; } return f.bind({ v: 1 })(2);", "3");
    // bind is a PARTIAL APPLICATION, not just a receiver change
    expect("function f(a, b) { return this.v + a + b; } return f.bind({ v: 1 }, 10)(2);", "13");
    // the constructor-borrowing pattern every transpiler emits
    expect("function P() { this.v = 1; } function C() { P.call(this); } return new C().v;", "1");
    // and a native has the same prototype
    expect("return typeof Math.max.apply;", "function");
}

// A FUNCTION HAS A [[Prototype]] OF ITS OWN, and it is not its `prototype`
// property. Babel's `_inherits` sets both - the subclass's prototype property
// so instances find inherited methods, and the subclass FUNCTION so STATICS are
// inherited - and answering null for a function broke every transpiled
// `extends`.
void test_function_prototype_link() {
    expect("function B() {} function D() {} Object.setPrototypeOf(D, B); "
           "return Object.getPrototypeOf(D) === B;",
           "true");
    // static inheritance through it
    expect("function B() {} B.tag = 'base'; function D() {} Object.setPrototypeOf(D, B); "
           "return D.tag;",
           "base");
    // two levels
    expect("function A() {} A.tag = 'a'; function B() {} function C() {} "
           "Object.setPrototypeOf(B, A); Object.setPrototypeOf(C, B); return C.tag;",
           "a");
    // an ordinary object still answers with its own prototype
    expect("const base = {}; const o = Object.create(base); "
           "return Object.getPrototypeOf(o) === base;",
           "true");
}

// TYPED ARRAYS COERCE ON WRITE, which is the whole of what makes them typed.
// Stored as ordinary arrays of values rather than packed bytes - that buys the
// existing array machinery for nothing - but a shortcut on the coercion would
// have been a silent wrong answer in exactly the place it matters most.
void test_typed_arrays() {
    expect("const a = new Uint8Array(3); return a.length + '|' + a[0];", "3|0");
    expect("const a = new Uint8Array([1, 2, 3]); return a.join(',');", "1,2,3");
    // wrapping, and the one type that CLAMPS instead - it is the pixel type
    expect("const a = new Uint8Array(1); a[0] = 300; return a[0];", "44");
    expect("const a = new Uint8ClampedArray(1); a[0] = 300; return a[0];", "255");
    expect("const a = new Uint8ClampedArray(1); a[0] = -5; return a[0];", "0");
    expect("const a = new Int8Array(1); a[0] = 200; return a[0];", "-56");
    expect("const a = new Int32Array(1); a[0] = 2147483648; return a[0];", "-2147483648");
    // a float32 loses precision a double would keep, which is observable
    expect("const a = new Float32Array(1); a[0] = 0.1; return a[0] === 0.1;", "false");
    expect("const a = new Float64Array(1); a[0] = 0.1; return a[0] === 0.1;", "true");
    // and a typed array does NOT grow: a write past the end is dropped
    expect("const a = new Uint8Array(2); a[5] = 1; return a.length;", "2");
    expect("return Uint16Array.BYTES_PER_ELEMENT;", "2");
    expect("const a = new Uint8Array(4); a.set([9, 8], 1); return a.join(',');", "0,9,8,0");
    expect("const a = new Uint8Array([1, 2, 3, 4]); return a.subarray(1, 3).join(',');", "2,3");
}

void test_object_prototype() {
    expect("const o = { a: 1 }; return o.hasOwnProperty('a') + '|' + o.hasOwnProperty('b');",
           "true|false");
    // an INHERITED property is not an own one, which is the whole question
    expect("const base = { a: 1 }; const o = Object.create(base); "
           "return o.a + '|' + o.hasOwnProperty('a');",
           "1|false");
    expect("const o = { get v() { return 1; } }; return o.hasOwnProperty('v');", "true");
    expect("const base = {}; const o = Object.create(base); return base.isPrototypeOf(o);", "true");
    expect("return ({}).toString();", "[object Object]");
    expect("return [1, 2].hasOwnProperty('length');", "true");
}

void test_string_statics() {
    expect("return String.fromCharCode(104, 105);", "hi");
    expect("return String.fromCharCode.apply(null, [97, 98, 99]);", "abc");
    expect("return String(42);", "42");
    // above 0x7F encodes as UTF-8, because strings here are bytes
    expect("return String.fromCharCode(233).length;", "2");
}

// A DECLARATION SHADOWS, IT DOES NOT WRITE THROUGH.
//
// Deciding whether a new binding needs a slot asked whether the name existed
// ANYWHERE in the frame - and a hoisted name is function-scoped, so a `const`
// inside a block reused the outer slot and assigned through to it.
//
// p5.js has a top-level `function boolean(...)` and, in a block,
// `for (const { arity, boolean } of OperatorTable)`. Both wrote to one cell, so
// zod's builder became the boolean `true` - and the failure surfaced 25,000
// instructions later, in a different function, as "a captured variable is
// boolean (true), not a function".
void test_a_declaration_shadows() {
    // a block-scoped const over a hoisted function of the same name
    expect("function f() { function v() { return 'fn'; } "
           "{ const v = 'shadow'; } return v(); } return f();",
           "fn");
    // ...and the shadow is visible inside the block
    expect("function f() { function v() { return 'fn'; } "
           "{ const v = 'shadow'; return v; } } return f();",
           "shadow");
    // the same through a DESTRUCTURING, which is the shape p5 has
    expect("function f() { function boolean(x) { return 'fn:' + x; } "
           "for (const { boolean } of [{ boolean: true }]) { } "
           "return boolean(1); } return f();",
           "fn:1");
    expect("function f() { function boolean() { return 'fn'; } let seen; "
           "for (const { boolean } of [{ boolean: true }]) { seen = boolean; } "
           "return seen + '|' + boolean(); } return f();",
           "true|fn");
    // a captured binding survives a shadow in a sibling block
    expect("function f() { function v() { return 'fn'; } const get = () => v(); "
           "{ const v = 1; } return get(); } return f();",
           "fn");
    // and a var of the same name at function scope still REUSES its slot
    expect("function f() { var x = 1; var x = 2; return x; } return f();", "2");
}

// A PROMISE CAN BE PENDING. It was settled-only - created already resolved,
// `then` running immediately, `new Promise(executor)` absent because an
// executor implies pending state. p5.js opens with
// `Promise.all([waitForDocumentReady(), ...]).then(_globalInit)`, so the
// library could not begin without it.
//
// Still missing, and a real difference: there is no MICROTASK QUEUE. A handler
// runs the moment the promise settles rather than at the end of the turn.
void test_pending_promises() {
    expect("let seen = 0; new Promise(r => r(5)).then(v => { seen = v; }); return seen;", "5");
    // pending until something resolves it, and the handler runs then
    expect("let seen = 0; let go; const p = new Promise(r => { go = r; }); "
           "p.then(v => { seen = v; }); const before = seen; go(7); return before + '|' + seen;",
           "0|7");
    // reject reaches catch, not then
    expect("let ok = '', bad = ''; new Promise((r, j) => j('no')).then(v => { ok = v; }, "
           "e => { bad = e; }); return ok + '|' + bad;",
           "|no");
    expect("let bad = ''; new Promise((r, j) => j('x')).catch(e => { bad = e; }); return bad;",
           "x");
    // a rejection passes THROUGH a bare then to a later catch
    expect("let bad = ''; new Promise((r, j) => j('y')).then(v => v).catch(e => { bad = e; }); "
           "return bad;",
           "y");
    // then CHAINS: the next promise gets what the handler returned
    expect("let seen = 0; new Promise(r => r(1)).then(v => v + 1).then(v => { seen = v; }); "
           "return seen;",
           "2");
    // a handler returning a promise is adopted rather than nested
    expect("let seen = 0; new Promise(r => r(1)).then(v => new Promise(r2 => r2(v + 10)))"
           ".then(v => { seen = v; }); return seen;",
           "11");
    // settle once: a second resolve is ignored
    expect("let seen = 0; new Promise(r => { r(1); r(2); }).then(v => { seen = v; }); return seen;",
           "1");
    // Promise.all over already-settled promises
    expect("let seen = ''; Promise.all([Promise.resolve(1), Promise.resolve(2)])"
           ".then(v => { seen = v.join(','); }); return seen;",
           "1,2");
}

// `f.toString()` RETURNS THE SOURCE. An engine with no answer here cannot run
// a library that reads its own code, and p5.js's error system parses the
// sketch it was handed. A closure knows which program its protos came from and
// the program keeps its text, so this is a substring rather than a
// reconstruction.
void test_function_to_string() {
    expect("function hi(x) { return x + 1; } return hi.toString();",
           "function hi(x) { return x + 1; }");
    expect("const f = (a, b) => a + b; return f.toString();", "(a, b) => a + b");
    expect("const g = z => z * 2; return g.toString();", "z => z * 2");
    // an expression keeps its own name and body
    expect("const h = function named() { return 1; }; return h.toString();",
           "function named() { return 1; }");
    // a method's span starts at its NAME, which is what a browser returns
    expect("class C { m(a) { return a; } } return C.prototype.m.toString();", "m(a) { return a; }");
    expect("const o = { go(n) { return n; } }; return o.go.toString();", "go(n) { return n; }");
    // a native says so rather than returning something a parser would accept
    expect("return Math.max.toString().indexOf('native code') >= 0;", "true");
}

void test_typeof() {
    diff_vs_v1("typeof 1", "number");
    diff_vs_v1("typeof 'x'", "string");
    diff_vs_v1("typeof true", "boolean");
    diff_vs_v1("typeof undefined", "undefined");
    diff_vs_v1("typeof null", "object"); // the famous wart
}

void test_variables_and_control_flow() {
    expect("let x = 5; return x;", "5");
    expect("let x = 1; x = x + 41; return x;", "42");
    expect("let x = 0; if (1 < 2) { x = 10; } else { x = 20; } return x;", "10");
    expect("let x = 0; if (1 > 2) { x = 10; } else { x = 20; } return x;", "20");
    expect("let n = 0; while (n < 5) { n = n + 1; } return n;", "5");
    expect("let s = 0; for (let i = 0; i < 5; i = i + 1) { s = s + i; } return s;", "10");
    expect("let s = 0; for (let i = 0; i < 10; i++) { s = s + i; } return s;", "45");
    expect("return 1 < 2 ? 'yes' : 'no';", "yes");
}

void test_increment_semantics() {
    expect("let i = 5; let a = i++; return a;", "5"); // postfix yields the old value
    expect("let i = 5; let a = i++; return i;", "6");
    expect("let i = 5; let a = ++i; return a;", "6"); // prefix yields the new one
    expect("let i = 5; i--; return i;", "4");
}

void test_functions() {
    expect("function add(a, b) { return a + b; } return add(2, 3);", "5");
    expect("function f() { return 7; } return f();", "7");
    expect("function id(x) { return x; } return id('hi');", "hi");
    // hoisting: callable before its declaration appears
    expect("let r = twice(21); function twice(n) { return n * 2; } return r;", "42");
    // recursion, and therefore real frame handling
    expect(
        "function fact(n) { if (n <= 1) { return 1; } return n * fact(n - 1); } return fact(10);",
        "3628800");
    expect(
        "function fib(n) { if (n < 2) { return n; } return fib(n-1) + fib(n-2); } return fib(20);",
        "6765");
    // a missing argument is undefined, not an error
    expect("function f(a, b) { return typeof b; } return f(1);", "undefined");
    // function expressions and arrows
    expect("let f = function(x) { return x * 3; }; return f(4);", "12");
    expect("let f = (x) => x + 1; return f(41);", "42");
    expect("let f = x => x * x; return f(9);", "81");
}

void test_objects_and_arrays() {
    expect("let o = { a: 1, b: 2 }; return o.a + o.b;", "3");
    expect("let o = { name: 'ctbrowser' }; return o.name;", "ctbrowser");
    expect("let o = {}; o.x = 5; return o.x;", "5");
    expect("let o = { a: 1 }; return o.missing;", "undefined");
    expect("let a = [1, 2, 3]; return a[0] + a[2];", "4");
    expect("let a = [1, 2, 3]; return a.length;", "3");
    expect("let a = []; a[0] = 'x'; return a[0];", "x");
    expect("let a = [1,2,3]; a[1] = 9; return a[1];", "9");
    expect("return 'hello'.length;", "5");
    expect("let o = { a: 1 }; return o['a'];", "1");
    expect("let a = [1,2,3]; let s = 0; for (let i = 0; i < a.length; i++) { s = s + a[i]; } "
           "return s;",
           "6");
    // nested structure
    expect("let o = { inner: { deep: 42 } }; return o.inner.deep;", "42");
}

// The most common shape in JavaScript: script-level state mutated by a
// function declared beside it. Script scope is global scope here, so this must
// simply work rather than hit the enclosing-local refusal.
void test_script_scope_is_shared_with_functions() {
    expect("let count = 0; function inc() { count = count + 1; } inc(); inc(); return count;", "2");
    expect("let name = 'ctbrowser'; function greet() { return 'hi ' + name; } return greet();",
           "hi ctbrowser");
    expect("let total = 0; function addAll(a) { for (let i = 0; i < a.length; i++) "
           "{ total = total + a[i]; } } addAll([1,2,3,4]); return total;",
           "10");
}

// Real closures, via boxed cells. Every case here is one that capture-by-value
// would get WRONG rather than merely slow, which is why the compiler used to
// refuse instead of guessing.
void test_closures() {
    // the counter: the canonical case. Capture-by-value returns 0 forever.
    expect("function counter() { let n = 0; function inc() { n = n + 1; return n; } return inc; }"
           "let c = counter(); c(); c(); return c();",
           "3");

    // two closures over the SAME cell must see each other's writes
    expect("function pair() { let n = 0;"
           "  return { bump: function() { n = n + 1; }, read: function() { return n; } }; }"
           "let p = pair(); p.bump(); p.bump(); return p.read();",
           "2");

    // separate invocations must NOT share a cell
    expect("function counter() { let n = 0; return function() { n = n + 1; return n; }; }"
           "let a = counter(); let b = counter(); a(); a(); return a() + ',' + b();",
           "3,1");

    // capture of a parameter, not just a local
    expect("function adder(x) { return function(y) { return x + y; }; }"
           "let add5 = adder(5); return add5(37);",
           "42");

    // TWO levels of nesting: the innermost function captures a variable from
    // its grandparent, which only works if the middle function re-exports it
    // as its own upvalue
    expect("function outer() { let v = 'deep';"
           "  function middle() { function inner() { return v; } return inner(); }"
           "  return middle(); }"
           "return outer();",
           "deep");

    // mutation from the innermost level, visible at the outermost
    expect("function outer() { let n = 1;"
           "  function middle() { function inner() { n = n * 10; } inner(); }"
           "  middle(); middle(); return n; }"
           "return outer();",
           "100");

    // a closure outliving the frame that created it - the cell must survive
    expect("function make() { let msg = 'alive'; return function() { return msg; }; }"
           "let f = make(); return f();",
           "alive");

    // closures in a loop capture the loop's binding
    expect("function build() { let fns = []; let i = 0;"
           "  while (i < 3) { fns[i] = function() { return i; }; i = i + 1; }"
           "  return fns[0](); }"
           "return build();",
           "3"); // `let i` outside the loop body is ONE binding, so all three see 3
}

// A captured cell is reachable only through the closure holding it, so the
// collector has to trace closure upvalues. If it does not, the cell is freed
// while the closure is still live and the closure returns garbage.
void test_gc_traces_captured_cells() {
    context cx;
    const program prog = compiler::compile("function make(v) { return function() { return v; }; }"
                                           "let keep = make(99);"
                                           "for (let i = 0; i < 100; i++) { let junk = make(i); }"
                                           "return 0;");
    const run_result r = cx.run(prog);
    CHECK(r.ok);
    const std::size_t freed = cx.collect();
    CHECK(freed > 0); // the 100 discarded closures and their cells

    // and the surviving closure still works after the sweep
    const program check = compiler::compile("function make(v) { return function() { return v; }; }"
                                            "let keep = make(99); return keep();");
    context cx2;
    const run_result r2 = cx2.run(check);
    CHECK(r2.ok);
    CHECK_EQ(cx2.to_string(r2.returned), std::string{"99"});
}

void test_native_bindings() {
    const program prog = compiler::compile("return double(21) + double(0);");
    context cx;
    cx.define_native("double", [](context &, std::span<value> args) {
        const double n = args.empty() ? 0 : context::to_number(args[0]);
        return value::number(n * 2);
    });
    const run_result r = cx.run(prog);
    CHECK(r.ok);
    CHECK_EQ(cx.to_string(r.returned), std::string{"42"});
}

void test_gc_collects_unreachable() {
    context cx;
    const program prog =
        compiler::compile("let keep = { alive: 1 };"
                          "for (let i = 0; i < 200; i++) { let junk = { dead: i }; }"
                          "return keep.alive;");
    const run_result r = cx.run(prog);
    CHECK(r.ok);
    CHECK_EQ(cx.to_string(r.returned), std::string{"1"});

    const std::size_t before = cx.live_objects();
    const std::size_t freed = cx.collect();
    // The loop allocated hundreds of objects that nothing can reach; a
    // collector that frees none of them is not working.
    CHECK(freed > 0);
    CHECK(cx.live_objects() < before);
    std::printf("  gc: %zu live -> %zu after freeing %zu\n", before, cx.live_objects(), freed);
}

void test_errors_are_reported_not_crashes() {
    bool ok = true;
    const std::string out = run_vm("let x = 1; x();", &ok);
    CHECK(!ok);
    // The message NAMES what was called and what it turned out to be, and
    // carries the stack it happened on. "attempted to call a non-function" is
    // true and useless; in a 4.5 MB bundle it is the difference between a
    // diagnostic and a shrug.
    CHECK(out.find("not a function") != std::string::npos);
    CHECK(out.find("`x`") != std::string::npos);
    CHECK(out.find("number") != std::string::npos);
    CHECK(out.find("at <script>") != std::string::npos);

    // A method call names the method; a call on a call names the inner one.
    bool method_ok = true;
    const std::string method_out = run_vm("const o = {}; o.missing();", &method_ok);
    CHECK(method_out.find("`missing`") != std::string::npos);
    bool chain_ok = true;
    const std::string chain_out = run_vm("function f() { return undefined; } f()();", &chain_ok);
    CHECK(chain_out.find("`f()`") != std::string::npos);

    const program bad = compiler::compile("let x = ;");
    CHECK(!bad.ok);
}

// --- stage 2: the things ordinary code needs ------------------------------

void test_compound_assignment() {
    // `x += 1` is in essentially every script ever written, and it did not
    // compile at all - the compiler rejected the whole program.
    expect_result("var x = 1; x += 4; return x;", "5");
    expect_result("var x = 10; x -= 4; return x;", "6");
    expect_result("var x = 3; x *= 4; return x;", "12");
    expect_result("var x = 12; x /= 4; return x;", "3");
    expect_result("var x = 13; x %= 5; return x;", "3");
    expect_result("var s = 'a'; s += 'b'; return s;", "ab"); // += concatenates strings
    expect_result("var o = {n: 1}; o.n += 5; return o.n;", "6");
    expect_result("var a = [1,2]; a[0] += 9; return a[0];", "10");
    expect_result("var x = 0; function f() { x += 2; } f(); f(); return x;", "4");
}

void test_compound_assignment_evaluates_its_target_once() {
    // THE case the reference machinery exists for. If the target were compiled
    // twice - once to read and once to write - this would increment i twice and
    // store into the wrong slot.
    expect_result("var a = [0,0,0]; var i = 0; a[i++] += 5; return i;", "1");
    expect_result("var a = [0,0,0]; var i = 0; a[i++] += 5; return a[0];", "5");
}

void test_update_on_properties_and_indices() {
    expect_result("var o = {n: 1}; o.n++; return o.n;", "2");
    expect_result("var o = {n: 1}; var r = o.n++; return r;", "1"); // postfix: the old value
    expect_result("var o = {n: 1}; var r = ++o.n; return r;", "2"); // prefix: the new one
    expect_result("var a = [5]; a[0]--; return a[0];", "4");
}

void test_this() {
    // `this` compiled to undefined unconditionally, so no method could see the
    // object it was called on - which is most of what objects are for.
    expect_result("var o = {n: 7, get: function () { return this.n; }}; return o.get();", "7");
    // and it follows the CALL SITE, not the definition
    expect_result("var a = {n: 1, get: function () { return this.n; }};"
                  "var b = {n: 2, get: a.get}; return b.get();",
                  "2");
}

void test_break_and_continue() {
    // Without these no loop could exit early: every search loop, every guard,
    // every early-out in a game loop.
    expect_result("var t = 0; for (var i = 0; i < 10; i++) { if (i == 5) { break; } t += 1; }"
                  "return t;",
                  "5");
    expect_result("var t = 0; for (var i = 0; i < 5; i++) { if (i == 2) { continue; } t += 1; }"
                  "return t;",
                  "4");
    // The subtle one: `continue` in a for-loop must run the UPDATE. If it jumped
    // back to the condition instead, this would never terminate.
    expect_result("var n = 0; for (var i = 0; i < 4; i++) { if (i < 2) { continue; } n += 1; }"
                  "return n;",
                  "2");
    expect_result("var t = 0; var i = 0; while (i < 10) { i += 1; if (i > 3) { break; } t += 1; }"
                  "return t;",
                  "3");
    expect_result("var t = 0; var i = 0; do { i += 1; t += i; } while (i < 3); return t;", "6");
}

void test_labeled_break() {
    // Without the label, `break` leaves only the inner loop - so this would be
    // 3, one break per outer iteration, rather than 1.
    expect_result("var n = 0;"
                  "outer: for (var i = 0; i < 3; i++) {"
                  "  for (var j = 0; j < 3; j++) { n += 1; if (j == 0) { break outer; } }"
                  "} return n;",
                  "1");
    expect_result("var n = 0;"
                  "outer: for (var i = 0; i < 3; i++) {"
                  "  for (var j = 0; j < 3; j++) { if (j == 0) { continue outer; } n += 1; }"
                  "} return n;",
                  "0");
}

void test_try_catch() {
    expect_result("var r = 0; try { throw 7; } catch (e) { r = e; } return r;", "7");
    expect_result("var r = 0; try { r = 1; } catch (e) { r = 2; } return r;", "1");
    expect_result("var r = ''; try { throw 'boom'; } catch (e) { r = e; } return r;", "boom");
}

void test_exceptions_unwind_call_frames() {
    // The reason exceptions are a VM change and not a compiler one: a throw
    // several frames deep has to reach a try in a caller, discarding the frames
    // in between.
    expect_result("function deep() { throw 42; }"
                  "function middle() { deep(); return 1; }"
                  "var r = 0; try { middle(); } catch (e) { r = e; } return r;",
                  "42");
    expect_result("function f() { try { throw 1; } catch (e) { return 5; } return 9; }"
                  "return f();",
                  "5");
}

void test_finally() {
    expect_result("var r = 0; try { r = 1; } finally { r += 10; } return r;", "11");
    expect_result("var r = 0; try { throw 1; } catch (e) { r = 2; } finally { r += 10; }"
                  "return r;",
                  "12");
}

void test_nested_try() {
    expect_result("var r = 0;"
                  "try { try { throw 1; } catch (e) { r = 1; throw 2; } } catch (e) { r += e; }"
                  "return r;",
                  "3");
}

void test_break_out_of_try() {
    // Jumping out of a try block has to drop its handler. If it does not, the
    // catch stays reachable after the loop and a later throw lands in dead
    // code - a crash, not a wrong answer.
    expect_result("var n = 0;"
                  "for (var i = 0; i < 3; i++) { try { n += 1; break; } catch (e) { n = 99; } }"
                  "var caught = 0; try { throw 5; } catch (e) { caught = e; } return caught;",
                  "5");
}

// --- stage 3: the standard library ----------------------------------------

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

void test_object_statics() {
    expect_result("return Object.keys({a: 1, b: 2}).join(',');", "a,b");
    expect_result("return Object.values({a: 1, b: 2}).join(',');", "1,2");
    expect_result("var t = Object.assign({}, {a: 1}, {b: 2}); return t.a + t.b;", "3");
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

void test_own_properties_beat_the_prototype() {
    // A page that puts its own `join` on an object must get its own, not the
    // array method. Prototype lookup has to come SECOND.
    expect_result("var o = {join: function () { return 'mine'; }}; return o.join();", "mine");
}

void test_computed_and_named_lookup_agree() {
    // `a["length"]` and `a.length` went down different paths, so a page that
    // computed a property name silently saw undefined.
    expect_result("var a = [1,2,3]; return a['length'];", "3");
    expect_result("var s = 'abc'; return s['length'];", "3");
    expect_result("var a = [1,2]; var m = 'push'; a[m](3); return a.length;", "3");
}

// --- stage 4: the rest of the language ------------------------------------

void test_for_of() {
    expect_result("var t = 0; for (const x of [1,2,3]) { t += x; } return t;", "6");
    expect_result("var s = ''; for (const c of 'abc') { s += c; } return s;", "abc");
    expect_result("var t = 0; for (const x of [1,2,3]) { if (x == 2) { continue; } t += x; }"
                  "return t;",
                  "4");
    expect_result("var t = 0; for (const x of [1,2,3]) { if (x == 2) { break; } t += x; }"
                  "return t;",
                  "1");
    // The loop variable is per-iteration, so a closure made in the body sees
    // THIS element and not the last one.
    expect_result("var fns = []; for (const x of [1,2,3]) { fns.push(function () { return x; }); }"
                  "return fns[0]() + fns[2]();",
                  "4");
}

void test_for_in() {
    expect_result("var keys = ''; for (const k in {a: 1, b: 2}) { keys += k; } return keys;", "ab");
    expect_result("var t = 0; var o = {a: 1, b: 2}; for (const k in o) { t += o[k]; } return t;",
                  "3");
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

void test_switch() {
    expect_result("var r = 0; switch (2) { case 1: r = 10; break; case 2: r = 20; break; }"
                  "return r;",
                  "20");
    expect_result("var r = 0; switch (9) { case 1: r = 10; break; default: r = 99; } return r;",
                  "99");
    // FALLTHROUGH is the behaviour code relies on, so it has to be preserved.
    expect_result("var r = ''; switch (1) { case 1: r += 'a'; case 2: r += 'b'; break;"
                  "  case 3: r += 'c'; } return r;",
                  "ab");
    // switch matches STRICTLY - `switch (1)` does not match `case '1'`.
    expect_result("var r = 'no'; switch (1) { case '1': r = 'yes'; break; } return r;", "no");
}

void test_new_and_classes() {
    expect_result("class P { constructor(n) { this.n = n; } }"
                  "var p = new P(7); return p.n;",
                  "7");
    expect_result("class P { constructor(n) { this.n = n; } double() { return this.n * 2; } }"
                  "return new P(4).double();",
                  "8");
    // A method lives on the prototype, so two instances share one function and
    // both find it.
    expect_result("class P { hi() { return 'hi'; } }"
                  "var a = new P(); var b = new P(); return a.hi() + b.hi();",
                  "hihi");
    expect_result("class P { static make() { return 'static'; } } return P.make();", "static");
    // `extends` chains the prototypes, so an inherited method is reachable.
    expect_result("class A { who() { return 'A'; } }"
                  "class B extends A { } return new B().who();",
                  "A");
    expect_result("class A { who() { return 'A'; } }"
                  "class B extends A { who() { return 'B'; } } return new B().who();",
                  "B");

    // `super(...)` runs the parent constructor against the SAME object, so the
    // fields it sets are on the instance.
    expect_result("class A { constructor(n) { this.n = n; } }"
                  "class B extends A { constructor() { super(3); this.m = 4; } }"
                  "var b = new B(); return b.n + b.m;",
                  "7");
    // `super.m()` calls the parent's version, and `this` inside it is still the
    // instance.
    expect_result("class A { label() { return 'A' + this.n; } }"
                  "class B extends A { constructor() { this.n = 1; }"
                  "  label() { return super.label() + 'B'; } } return new B().label();",
                  "A1B");
    // Three deep. This is what resolving super against `this` gets wrong: C's
    // method would find itself again and recurse until the stack gave out.
    expect_result("class A { who() { return 'A'; } }"
                  "class B extends A { who() { return super.who() + 'B'; } }"
                  "class C extends B { who() { return super.who() + 'C'; } }"
                  "return new C().who();",
                  "ABC");
    expect_result("class A { }; return typeof new A().constructor;", "function");
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

// A NAMED FUNCTION EXPRESSION BINDS ITS OWN NAME.
//
// `function me() {}` as an EXPRESSION puts `me` in scope inside its own body
// and nowhere else. It is the only way an otherwise-anonymous function can
// reach itself, and every self-driving callback is written that way:
// `(function pump() { requestAnimationFrame(pump); })()` never ran a second
// time here, silently, because a callback that is undefined is not an error at
// the point it is registered.
// `obj[key](...)` PASSES ITS ARGUMENTS, not the key.
//
// call_computed reads its arguments from base+1 upwards, and the compiler was
// evaluating the key into that same register - so every computed call with
// arguments arrived shifted by one, with the KEY as argument 0 and the last
// argument dropped. Silent, and only in the form with arguments, which is why
// `xs[0]()` looked fine.
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

// CALLING A NON-FUNCTION IS A CATCHABLE TypeError.
//
// It used to end the run outright. Pages catch it - feature detection is
// written as `try { thing() } catch (e) {}` at least as often as a typeof test -
// and an uncatchable fault also unwinds nothing, so a probe wrapped in
// try/catch reported no error and the failure appeared to come from wherever
// the run happened to stop.
void test_calling_a_non_function_throws() {
    expect_result("try { undefined(); } catch (e) { return e.name; } return 'not thrown';",
                  "TypeError");
    expect_result("try { ({}).nope(); } catch (e) { return e.name; } return 'not thrown';",
                  "TypeError");
    expect_result("try { new (undefined)(); } catch (e) { return e.name; } return 'not thrown';",
                  "TypeError");
    expect_result("try { undefined(); } catch (e) { return e instanceof Error; } return false;",
                  "true");
    // ...and it names what was called, which is the whole diagnostic.
    expect_result("try { ({}).missing(); } catch (e) {"
                  "  return e.message.indexOf('missing') >= 0; } return false;",
                  "true");
    // The run CONTINUES afterwards, which is what "catchable" means.
    expect_result("let n = 0; try { undefined(); } catch (e) { n = 1; } n += 1; return n;", "2");
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

void test_computed_calls_pass_their_arguments() {
    expect_result("var t = { k: function (a, b, c) { return a + '|' + b + '|' + c; } };"
                  "return t['k'](1, 2, 3);",
                  "1|2|3");
    // The key as an expression, which is the form a dispatch table takes.
    expect_result("var t = { k: function (a, b) { return a + '|' + b; } }; var self = { m: 'k' };"
                  "return t[self.m](1, 2);",
                  "1|2");
    // ...and the receiver is still the object, so `this` works.
    expect_result("var t = { n: 7, k: function (a) { return this.n + a; } }; return t['k'](1);",
                  "8");
    expect_result("var o = { m: 'k', t: { k: function (a, b) { return a + b; } },"
                  "          go: function () { return this.t[this.m](3, 4); } }; return o.go();",
                  "7");
}

// `arguments` - every value a call actually received.
//
// It has to be materialised at ENTRY. Built where the name is mentioned, it
// read the frame's registers as they were at that moment, and the expression
// around it had already reused the ones holding arguments past the last
// declared parameter.
void test_arguments() {
    expect_result("function f(a, b, c) { return arguments.length; } return f(1);", "1");
    expect_result("function f(a, b, c) { return arguments.length; } return f(1, 2, 3, 4);", "4");
    expect_result("function g() { return arguments[0] + ',' + arguments[1]; } return g('x', 'y');",
                  "x,y");
    // Array-like enough for the commonest thing done with it.
    expect_result("function h() { return Array.prototype.slice.call(arguments).join('|'); }"
                  "return h(1, 2, 3);",
                  "1|2|3");
    // ONE object per call, not one per mention.
    expect_result("function s() { return arguments === arguments; } return s(1);", "true");
    // An arrow has none of its own and sees the enclosing function's.
    expect_result("function outer(a, b) { var f = () => arguments.length + ':' + arguments[1];"
                  "return f(); } return outer(1, 2, 3);",
                  "3:2");
    // A nested function gets its own.
    expect_result("function nest() { function inner(x) { return arguments.length; }"
                  "return inner(1, 2, 3); } return nest();",
                  "3");
    // A parameter of that name shadows it, which is what makes this safe to add.
    expect_result("function shadow(arguments) { return arguments; } return shadow(7);", "7");
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

void test_named_function_expressions() {
    expect_result("var f = function me(n) { return n <= 0 ? 'done' : me(n - 1); };"
                  "return f(3);",
                  "done");
    expect_result("var g = function named() { return typeof named; }; return g();", "function");
    // ...and NOWHERE ELSE: the name is not a declaration in the enclosing scope.
    expect_result("var g = function named() { return 1; }; return typeof named;", "undefined");
    // A parameter of the same name shadows the binding, which is what makes it
    // safe to add: it can only ever be read where nothing else defined the name.
    expect_result("var f = function me(me) { return me; }; return f(7);", "7");
    // The binding survives capture, so a nested function sees it too.
    expect_result("var f = function me(n) { return (function () { return typeof me; })(); };"
                  "return f(1);",
                  "function");
    // A declaration is unaffected - its name is in the enclosing scope as well.
    expect_result("function d() { return typeof d; } return d() + ',' + typeof d;",
                  "function,function");
}

void test_async_and_promises() {
    // Promises are SETTLED on creation - there is no job queue - so `await`
    // reads the value straight out and `then` runs at once. That is the subset,
    // and it is what `await fetch(url)` is written against.
    expect_result("return await 3;", "3"); // awaiting a plain value is the value
    expect_result("async function f() { return 5; } return await f();", "5");
    // An async function hands back a PROMISE, not the bare value - otherwise
    // `f().then(...)` has nothing to call.
    expect_result("async function f() { return 5; } return typeof f().then;", "function");
    expect_result("async function f() { return 5; } var r = 0; f().then(function (v) { r = v; });"
                  "return r;",
                  "5");
    expect_result("async function f() { return 1; } async function g() { return await f() + 1; }"
                  "return await g();",
                  "2");
    // await inside a loop, which is the fetchboard shape.
    expect_result(
        "async function one(n) { return n * 2; }"
        "async function run() { var t = 0; for (const n of [1,2,3]) { t += await one(n); }"
        "  return t; } return await run();",
        "12");

    expect_result("return await Promise.resolve(7);", "7");
    expect_result("var r = 0; Promise.resolve(2).then(function (v) { r = v * 3; }); return r;",
                  "6");
    // then() chains: each callback's return settles the next promise.
    expect_result("var r = 0; Promise.resolve(2).then(function (v) { return v + 1; })"
                  "  .then(function (v) { r = v; }); return r;",
                  "3");
    // A rejected promise skips then and reaches catch.
    expect_result("var r = 'none'; Promise.reject('bad').then(function () { r = 'ran'; })"
                  "  .catch(function (e) { r = e; }); return r;",
                  "bad");
    expect_result("var r = 0; Promise.reject(1).catch(function () { return 9; })"
                  "  .then(function (v) { r = v; }); return r;",
                  "9");
    expect_result("var r = 0; Promise.resolve(1).finally(function () { r = 5; }); return r;", "5");
    expect_result("var p = await Promise.all([Promise.resolve(1), 2]); return p.join(',');", "1,2");

    // Awaiting a rejected promise THROWS, which is what makes try/catch around
    // an await behave the way pages assume.
    expect_result("var r = 0; try { await Promise.reject(9); } catch (e) { r = e; } return r;",
                  "9");
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

void test_object_literal_keys() {
    expect_result("var k = 'x'; var o = {[k]: 5}; return o.x;", "5");
    expect_result("var o = {'a b': 1}; return o['a b'];", "1");
    expect_result("var o = {1: 'one'}; return o[1];", "one");
    // Spread copies own properties in sequence, so a later key still wins.
    expect_result("var a = {x: 1, y: 2}; var b = {...a, y: 3}; return b.x + b.y;", "4");
    expect_result("var a = {x: 1}; var b = {...a}; b.x = 9; return a.x;", "1"); // a copy
    expect_result("var a = {x: 1}; var b = {y: 2, ...a}; var keys = '';"
                  "for (const k in b) { keys += k; } return keys;",
                  "yx");
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
    test_default_parameters();
    test_rest_parameters();
    test_nested_function_declarations_are_local();
    test_arrow_this_is_lexical();
    test_class_fields_are_per_instance();
    test_bitwise_compound_assignment();
    test_spread_in_a_call();
    test_private_names_are_distinct();
    test_destructuring_declarations();
    test_destructuring_parameters();
    test_destructuring_assignment();
    test_destructuring_in_for_of();
    test_regex();
    test_accessors();
    test_object_descriptors();
    test_optional_chain_short_circuits();
    test_symbol();
    test_collections();
    test_errors();
    test_implicit_super();
    test_class_expressions();
    test_named_class_expression_binds_itself();
    test_class_declaration_survives_the_statement();
    test_chain_state_does_not_leak_into_a_nested_function();
    test_stdlib_additions();
    test_comma_operator_evaluates_everything();
    test_functions_have_a_prototype();
    test_proxy();
    test_define_property_attributes_only();
    test_function_prototype();
    test_function_prototype_link();
    test_typed_arrays();
    test_object_prototype();
    test_string_statics();
    test_a_declaration_shadows();
    test_pending_promises();
    test_function_to_string();
    test_typeof();
    test_variables_and_control_flow();
    test_increment_semantics();
    test_functions();
    test_objects_and_arrays();
    test_script_scope_is_shared_with_functions();
    test_closures();
    test_gc_traces_captured_cells();
    test_native_bindings();
    test_gc_collects_unreachable();
    test_errors_are_reported_not_crashes();
    test_compound_assignment();
    test_compound_assignment_evaluates_its_target_once();
    test_update_on_properties_and_indices();
    test_this();
    test_break_and_continue();
    test_labeled_break();
    test_try_catch();
    test_exceptions_unwind_call_frames();
    test_finally();
    test_nested_try();
    test_break_out_of_try();

    test_math();
    test_math_random_is_in_range_and_moves();
    test_array_methods();
    test_array_iteration_calls_back_into_the_vm();
    test_array_sort();
    test_string_methods();
    test_number_and_conversions();
    test_object_statics();
    test_json();
    test_own_properties_beat_the_prototype();
    test_computed_and_named_lookup_agree();

    test_for_of();
    test_for_in();
    test_template_literals();
    test_switch();
    test_new_and_classes();
    test_optional_chaining();
    test_spread();
    test_string_identity();
    test_bitwise_and_friends();
    test_delete_in_instanceof();
    test_object_literal_keys();
    test_type_identification();
    test_type_tags();
    test_calling_a_non_function_throws();
    test_string_match();
    test_structured_clone();
    test_computed_calls_pass_their_arguments();
    test_arguments();
    test_number_and_string_conversions();
    test_named_function_expressions();
    test_async_and_promises();

    REPORT("vm_basics");
}
