// Functions: parameters, `this`, `arguments`, closures and the scopes they
// capture, and the function object itself. Carved out of js/vm_basics.cpp on
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

// HALF OF A SIGNATURE USED TO BE DROPPED.
//
// The parser has always carried both - a default is the parameter node's `a`
// child and a rest is `d == 1` - and the compiler read neither. So an omitted
// argument stayed undefined instead of taking its default, and `...rest` bound
// the one positional argument in that slot rather than an array of what was
// left. Silent in both directions.
void test_default_parameters() {
    expect_result("function f(a, b = 2) { return a + b; } return f(1);", "3");
    expect_result("function f(a, b = 2) { return a + b; } return f(1, 10);", "11");
    // undefined takes the default; null does NOT - they are different questions
    expect_result("function f(a = 5) { return a; } return f(undefined);", "5");
    expect_result("function f(a = 5) { return a; } return f(null);", "null");
    // a default may be an expression, and may see earlier parameters
    expect_result("function f(a, b = a * 2) { return b; } return f(4);", "8");
    // and it is only evaluated when it is needed
    expect_result("let hit = 0; function d() { hit = 1; return 1; } "
                  "function f(a = d()) { return a; } f(9); return hit;",
                  "0");
    // arrow functions take the same path
    expect_result("const f = (a, b = 3) => a + b; return f(1);", "4");
}

void test_rest_parameters() {
    expect_result("function f(...rest) { return rest.length; } return f(1, 2, 3);", "3");
    expect_result("function f(...rest) { return rest[1]; } return f('a', 'b');", "b");
    expect_result("function f(a, ...rest) { return rest.join('-'); } return f(1, 2, 3, 4);",
                  "2-3-4");
    // no extra arguments is an EMPTY array, not undefined
    expect_result("function f(a, ...rest) { return rest.length; } return f(1);", "0");
    expect_result(
        "function f(...rest) { return Array.isArray(rest) && rest.length === 0; } return f();",
        "true");
    // the rest array is a real array and the locals after it are undisturbed
    expect_result(
        "function f(a, ...rest) { let x = 7; return a + rest.length + x; } return f(1, 2, 3);",
        "10");
    // and a body long enough to reuse the registers the arguments arrived in
    expect_result("function f(a, ...rest) { let p = 1, q = 2, r = 3, s = 4, t = 5; "
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
    expect_result("function outer() { function helper() { return 1; } return helper(); } "
                  "outer(); return typeof helper;",
                  "undefined");
    // two scopes, same name, no collision
    expect_result("function a() { function h() { return 'a'; } return h(); } "
                  "function b() { function h() { return 'b'; } return h(); } "
                  "return a() + b();",
                  "ab");
    // and it still captures the enclosing local rather than a global
    expect_result(
        "var n = 'global'; "
        "function outer() { let n = 'local'; function read() { return n; } return read(); } "
        "return outer();",
        "local");
    // recursion resolves to itself, not to a global of the same name
    expect_result("function outer() { function fact(k) { return k <= 1 ? 1 : k * fact(k - 1); } "
                  "return fact(5); } return outer();",
                  "120");
    // a top-level declaration is STILL a global - pages define functions the
    // host calls by name, and that is deliberate
    expect_result("function top() { return 7; } return typeof top;", "function");
}

// AN ARROW SEES THE `this` WHERE IT WAS WRITTEN. It used to get its own frame
// with an undefined receiver, so `this` inside an arrow inside a method - which
// is where arrows are usually written - was undefined.
void test_arrow_this_is_lexical() {
    expect_result(
        "const o = { n: 5, get() { const f = () => this.n; return f(); } }; return o.get();", "5");
    // through two levels of arrow
    expect_result("const o = { n: 6, get() { const f = () => () => this.n; return f()(); } }; "
                  "return o.get();",
                  "6");
    // an arrow inside a callback still sees the method's object
    expect_result("const o = { n: 3, sum() { return [1, 2].map(x => x * this.n).join(','); } }; "
                  "return o.sum();",
                  "3,6");
    // an ordinary function still gets its OWN receiver
    expect_result("const o = { n: 1, get() { return this.n; } }; return o.get();", "1");
    // and a class method's arrow sees the instance
    expect_result("class C { constructor() { this.v = 9; } read() { const f = () => this.v; "
                  "return f(); } } return new C().read();",
                  "9");
}

// `f(...args)` - the single construct that stopped more of p5.js than any
// other. `nk::spread` was not a case in compile_expr at all, so it reached the
// default arm and refused the whole call.
void test_spread_in_a_call() {
    expect_result("function f(a, b, c) { return a + b + c; } return f(...[1, 2, 3]);", "6");
    // mixed with ordinary arguments, in any position
    expect_result("function f(a, b, c) { return a + '-' + b + '-' + c; } "
                  "return f(1, ...[2, 3]);",
                  "1-2-3");
    expect_result("function f(a, b, c) { return a + '-' + b + '-' + c; } "
                  "return f(...[1, 2], 3);",
                  "1-2-3");
    expect_result("function f(a, b, c, d) { return a + b + c + d; } "
                  "return f(...[1, 2], ...[3, 4]);",
                  "10");
    // a METHOD call keeps its receiver
    expect_result(
        "const o = { n: 10, add(a, b) { return this.n + a + b; } }; return o.add(...[1, 2]);",
        "13");
    // and so does a computed one
    expect_result("const o = { n: 10, add(a, b) { return this.n + a + b; } }; "
                  "const k = 'add'; return o[k](...[1, 2]);",
                  "13");
    // it reaches natives too
    expect_result("return Math.max(...[3, 9, 4]);", "9");
    // spread of a computed array, not just a literal
    expect_result("function f(a, b) { return a * b; } const xs = [3, 4]; return f(...xs);", "12");
    // ...and it composes with a rest parameter on the other side
    expect_result("function f(...rest) { return rest.length; } return f(...[1, 2, 3], 4);", "4");
    expect_result("function f(a, ...rest) { return rest.join(','); } return f(...[1, 2, 3]);",
                  "2,3");
    // `new C(...args)`
    expect_result("class P { constructor(a, b) { this.v = a + b; } } return new P(...[2, 3]).v;",
                  "5");
    expect_result("class P { constructor(a, b, c) { this.v = a + b + c; } } "
                  "return new P(1, ...[2, 3]).v;",
                  "6");
    // a spread of an empty array passes nothing
    expect_result("function f(a) { return typeof a; } return f(...[]);", "undefined");
}

// DESTRUCTURING. A binding position may hold a SHAPE, and every one of these
// used to be a PARSE ERROR: the declarator read `{` as the variable's name and
// the parser desynchronised from there. It stopped seventeen of p5.js's
// seventy-one modules, each at the first destructuring in the file.
void test_destructuring_declarations() {
    expect_result("const {a, b} = {a: 1, b: 2}; return a + b;", "3");
    expect_result("const [x, y] = [3, 4]; return x * y;", "12");
    expect_result("const {a: renamed} = {a: 7}; return renamed;", "7");
    // defaults, and only for undefined
    expect_result("const {a = 5} = {}; return a;", "5");
    expect_result("const {a = 5} = {a: 0}; return a;", "0");
    expect_result("const [p = 1, q = 2] = [9]; return p + ',' + q;", "9,2");
    // holes
    expect_result("const [, second] = ['a', 'b']; return second;", "b");
    // rest, in both shapes
    expect_result("const [head, ...tail] = [1, 2, 3]; return head + '|' + tail.join(',');",
                  "1|2,3");
    expect_result("const {a, ...rest} = {a: 1, b: 2, c: 3}; "
                  "return a + '|' + Object.keys(rest).join(',');",
                  "1|b,c");
    // nested
    expect_result("const {a: {b}} = {a: {b: 'deep'}}; return b;", "deep");
    expect_result("const [[m], [n]] = [[1], [2]]; return m + n;", "3");
    expect_result("const {list: [first]} = {list: ['x']}; return first;", "x");
    // a computed key
    expect_result("const k = 'dyn'; const {[k]: got} = {dyn: 42}; return got;", "42");
    // a missing property is undefined, not an error
    expect_result("const {nope} = {}; return typeof nope;", "undefined");
    // inside a function, so the binding is a local rather than a global
    expect_result("function f(o) { const {a, b} = o; return a - b; } return f({a: 9, b: 4});", "5");
    // and a nested function can capture a name a pattern bound
    expect_result("function f(o) { const {v} = o; const get = () => v; return get(); } "
                  "return f({v: 'captured'});",
                  "captured");
}

void test_destructuring_parameters() {
    expect_result("function f({x, y}) { return x + y; } return f({x: 1, y: 2});", "3");
    expect_result("function f([a, b]) { return a * b; } return f([3, 4]);", "12");
    expect_result("function f({x = 10}) { return x; } return f({});", "10");
    expect_result("const f = ({n}) => n * 2; return f({n: 21});", "42");
    // mixed with ordinary parameters and with a rest
    expect_result("function f(a, {b}, ...rest) { return a + b + rest.length; } "
                  "return f(1, {b: 2}, 9, 9);",
                  "5");
    // a whole-parameter default alongside a pattern
    expect_result("function f({x} = {x: 'fallback'}) { return x; } return f();", "fallback");
}

void test_destructuring_assignment() {
    expect_result("let a, b; [a, b] = [1, 2]; return a + b;", "3");
    expect_result("let a = 1, b = 2; [a, b] = [b, a]; return a + ',' + b;", "2,1");
    expect_result("let x; ({x} = {x: 'set'}); return x;", "set");
    expect_result("let x, y; ({x, y: y} = {x: 1, y: 2}); return x + y;", "3");
    // a hole skips a position - which is why array literals had to learn them
    expect_result("let second; [, second] = ['a', 'b']; return second;", "b");
    // a member expression is a legal target
    expect_result("const o = {}; [o.first] = ['here']; return o.first;", "here");
}

void test_destructuring_in_for_of() {
    expect_result(
        "let sum = 0; for (const [a, b] of [[1, 2], [3, 4]]) { sum += a * b; } return sum;", "14");
    expect_result(
        "let names = ''; for (const {n} of [{n: 'a'}, {n: 'b'}]) { names += n; } return names;",
        "ab");
    // for-in with no declaration keyword, assigning to an existing binding
    expect_result("let k, seen = ''; for (k in {a: 1, b: 2}) { seen += k; } return seen;", "ab");
}

// EVERY FUNCTION HAS A `prototype`, made on first use. `function F() {}; new F()
// instanceof F` is the constructor-function pattern every transpiler emits -
// Babel's own `_classCallCheck` guard is exactly that test - and a plain
// function had none, so `new F()` produced an object with no prototype.
void test_functions_have_a_prototype() {
    expect_result("function F() {} return typeof F.prototype;", "object");
    expect_result("function F() {} return F.prototype.constructor === F;", "true");
    expect_result("function F() {} return new F() instanceof F;", "true");
    // the same object every time, or nothing built on it would hold
    expect_result("function F() {} return F.prototype === F.prototype;", "true");
    // methods installed on it are found by an instance
    expect_result("function F() {} F.prototype.m = function () { return 4; }; return new F().m();",
                  "4");
    // an arrow is not a constructor and gets none
    expect_result("const a = () => 1; return typeof a.prototype;", "undefined");
}

void test_function_prototype() {
    expect_result("function f(a) { return this.v + a; } return f.call({ v: 1 }, 2);", "3");
    expect_result("function f(a) { return this.v + a; } return f.apply({ v: 1 }, [2]);", "3");
    expect_result("function f(a) { return this.v + a; } return f.bind({ v: 1 })(2);", "3");
    // bind is a PARTIAL APPLICATION, not just a receiver change
    expect_result("function f(a, b) { return this.v + a + b; } return f.bind({ v: 1 }, 10)(2);",
                  "13");
    // the constructor-borrowing pattern every transpiler emits
    expect_result("function P() { this.v = 1; } function C() { P.call(this); } return new C().v;",
                  "1");
    // and a native has the same prototype
    expect_result("return typeof Math.max.apply;", "function");
}

// A FUNCTION HAS A [[Prototype]] OF ITS OWN, and it is not its `prototype`
// property. Babel's `_inherits` sets both - the subclass's prototype property
// so instances find inherited methods, and the subclass FUNCTION so STATICS are
// inherited - and answering null for a function broke every transpiled
// `extends`.
void test_function_prototype_link() {
    expect_result("function B() {} function D() {} Object.setPrototypeOf(D, B); "
                  "return Object.getPrototypeOf(D) === B;",
                  "true");
    // static inheritance through it
    expect_result("function B() {} B.tag = 'base'; function D() {} Object.setPrototypeOf(D, B); "
                  "return D.tag;",
                  "base");
    // two levels
    expect_result("function A() {} A.tag = 'a'; function B() {} function C() {} "
                  "Object.setPrototypeOf(B, A); Object.setPrototypeOf(C, B); return C.tag;",
                  "a");
    // an ordinary object still answers with its own prototype
    expect_result("const base = {}; const o = Object.create(base); "
                  "return Object.getPrototypeOf(o) === base;",
                  "true");
}

// A DESTRUCTURED NAME IS A LOCAL, NOT A TEMPORARY.
//
// `const { data } = f()` allocated `data`'s register INSIDE the reg_mark that
// exists to free the temporary holding f()'s result - so release_to handed the
// local's slot back and the next temporary in the same scope wrote over it.
//
// It only showed inside a BLOCK. In a function's top scope the name is hoisted,
// so nothing was allocated and every test of destructuring passed. In a try
// block, `const { data } = f(); return 'len=' + data.length` read `data` as the
// string "len=" - a plausible value, from a plausible-looking line, with nothing
// reporting anything.
//
// This is what stopped p5.js loading an image: `loadImage` destructures the
// result of its fetch inside a try, so its bytes were a fragment of its own
// error message and the decode failed with no explanation.
void test_destructuring_in_a_block() {
    expect_result(
        "function f() { return { data: 'abc' }; }\n"
        "try { const { data } = f(); return 'len=' + data.length; } catch (e) { return 'threw'; }",
        "len=3");
    // The two-name case, which happened to work and must keep working.
    expect_result("function f() { return { a: 'xy', b: 'z' }; }\n"
                  "try { const { a, b } = f(); return a + b + '/' + a.length; } catch (e) { return "
                  "'threw'; }",
                  "xyz/2");
    // `var` in a block takes the same path.
    expect_result(
        "function f() { return { data: [1, 2, 3] }; }\n"
        "try { var { data } = f(); return 'n=' + data.length; } catch (e) { return 'threw'; }",
        "n=3");
    // Nested blocks, and a temporary allocated after the pattern.
    expect_result(
        "function f() { return { data: 'abc' }; }\n"
        "{ { const { data } = f(); const t = 'a longer string'; return data + t.length; } }",
        "abc15");
    // Array patterns and defaults in a block, for the same reason.
    expect_result(
        "try { const [x, y] = ['ab', 'c']; return x + y + '/' + x.length; } catch (e) { return "
        "'threw'; }",
        "abc/2");
    expect_result(
        "function f() { return {}; }\n"
        "{ const { missing = 'fallback' } = f(); return missing + '/' + missing.length; }",
        "fallback/8");
    // Renamed and nested shapes.
    expect_result("function f() { return { outer: { inner: 'v' } }; }\n"
                  "{ const { outer: { inner } } = f(); return inner + '/' + inner.length; }",
                  "v/1");
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
    expect_result("function f() { function v() { return 'fn'; } "
                  "{ const v = 'shadow'; } return v(); } return f();",
                  "fn");
    // ...and the shadow is visible inside the block
    expect_result("function f() { function v() { return 'fn'; } "
                  "{ const v = 'shadow'; return v; } } return f();",
                  "shadow");
    // the same through a DESTRUCTURING, which is the shape p5 has
    expect_result("function f() { function boolean(x) { return 'fn:' + x; } "
                  "for (const { boolean } of [{ boolean: true }]) { } "
                  "return boolean(1); } return f();",
                  "fn:1");
    expect_result("function f() { function boolean() { return 'fn'; } let seen; "
                  "for (const { boolean } of [{ boolean: true }]) { seen = boolean; } "
                  "return seen + '|' + boolean(); } return f();",
                  "true|fn");
    // a captured binding survives a shadow in a sibling block
    expect_result("function f() { function v() { return 'fn'; } const get = () => v(); "
                  "{ const v = 1; } return get(); } return f();",
                  "fn");
    // and a var of the same name at function scope still REUSES its slot
    expect_result("function f() { var x = 1; var x = 2; return x; } return f();", "2");
}

// `f.toString()` RETURNS THE SOURCE. An engine with no answer here cannot run
// a library that reads its own code, and p5.js's error system parses the
// sketch it was handed. A closure knows which program its protos came from and
// the program keeps its text, so this is a substring rather than a
// reconstruction.
void test_function_to_string() {
    expect_result("function hi(x) { return x + 1; } return hi.toString();",
                  "function hi(x) { return x + 1; }");
    expect_result("const f = (a, b) => a + b; return f.toString();", "(a, b) => a + b");
    expect_result("const g = z => z * 2; return g.toString();", "z => z * 2");
    // an expression keeps its own name and body
    expect_result("const h = function named() { return 1; }; return h.toString();",
                  "function named() { return 1; }");
    // a method's span starts at its NAME, which is what a browser returns
    expect_result("class C { m(a) { return a; } } return C.prototype.m.toString();",
                  "m(a) { return a; }");
    expect_result("const o = { go(n) { return n; } }; return o.go.toString();",
                  "go(n) { return n; }");
    // a native says so rather than returning something a parser would accept
    expect_result("return Math.max.toString().indexOf('native code') >= 0;", "true");
}

void test_functions() {
    expect_result("function add(a, b) { return a + b; } return add(2, 3);", "5");
    expect_result("function f() { return 7; } return f();", "7");
    expect_result("function id(x) { return x; } return id('hi');", "hi");
    // hoisting: callable before its declaration appears
    expect_result("let r = twice(21); function twice(n) { return n * 2; } return r;", "42");
    // recursion, and therefore real frame handling
    expect_result(
        "function fact(n) { if (n <= 1) { return 1; } return n * fact(n - 1); } return fact(10);",
        "3628800");
    expect_result(
        "function fib(n) { if (n < 2) { return n; } return fib(n-1) + fib(n-2); } return fib(20);",
        "6765");
    // a missing argument is undefined, not an error
    expect_result("function f(a, b) { return typeof b; } return f(1);", "undefined");
    // function expressions and arrows
    expect_result("let f = function(x) { return x * 3; }; return f(4);", "12");
    expect_result("let f = (x) => x + 1; return f(41);", "42");
    expect_result("let f = x => x * x; return f(9);", "81");
}

// The most common shape in JavaScript: script-level state mutated by a
// function declared beside it. Script scope is global scope here, so this must
// simply work rather than hit the enclosing-local refusal.
void test_script_scope_is_shared_with_functions() {
    expect_result(
        "let count = 0; function inc() { count = count + 1; } inc(); inc(); return count;", "2");
    expect_result(
        "let name = 'ctbrowser'; function greet() { return 'hi ' + name; } return greet();",
        "hi ctbrowser");
    expect_result("let total = 0; function addAll(a) { for (let i = 0; i < a.length; i++) "
                  "{ total = total + a[i]; } } addAll([1,2,3,4]); return total;",
                  "10");
}

// Real closures, via boxed cells. Every case here is one that capture-by-value
// would get WRONG rather than merely slow, which is why the compiler used to
// refuse instead of guessing.
void test_closures() {
    // the counter: the canonical case. Capture-by-value returns 0 forever.
    expect_result(
        "function counter() { let n = 0; function inc() { n = n + 1; return n; } return inc; }"
        "let c = counter(); c(); c(); return c();",
        "3");

    // two closures over the SAME cell must see each other's writes
    expect_result("function pair() { let n = 0;"
                  "  return { bump: function() { n = n + 1; }, read: function() { return n; } }; }"
                  "let p = pair(); p.bump(); p.bump(); return p.read();",
                  "2");

    // separate invocations must NOT share a cell
    expect_result("function counter() { let n = 0; return function() { n = n + 1; return n; }; }"
                  "let a = counter(); let b = counter(); a(); a(); return a() + ',' + b();",
                  "3,1");

    // capture of a parameter, not just a local
    expect_result("function adder(x) { return function(y) { return x + y; }; }"
                  "let add5 = adder(5); return add5(37);",
                  "42");

    // TWO levels of nesting: the innermost function captures a variable from
    // its grandparent, which only works if the middle function re-exports it
    // as its own upvalue
    expect_result("function outer() { let v = 'deep';"
                  "  function middle() { function inner() { return v; } return inner(); }"
                  "  return middle(); }"
                  "return outer();",
                  "deep");

    // mutation from the innermost level, visible at the outermost
    expect_result("function outer() { let n = 1;"
                  "  function middle() { function inner() { n = n * 10; } inner(); }"
                  "  middle(); middle(); return n; }"
                  "return outer();",
                  "100");

    // a closure outliving the frame that created it - the cell must survive
    expect_result("function make() { let msg = 'alive'; return function() { return msg; }; }"
                  "let f = make(); return f();",
                  "alive");

    // closures in a loop capture the loop's binding
    expect_result("function build() { let fns = []; let i = 0;"
                  "  while (i < 3) { fns[i] = function() { return i; }; i = i + 1; }"
                  "  return fns[0](); }"
                  "return build();",
                  "3"); // `let i` outside the loop body is ONE binding, so all three see 3
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

// `new Function(body)` - A COMPILER AT RUN TIME.
//
// It existed and refused. Two things had to be true first, and both now are: a
// closure records which PROGRAM its nested functions live in
// (`closure_object::owner`), so a frame from one program can call into another;
// and the context OWNS the programs it compiles, so they outlive the closures
// holding pointers into them.
void test_new_function() {
    expect_result("return typeof new Function();", "function");
    expect_result("return new Function('return 41 + 1;')();", "42");
    expect_result("return new Function('a', 'return a * 2;')(21);", "42");
    expect_result("return new Function('a', 'b', 'return a + b;')(1, 2);", "3");
    // Every argument but the last names a parameter, and one argument may name
    // several - `new Function('a,b', ...)` is the form a minifier emits.
    expect_result("return new Function('a,b', 'return a - b;')(5, 2);", "3");
    // Called without `new`, which is the same thing.
    expect_result("return Function('return 3;')();", "3");
    // The body sees the globals, which is the only scope it has - a function
    // built from text closes over nothing else.
    // A top-level `var` IS a global here by design (see above), which is the
    // only scope a function built from text can see.
    expect_result("var g = 7; return new Function('return g;')();", "7");
    expect_result("var f = function (x) { return x + 1; };"
                  "return new Function('return f(1);')();",
                  "2");
    // ...and it is a real compile, so a function inside the body works.
    expect_result("return new Function('var f = function () { return 5; }; return f();')();", "5");
    expect_result("return new Function('a', 'b', 'return 1;').length;", "2");
    expect_result("return new Function('return 1;').name;", "anonymous");
    // A bad body is a SyntaxError a page can CATCH, because building a
    // function from user-supplied text is exactly where one is expected.
    expect_result("try { new Function('this is not javascript ((('); } catch (e) { return e.name; }"
                  "return 'not thrown';",
                  "SyntaxError");
    // It runs NESTED, so it must not drain the microtask queue: that belongs to
    // the turn, not to the program that happened to build a function.
    expect_after_turn(
        "var result = ''; Promise.resolve(1).then(function () { result += 'then;'; });"
        "const f = new Function('return 1;');"
        "result += 'built(' + f() + ');';",
        "built(1);then;");
}

// A DESTRUCTURED PARAMETER THAT A NESTED FUNCTION CAPTURES.
//
// Two things box a local, and both were running: the pattern binding boxes the
// names it declares as it binds them, and the parameter loop boxed everything
// the frame had declared - which by then included those names. The result was a
// cell inside a cell, so reading the variable gave the inner CELL rather than
// the value: an object with no properties, and no error anywhere.
//
// It only showed when the name was CAPTURED, because an uncaptured local is
// never boxed at all - which is why every simpler test of destructured
// parameters passed. colorjs's `toGamutCSS(origin, { space } = {})` is exactly
// this shape, and got a cell where it wanted a colour space.
void test_captured_destructured_parameters() {
    expect_result("const o = { id: 'x' };"
                  "function f(n, { space } = {}) { const read = () => space; return read().id; }"
                  "return f(1, { space: o });",
                  "x");
    // The inner and outer reads must be the SAME binding.
    expect_result("const o = { id: 'y' };"
                  "function f(n, { space } = {}) { const read = function () { return space; };"
                  "  return space === read() ? 'same' : 'different'; }"
                  "return f(1, { space: o });",
                  "same");
    // ...and a write through the closure reaches it.
    expect_result("function f({ v } = {}) { const set = () => { v = v + 1; }; set(); return v; }"
                  "return f({ v: 1 });",
                  "2");
    expect_result("function h([a] = []) { const r = () => a; return r(); } return h([5]);", "5");
    // A default still applies when the argument is absent.
    expect_result(
        "function f(n, { space = 'fallback' } = {}) { const r = () => space; return r(); }"
        "return f(1);",
        "fallback");
}

// A NAME USED ONLY INSIDE A TEMPLATE HOLE IS STILL CAPTURED.
//
// A template literal is ONE node carrying its whole source, substitutions
// included - the parser does not break them out into children - so every walk
// over the tree was blind to them. The two that matter decide whether a local
// is BOXED and whether `arguments` is materialised, and a name that appeared
// only in a hole was invisible to both: the enclosing frame never boxed it, the
// nested function resolved it as a global, and it read undefined. p5.js builds
// its CDN url that way, with the version number in a hole.
void test_template_holes_capture() {
    expect_result("const V = '1.2.3'; const f = function () { return `v=${V}`; }; return f();",
                  "v=1.2.3");
    expect_result("const n = 7; const f = () => `n=${n * 2}`; return f();", "n=14");
    // ...and the capture is a real one, so a mutation through the hole reaches
    // the enclosing local rather than a copy.
    expect_result("let c = 0; const bump = function () { return `c=${++c}`; };"
                  "bump(); bump(); return c;",
                  "2");
    // Nested braces inside a hole must not end it early.
    expect_result("const o = { a: 1 }; const f = function () { return `x=${ {v: o.a}.v }`; };"
                  "return f();",
                  "x=1");
    // `arguments` only inside a hole still materialises it.
    expect_result(
        "function f() { const g = function () { return 1; }; return `n=${arguments.length}`; }"
        "return f(1, 2, 3);",
        "n=3");
}

// `obj[key](...)` PASSES ITS ARGUMENTS, not the key.
//
// call_computed reads its arguments from base+1 upwards, and the compiler was
// evaluating the key into that same register - so every computed call with
// arguments arrived shifted by one, with the KEY as argument 0 and the last
// argument dropped. Silent, and only in the form with arguments, which is why
// `xs[0]()` looked fine.
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

// A NAMED FUNCTION EXPRESSION BINDS ITS OWN NAME.
//
// `function me() {}` as an EXPRESSION puts `me` in scope inside its own body
// and nowhere else. It is the only way an otherwise-anonymous function can
// reach itself, and every self-driving callback is written that way:
// `(function pump() { requestAnimationFrame(pump); })()` never ran a second
// time here, silently, because a callback that is undefined is not an error at
// the point it is registered.
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

} // namespace

int main() {
    test_default_parameters();
    test_rest_parameters();
    test_nested_function_declarations_are_local();
    test_arrow_this_is_lexical();
    test_spread_in_a_call();
    test_destructuring_declarations();
    test_destructuring_parameters();
    test_destructuring_assignment();
    test_destructuring_in_for_of();
    test_functions_have_a_prototype();
    test_function_prototype();
    test_function_prototype_link();
    test_destructuring_in_a_block();
    test_a_declaration_shadows();
    test_function_to_string();
    test_functions();
    test_script_scope_is_shared_with_functions();
    test_closures();
    test_this();
    test_new_function();
    test_captured_destructured_parameters();
    test_template_holes_capture();
    test_computed_calls_pass_their_arguments();
    test_arguments();
    test_named_function_expressions();
    REPORT("vm_functions");
}
