//===- ctbrowser/unittests/js/vm/functions.cpp - VM function tests --------===//
//
// Functions: parameters, `this`, `arguments`, closures and the scopes they
// capture, and the function object itself. expect.hpp holds the shared
// assertion helpers; functions/constructors.cpp holds the class and
// constructor cases. Section numbers are ECMA-262's.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include "expect.hpp"
#include "functions/constructors.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

using namespace ctbrowser::script;

namespace {

// Default and rest parameters: an omitted argument takes its default, and
// `...rest` binds an array of the remaining arguments. In the parse tree a
// default is the parameter node's `a` child and a rest has `d == 1`.
void test_default_parameters() {
    expect_result("function f(a, b = 2) { return a + b; } return f(1);", "3");
    expect_result("function f(a, b = 2) { return a + b; } return f(1, 10);", "11");
    // undefined takes the default; null does not.
    expect_result("function f(a = 5) { return a; } return f(undefined);", "5");
    expect_result("function f(a = 5) { return a; } return f(null);", "null");
    // A default may be an expression, and may see earlier parameters...
    expect_result("function f(a, b = a * 2) { return b; } return f(4);", "8");
    // ...including a captured one. `a` is boxed because the arrow closes over
    // it, so the box must exist before the default reads it; both the read
    // and the default's own write go through the cell. (zod's
    // `_instanceof(cls, params = {error: cls.name})`, used by p5's parameter
    // validator, has this shape.)
    expect_result("function f(a, b = a * 2) { return () => a + b; } return f(4)();", "12");
    expect_result("function f(a, b = a * 2) { const g = () => b; return g(); } return f(3);", "6");
    expect_result("function f(a, b = a) { const g = () => b; b = 7; return g(); } return f(3);",
                  "7");
    expect_result("function f(...r) { const g = () => r.length; return g(); } return f(1, 2, 3);",
                  "3");
    // It is only evaluated when it is needed.
    expect_result("let hit = 0; function d() { hit = 1; return 1; } "
                  "function f(a = d()) { return a; } f(9); return hit;",
                  "0");
    // Arrow functions take the same path.
    expect_result("const f = (a, b = 3) => a + b; return f(1);", "4");
}

void test_rest_parameters() {
    expect_result("function f(...rest) { return rest.length; } return f(1, 2, 3);", "3");
    expect_result("function f(...rest) { return rest[1]; } return f('a', 'b');", "b");
    expect_result("function f(a, ...rest) { return rest.join('-'); } return f(1, 2, 3, 4);",
                  "2-3-4");
    // No extra arguments is an empty array, not undefined.
    expect_result("function f(a, ...rest) { return rest.length; } return f(1);", "0");
    expect_result(
        "function f(...rest) { return Array.isArray(rest) && rest.length === 0; } return f();",
        "true");
    // The rest array is a real array and the locals after it are undisturbed.
    expect_result(
        "function f(a, ...rest) { let x = 7; return a + rest.length + x; } return f(1, 2, 3);",
        "10");
    // A body long enough to reuse the registers the arguments arrived in.
    expect_result("function f(a, ...rest) { let p = 1, q = 2, r = 3, s = 4, t = 5; "
                  "return rest.join(',') + '|' + (p + q + r + s + t); } return f(0, 8, 9);",
                  "8,9|15");
}

// A nested function declaration is a binding in its scope, not a global.
// Otherwise two helpers with the same name in two different closures collide,
// and a nested function meant to capture an enclosing local reads a global
// instead, undoing the point of an IIFE.
void test_nested_function_declarations_are_local() {
    // The inner name does not escape.
    expect_result("function outer() { function helper() { return 1; } return helper(); } "
                  "outer(); return typeof helper;",
                  "undefined");
    // Two scopes, same name, no collision.
    expect_result("function a() { function h() { return 'a'; } return h(); } "
                  "function b() { function h() { return 'b'; } return h(); } "
                  "return a() + b();",
                  "ab");
    // It still captures the enclosing local rather than a global.
    expect_result(
        "var n = 'global'; "
        "function outer() { let n = 'local'; function read() { return n; } return read(); } "
        "return outer();",
        "local");
    // Recursion resolves to itself, not to a global of the same name.
    expect_result("function outer() { function fact(k) { return k <= 1 ? 1 : k * fact(k - 1); } "
                  "return fact(5); } return outer();",
                  "120");
    // A top-level declaration is still a global: pages define functions the
    // host calls by name.
    expect_result("function top() { return 7; } return typeof top;", "function");
}

// An arrow sees the `this` of the scope where it was written; it has no
// receiver of its own, so `this` in an arrow inside a method is the method's.
void test_arrow_this_is_lexical() {
    expect_result(
        "const o = { n: 5, get() { const f = () => this.n; return f(); } }; return o.get();", "5");
    // Through two levels of arrow.
    expect_result("const o = { n: 6, get() { const f = () => () => this.n; return f()(); } }; "
                  "return o.get();",
                  "6");
    // An arrow inside a callback still sees the method's object.
    expect_result("const o = { n: 3, sum() { return [1, 2].map(x => x * this.n).join(','); } }; "
                  "return o.sum();",
                  "3,6");
    // An ordinary function still gets its own receiver.
    expect_result("const o = { n: 1, get() { return this.n; } }; return o.get();", "1");
    // A class method's arrow sees the instance.
    expect_result("class C { constructor() { this.v = 9; } read() { const f = () => this.v; "
                  "return f(); } } return new C().read();",
                  "9");
}

// Spread arguments: `f(...args)`, method and computed calls, natives and `new`.
void test_spread_in_a_call() {
    expect_result("function f(a, b, c) { return a + b + c; } return f(...[1, 2, 3]);", "6");
    // Mixed with ordinary arguments, in any position.
    expect_result("function f(a, b, c) { return a + '-' + b + '-' + c; } "
                  "return f(1, ...[2, 3]);",
                  "1-2-3");
    expect_result("function f(a, b, c) { return a + '-' + b + '-' + c; } "
                  "return f(...[1, 2], 3);",
                  "1-2-3");
    expect_result("function f(a, b, c, d) { return a + b + c + d; } "
                  "return f(...[1, 2], ...[3, 4]);",
                  "10");
    // A method call keeps its receiver...
    expect_result(
        "const o = { n: 10, add(a, b) { return this.n + a + b; } }; return o.add(...[1, 2]);",
        "13");
    // ...and so does a computed one.
    expect_result("const o = { n: 10, add(a, b) { return this.n + a + b; } }; "
                  "const k = 'add'; return o[k](...[1, 2]);",
                  "13");
    // It reaches natives too.
    expect_result("return Math.max(...[3, 9, 4]);", "9");
    // Spread of a computed array, not just a literal...
    expect_result("function f(a, b) { return a * b; } const xs = [3, 4]; return f(...xs);", "12");
    // ...and it composes with a rest parameter on the other side.
    expect_result("function f(...rest) { return rest.length; } return f(...[1, 2, 3], 4);", "4");
    expect_result("function f(a, ...rest) { return rest.join(','); } return f(...[1, 2, 3]);",
                  "2,3");
    // `new C(...args)`.
    expect_result("class P { constructor(a, b) { this.v = a + b; } } return new P(...[2, 3]).v;",
                  "5");
    expect_result("class P { constructor(a, b, c) { this.v = a + b + c; } } "
                  "return new P(1, ...[2, 3]).v;",
                  "6");
    // A spread of an empty array passes nothing.
    expect_result("function f(a) { return typeof a; } return f(...[]);", "undefined");
}

// Destructuring declarations: a binding position may hold an object or array
// pattern instead of a name.
void test_destructuring_declarations() {
    expect_result("const {a, b} = {a: 1, b: 2}; return a + b;", "3");
    expect_result("const [x, y] = [3, 4]; return x * y;", "12");
    expect_result("const {a: renamed} = {a: 7}; return renamed;", "7");
    // Defaults, and only for undefined.
    expect_result("const {a = 5} = {}; return a;", "5");
    expect_result("const {a = 5} = {a: 0}; return a;", "0");
    expect_result("const [p = 1, q = 2] = [9]; return p + ',' + q;", "9,2");
    // Holes.
    expect_result("const [, second] = ['a', 'b']; return second;", "b");
    // Rest, in both shapes.
    expect_result("const [head, ...tail] = [1, 2, 3]; return head + '|' + tail.join(',');",
                  "1|2,3");
    expect_result("const {a, ...rest} = {a: 1, b: 2, c: 3}; "
                  "return a + '|' + Object.keys(rest).join(',');",
                  "1|b,c");
    // A computed key is excluded from the rest too (14.3.3.3 excludedNames).
    expect_result("const k = 'b'; const {[k]: v, ...rest} = {a: 1, b: 2, c: 3}; "
                  "return v + '|' + Object.keys(rest).join(',');",
                  "2|a,c");
    expect_result("let b, rest; ({['fo' + 'o']: b, ...rest} = {foo: 1, bar: 2}); "
                  "return b + '|' + Object.keys(rest).join(',');",
                  "1|bar");
    // Nested.
    expect_result("const {a: {b}} = {a: {b: 'deep'}}; return b;", "deep");
    expect_result("const [[m], [n]] = [[1], [2]]; return m + n;", "3");
    expect_result("const {list: [first]} = {list: ['x']}; return first;", "x");
    // A computed key.
    expect_result("const k = 'dyn'; const {[k]: got} = {dyn: 42}; return got;", "42");
    // A missing property is undefined, not an error.
    expect_result("const {nope} = {}; return typeof nope;", "undefined");
    // Inside a function, so the binding is a local rather than a global.
    expect_result("function f(o) { const {a, b} = o; return a - b; } return f({a: 9, b: 4});", "5");
    // A nested function can capture a name a pattern bound.
    expect_result("function f(o) { const {v} = o; const get = () => v; return get(); } "
                  "return f({v: 'captured'});",
                  "captured");
}

void test_destructuring_parameters() {
    expect_result("function f({x, y}) { return x + y; } return f({x: 1, y: 2});", "3");
    expect_result("function f([a, b]) { return a * b; } return f([3, 4]);", "12");
    expect_result("function f({x = 10}) { return x; } return f({});", "10");
    expect_result("const f = ({n}) => n * 2; return f({n: 21});", "42");
    // Mixed with ordinary parameters and with a rest.
    expect_result("function f(a, {b}, ...rest) { return a + b + rest.length; } "
                  "return f(1, {b: 2}, 9, 9);",
                  "5");
    // A whole-parameter default alongside a pattern.
    expect_result("function f({x} = {x: 'fallback'}) { return x; } return f();", "fallback");
    // A generator evaluates its parameters when called (10.2.1 step 8), not
    // on the first `.next()`: the default runs at g(), a throw is g()'s, and
    // the body itself has not started, so `.return()` before a `.next()` still
    // finishes it without running anything.
    expect_result("var ran = 0; function* g(a = (ran++, 1)) { yield a; }"
                  "var it = g(); var before = ran; var v = it.next().value;"
                  "return before + ',' + ran + ',' + v;",
                  "1,1,1");
    expect_result("function* g({x}) { yield x; }"
                  "try { g(null); return 'no throw'; } catch (e) { return e.constructor.name; }",
                  "TypeError");
    expect_result("var body = 0; function* g(a = 1) { body++; yield a; }"
                  "var it = g(); var r = it.return(7); return body + ',' + r.value + ',' + r.done;",
                  "0,7,true");
    expect_result("function* g(a = 1) { var got = yield a; return got; }"
                  "var it = g(); it.next('ignored'); return it.next('sent').value;",
                  "sent");
    expect_result(
        "async function* ag({x}) { yield x; }"
        "try { ag(undefined); return 'no throw'; } catch (e) { return e.constructor.name; }",
        "TypeError");
}

void test_destructuring_assignment() {
    expect_result("let a, b; [a, b] = [1, 2]; return a + b;", "3");
    expect_result("let a = 1, b = 2; [a, b] = [b, a]; return a + ',' + b;", "2,1");
    expect_result("let x; ({x} = {x: 'set'}); return x;", "set");
    expect_result("let x, y; ({x, y: y} = {x: 1, y: 2}); return x + y;", "3");
    // A hole skips a position.
    expect_result("let second; [, second] = ['a', 'b']; return second;", "b");
    // A member expression is a legal target.
    expect_result("const o = {}; [o.first] = ['here']; return o.first;", "here");
}

void test_destructuring_in_for_of() {
    expect_result(
        "let sum = 0; for (const [a, b] of [[1, 2], [3, 4]]) { sum += a * b; } return sum;", "14");
    expect_result(
        "let names = ''; for (const {n} of [{n: 'a'}, {n: 'b'}]) { names += n; } return names;",
        "ab");
    // for-in with no declaration keyword, assigning to an existing binding.
    expect_result("let k, seen = ''; for (k in {a: 1, b: 2}) { seen += k; } return seen;", "ab");
}

// Every ordinary function has a `prototype` object, made on first use, so
// `function F() {}; new F() instanceof F` holds. This is the
// constructor-function pattern every transpiler emits (Babel's
// `_classCallCheck` guard is exactly that test).
void test_functions_have_a_prototype() {
    expect_result("function F() {} return typeof F.prototype;", "object");
    expect_result("function F() {} return F.prototype.constructor === F;", "true");
    expect_result("function F() {} return new F() instanceof F;", "true");
    // The same object every time, or nothing built on it would hold.
    expect_result("function F() {} return F.prototype === F.prototype;", "true");
    // Methods installed on it are found by an instance.
    expect_result("function F() {} F.prototype.m = function () { return 4; }; return new F().m();",
                  "4");
    // An arrow is not a constructor and gets none.
    expect_result("const a = () => 1; return typeof a.prototype;", "undefined");
}

void test_function_prototype() {
    expect_result("function f(a) { return this.v + a; } return f.call({ v: 1 }, 2);", "3");
    expect_result("function f(a) { return this.v + a; } return f.apply({ v: 1 }, [2]);", "3");
    expect_result("function f(a) { return this.v + a; } return f.bind({ v: 1 })(2);", "3");
    // bind is a partial application, not just a receiver change.
    expect_result("function f(a, b) { return this.v + a + b; } return f.bind({ v: 1 }, 10)(2);",
                  "13");
    // The constructor-borrowing pattern every transpiler emits.
    expect_result("function P() { this.v = 1; } function C() { P.call(this); } return new C().v;",
                  "1");
    // A native has the same prototype.
    expect_result("return typeof Math.max.apply;", "function");
}

void test_apply_reads_argument_properties() {
    expect_result(R"(
        var args = [0, 1];
        Object.defineProperty(args, '0', {get: function () { args[1] = 8; return 42; }});
        return (function (a, b) { return a + ',' + b; }).apply(null, args);
    )",
                  "42,8");
    expect_result(R"(
        var args = [, 2];
        Object.setPrototypeOf(args, {0: 9});
        return (function (a, b) { return a + ',' + b; }).apply(null, args);
    )",
                  "9,2");
    for (const std::string_view invoke :
         {"target.apply(null, args)", "Reflect.apply(target, null, args)",
          "Reflect.construct(target, args)"}) {
        const std::string call{invoke};
        expect_result(R"(
            var log = '';
            function target() { log += 'called'; }
            var args = {length: 2, get 0() { log += '0'; throw 42; },
                        get 1() { log += '1'; return 0; }};
            try { )" + call +
                          R"(; } catch (e) { log += ':' + e; }
            return log;
        )",
                      "0:42");
        expect_result(R"(
            var log = '';
            function target() { log += 'called'; }
            var args = {get length() { throw 42; }, get 0() { log += '0'; }};
            try { )" + call +
                          R"(; } catch (e) { log += e; }
            return log;
        )",
                      "42");
        expect_result(R"(
            function target(a, b) { return {result: a + ',' + b}; }
            var args = {length: 2, get 0() { try { throw 0; } catch (e) {} return 7; }, 1: 8};
            return ()" + call +
                          R"().result;
        )",
                      "7,8");
    }
}

// A function has a [[Prototype]] of its own, distinct from its `prototype`
// property. Babel's `_inherits` sets both: the subclass's prototype property
// so instances find inherited methods, and the subclass function's
// [[Prototype]] so statics are inherited.
void test_function_prototype_link() {
    expect_result("function B() {} function D() {} Object.setPrototypeOf(D, B); "
                  "return Object.getPrototypeOf(D) === B;",
                  "true");
    // Static inheritance through it.
    expect_result("function B() {} B.tag = 'base'; function D() {} Object.setPrototypeOf(D, B); "
                  "return D.tag;",
                  "base");
    // Two levels.
    expect_result("function A() {} A.tag = 'a'; function B() {} function C() {} "
                  "Object.setPrototypeOf(B, A); Object.setPrototypeOf(C, B); return C.tag;",
                  "a");
    // An ordinary object still answers with its own prototype.
    expect_result("const base = {}; const o = Object.create(base); "
                  "return Object.getPrototypeOf(o) === base;",
                  "true");
}

// A destructured name is a local, not a temporary: its register must be
// allocated outside the reg_mark that frees f()'s result, or the next
// temporary in the block overwrites it. Only visible inside a block, since
// a function's top-level names are hoisted. (p5.js's `loadImage` destructures
// its fetch result inside a try.)
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

// The TypeError for calling a non-function is built without running page
// code: describing the callee through an inherited Function.prototype.toString
// could throw a second TypeError under the first and escape the page's own
// catch.
void test_call_type_error_is_catchable() {
    expect_result("function P() {} function F() {} F.prototype = P; const o = new F(); "
                  "try { o(); return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
    expect_result("try { new Function.prototype(); return 'no'; } catch (e) { return e.name; }",
                  "TypeError");
    expect_result("try { (5)(); } catch (e) { return e.message.includes('(5)'); }", "true");
}

// A declaration shadows; it does not write through. A `const` in a block gets
// its own slot even when the frame has a hoisted name of the same spelling,
// rather than reusing the outer slot and assigning through to it. (p5.js has a
// top-level `function boolean(...)` and, in a block,
// `for (const { arity, boolean } of OperatorTable)`.)
void test_a_declaration_shadows() {
    // A block-scoped const over a hoisted function of the same name...
    expect_result("function f() { function v() { return 'fn'; } "
                  "{ const v = 'shadow'; } return v(); } return f();",
                  "fn");
    // ...and the shadow is visible inside the block
    expect_result("function f() { function v() { return 'fn'; } "
                  "{ const v = 'shadow'; return v; } } return f();",
                  "shadow");
    // The same through a destructuring, which is the shape p5 has.
    expect_result("function f() { function boolean(x) { return 'fn:' + x; } "
                  "for (const { boolean } of [{ boolean: true }]) { } "
                  "return boolean(1); } return f();",
                  "fn:1");
    expect_result("function f() { function boolean() { return 'fn'; } let seen; "
                  "for (const { boolean } of [{ boolean: true }]) { seen = boolean; } "
                  "return seen + '|' + boolean(); } return f();",
                  "true|fn");
    // A captured binding survives a shadow in a sibling block.
    expect_result("function f() { function v() { return 'fn'; } const get = () => v(); "
                  "{ const v = 1; } return get(); } return f();",
                  "fn");
    // A var of the same name at function scope still reuses its slot.
    expect_result("function f() { var x = 1; var x = 2; return x; } return f();", "2");
}

// `f.toString()` returns the source. Libraries read their own code, and
// p5.js's error system parses the sketch it was handed. A closure knows which
// program its protos came from and the program keeps its text, so this is a
// substring rather than a reconstruction.
void test_function_to_string() {
    expect_result("function hi(x) { return x + 1; } return hi.toString();",
                  "function hi(x) { return x + 1; }");
    expect_result("const f = (a, b) => a + b; return f.toString();", "(a, b) => a + b");
    expect_result("const g = z => z * 2; return g.toString();", "z => z * 2");
    // An expression keeps its own name and body.
    expect_result("const h = function named() { return 1; }; return h.toString();",
                  "function named() { return 1; }");
    // A method's span starts at its name, which is what a browser returns...
    expect_result("class C { m(a) { return a; } } return C.prototype.m.toString();",
                  "m(a) { return a; }");
    expect_result("const o = { go(n) { return n; } }; return o.go.toString();",
                  "go(n) { return n; }");
    // ...after its `static` (not part of the MethodDefinition, 15.7.1) and
    // with its `async` / `get` (which are).
    expect_result("class C { static /* s */ m() {} static get g() { return 1; }"
                  " static async * ag() {} } return [C.m, Object.getOwnPropertyDescriptor(C, 'g')"
                  ".get, C.ag].map(String).join('|');",
                  "m() {}|get g() { return 1; }|async * ag() {}");
    expect_result("const f = async function /* a */ F(x) {}; async function g() {}"
                  " const h = async /* b */ (a) => a; const k = async x => x;"
                  " return [f, g, h, k].map(String).join('|');",
                  "async function /* a */ F(x) {}|async function g() {}|async /* b */ (a) => a|"
                  "async x => x");
    // A symbol-named native shows no IdentifierName (the syntax makes it
    // optional) rather than its internal spelling.
    expect_result("return String([][Symbol.iterator]).indexOf('@@') < 0;", "true");
    // A class is its whole text (20.2.3.5), explicit constructor or not. A
    // synthesised constructor must not report the offsets of the
    // "(function () {})" it was compiled from.
    expect_result("class C { m(a) { return a; } } return String(C);",
                  "class C { m(a) { return a; } }");
    expect_result("const D = class E { constructor(x) { this.x = x; } }; return String(D);",
                  "class E { constructor(x) { this.x = x; } }");
    expect_result("class F {} class G extends F {} return String(G);", "class G extends F {}");
    // A native says so rather than returning something a parser would accept.
    expect_result("return Math.max.toString().indexOf('native code') >= 0;", "true");
}

void test_functions() {
    expect_result("function add(a, b) { return a + b; } return add(2, 3);", "5");
    expect_result("function f() { return 7; } return f();", "7");
    expect_result("function id(x) { return x; } return id('hi');", "hi");
    // Hoisting: callable before its declaration appears.
    expect_result("let r = twice(21); function twice(n) { return n * 2; } return r;", "42");
    // Recursion, and therefore real frame handling.
    expect_result(
        "function fact(n) { if (n <= 1) { return 1; } return n * fact(n - 1); } return fact(10);",
        "3628800");
    expect_result(
        "function fib(n) { if (n < 2) { return n; } return fib(n-1) + fib(n-2); } return fib(20);",
        "6765");
    // A missing argument is undefined, not an error.
    expect_result("function f(a, b) { return typeof b; } return f(1);", "undefined");
    // Function expressions and arrows.
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
// would get wrong, not merely slow.
void test_closures() {
    // The counter, the canonical case. Capture-by-value returns 0 forever.
    expect_result(
        "function counter() { let n = 0; function inc() { n = n + 1; return n; } return inc; }"
        "let c = counter(); c(); c(); return c();",
        "3");

    // Two closures over the same cell must see each other's writes.
    expect_result("function pair() { let n = 0;"
                  "  return { bump: function() { n = n + 1; }, read: function() { return n; } }; }"
                  "let p = pair(); p.bump(); p.bump(); return p.read();",
                  "2");

    // Separate invocations must not share a cell.
    expect_result("function counter() { let n = 0; return function() { n = n + 1; return n; }; }"
                  "let a = counter(); let b = counter(); a(); a(); return a() + ',' + b();",
                  "3,1");

    // Capture of a parameter, not just a local.
    expect_result("function adder(x) { return function(y) { return x + y; }; }"
                  "let add5 = adder(5); return add5(37);",
                  "42");

    // Two levels of nesting: the innermost function captures a variable from
    // its grandparent, which only works if the middle function re-exports it
    // as its own upvalue.
    expect_result("function outer() { let v = 'deep';"
                  "  function middle() { function inner() { return v; } return inner(); }"
                  "  return middle(); }"
                  "return outer();",
                  "deep");

    // Mutation from the innermost level, visible at the outermost.
    expect_result("function outer() { let n = 1;"
                  "  function middle() { function inner() { n = n * 10; } inner(); }"
                  "  middle(); middle(); return n; }"
                  "return outer();",
                  "100");

    // A closure outliving the frame that created it: the cell must survive.
    expect_result("function make() { let msg = 'alive'; return function() { return msg; }; }"
                  "let f = make(); return f();",
                  "alive");

    // Closures in a loop capture the loop's binding.
    expect_result("function build() { let fns = []; let i = 0;"
                  "  while (i < 3) { fns[i] = function() { return i; }; i = i + 1; }"
                  "  return fns[0](); }"
                  "return build();",
                  "3"); // `let i` outside the body is one binding: all see 3.

    // A `let`/`const` declared in the body is a fresh binding per iteration
    // (ECMA-262 evaluates the block's declarations in a new declarative
    // environment each time), so a closure from iteration 0 keeps iteration
    // 0's value. These run at the top level of a script on purpose: a block's
    // lexical declaration there must not become a global. Every loop form,
    // and a nested block.
    expect_result("var out = []; for (const id of ['a', 'b']) { const el = id + '!';"
                  "  out.push(function (e) { return e === el; }); }"
                  "return out[0]('a!') + ',' + out[1]('b!') + ',' + out[1]('a!');",
                  "true,true,false");
    expect_result("var out = []; for (let i = 0; i < 2; i++) { let v = i * 10;"
                  "  out.push(function () { return v; }); }"
                  "return out[0]() + ',' + out[1]();",
                  "0,10");
    expect_result("var out = []; var i = 0; while (i < 2) { const v = i; i++;"
                  "  out.push(function () { return v; }); }"
                  "return out[0]() + ',' + out[1]();",
                  "0,1");
    expect_result("var out = []; var i = 0; do { const v = i; i++;"
                  "  out.push(function () { return v; }); } while (i < 2);"
                  "return out[0]() + ',' + out[1]();",
                  "0,1");
    expect_result("var out = []; for (const k in {a: 1, b: 2}) { const v = k + k;"
                  "  out.push(function () { return v; }); }"
                  "return out[0]() + ',' + out[1]();",
                  "aa,bb");
    expect_result("var out = []; for (const id of ['a', 'b']) { if (true) { const el = id + '!';"
                  "  out.push(function () { return el; }); } }"
                  "return out[0]() + ',' + out[1]();",
                  "a!,b!");
    // The loop variable itself is per-iteration too.
    expect_result("var out = []; for (const id of ['a', 'b']) { out.push(function () {"
                  "  return id; }); } return out[0]() + ',' + out[1]();",
                  "a,b");
    // The same shape inside a function.
    expect_result("function f() { var out = []; for (const id of ['a', 'b']) {"
                  "  const el = id + '!'; out.push(function () { return el; }); }"
                  "  return out[0]() + ',' + out[1](); } return f();",
                  "a!,b!");
    // A top-level `var` in a block is still the script's own, shared binding.
    expect_result("var out = []; for (const id of ['a', 'b']) { var el = id + '!';"
                  "  out.push(function () { return el; }); }"
                  "return out[0]() + ',' + out[1]() + ',' + el;",
                  "b!,b!,b!");
}

void test_this() {
    // A method sees the object it was called on.
    expect_result("var o = {n: 7, get: function () { return this.n; }}; return o.get();", "7");
    // `this` follows the call site, not the definition.
    expect_result("var a = {n: 1, get: function () { return this.n; }};"
                  "var b = {n: 2, get: a.get}; return b.get();",
                  "2");
    // A sloppy function called with no receiver sees the global object
    // (10.2.1.2 step 5.a): a plain `f()`, a callback a native fires with
    // undefined, `f.call(null)`; strict code keeps the undefined it was given.
    expect_result("function f() { return this === globalThis; } return f();", "true");
    expect_result("function f() { return this === globalThis; } return [1].map(f)[0];", "true");
    expect_result("function f() { return this === globalThis; } return f.call(null);", "true");
    expect_result("function f() { 'use strict'; return this === undefined; } return f();", "true");
    expect_result("const f = () => this === globalThis; return f();", "true");
    expect_result("function f() { return () => this; } return f()() === globalThis;", "true");
    expect_result("function f() { return () => this; } return [0].map(f)[0]() === globalThis;",
                  "true");
    expect_result("function f() { 'use strict'; return () => this; }"
                  "return f()() === undefined;",
                  "true");
    expect_result("class C { f() { return this; } } const f = C.prototype.f;"
                  "return f() === undefined && f.call(5) === 5;",
                  "true");
    expect_result("function f() { return this; } const n = Reflect.apply(f, 5, []);"
                  "return [typeof n, n instanceof Number, n.valueOf()].join();",
                  "object,true,5");
    expect_result("Number.prototype.receiverKind = function() { return typeof this; };"
                  "return (5).receiverKind();",
                  "object");
    expect_result("function f() { return [this, () => this, this]; }"
                  "const r = Reflect.apply(f, 5, []);"
                  "return r[0] === r[1]() && r[0] === r[2];",
                  "true");
    expect_result("function f() { return this; } const bound = f.bind(5);"
                  "return bound() === bound();",
                  "false");
    expect_result("function f(read = () => this) { return read(); }"
                  "return f() === globalThis && Reflect.apply(f, 5, []) instanceof Number;",
                  "true");
    expect_result("function f() { return this; }"
                  "return [5, true, 's', 1n, Symbol('s')].every(v => {"
                  "const r = Reflect.apply(f, v, []); return typeof r === 'object' &&"
                  "r.valueOf() === v; });",
                  "true");
    expect_result("const f = (function() { 'use strict'; return () => this; }).call(5);"
                  "return f.call(null) === 5 && f.bind(null)() === 5;",
                  "true");
    expect_result("function f() { return this; } const object = new Number(5);"
                  "return Reflect.apply(f, object, []) === object && f.bind(object)() === object;",
                  "true");
    expect_result("function f() { return this; } const p = new Proxy(f, {"
                  "apply(target, receiver) { return receiver; }});"
                  "return [p() === undefined, p.call(null) === null, p.apply(5, []) === 5,"
                  "p.bind(5)() === 5, Reflect.apply(p, undefined, []) === undefined].join();",
                  "true,true,true,true,true");
    expect_result("function f() { return this; } const p = new Proxy(f, {});"
                  "return [typeof p.call(5), p.call(5) instanceof Number,"
                  "p.bind(5)() === p.bind(5)()].join();",
                  "object,true,false");
}

// `new Function(body)` compiles at run time. It works because a closure
// records which program its nested functions belong to
// (`closure_object::owner`), so a frame from one program can call into
// another, and the context owns every program it compiles, so they outlive
// the closures that point into them.
void test_new_function() {
    expect_result("return new Function('return this')() === globalThis;", "true");
    expect_result("return Function('return this')() === globalThis;", "true");
    expect_result("'use strict'; return new Function('return this')() === globalThis;", "true");
    expect_result("const f = Function('return this'); return [f.call(null) === globalThis,"
                  "f.call(undefined) === globalThis, typeof f.call(5),"
                  "f.call(5) instanceof Number, f.call(5).valueOf()].join();",
                  "true,true,object,true,5");
    expect_result("const f = Function('\"use strict\"; return this');"
                  "return [f() === undefined, f.call(null) === null,"
                  "f.call(undefined) === undefined, f.call(5) === 5].join();",
                  "true,true,true,true");
    expect_result("return Function('return () => this')()() === globalThis;", "true");
    expect_result("return Function('\"use strict\"; return () => this')()() === undefined;",
                  "true");
    expect_result("return typeof new Function();", "function");
    expect_result("return new Function('return 41 + 1;')();", "42");
    expect_result("return new Function('a', 'return a * 2;')(21);", "42");
    expect_result("return new Function('a', 'b', 'return a + b;')(1, 2);", "3");
    // Every argument but the last names a parameter, and one argument may name
    // several: `new Function('a,b', ...)` is the form a minifier emits.
    expect_result("return new Function('a,b', 'return a - b;')(5, 2);", "3");
    // Called without `new`, which is the same thing.
    expect_result("return Function('return 3;')();", "3");
    // The body sees the globals, which is the only scope it has: a function
    // built from text closes over nothing else. A top-level `var` is a global.
    expect_result("var g = 7; return new Function('return g;')();", "7");
    expect_result("var f = function (x) { return x + 1; };"
                  "return new Function('return f(1);')();",
                  "2");
    // ...and it is a real compile, so a function inside the body works.
    expect_result("return new Function('var f = function () { return 5; }; return f();')();", "5");
    expect_result("return new Function('a', 'b', 'return 1;').length;", "2");
    expect_result("return new Function('return 1;').name;", "anonymous");
    // A bad body is a SyntaxError a page can catch, because building a
    // function from user-supplied text is exactly where one is expected.
    expect_result("try { new Function('this is not javascript ((('); } catch (e) { return e.name; }"
                  "return 'not thrown';",
                  "SyntaxError");
    expect_result("let caught = 0; for (const body of ['<', '1 + <', 'for (<; ; ) {}', "
                  "'for (in xs) {}', '{*a(){}}']) { try { Function(body); } catch (e) { "
                  "if (e instanceof SyntaxError) caught++; } } return caught;",
                  "5");
    // It runs nested, so it must not drain the microtask queue: that belongs to
    // the turn, not to the program that happened to build a function.
    expect_after_turn(
        "var result = ''; Promise.resolve(1).then(function () { result += 'then;'; });"
        "const f = new Function('return 1;');"
        "result += 'built(' + f() + ');';",
        "built(1);then;");
}

// A destructured parameter that a nested function captures must be boxed
// exactly once. The pattern binding boxes the names it declares as it binds
// them, so the parameter loop must not box them again; a cell inside a cell
// would make reads return the inner cell instead of the value. Only captured
// names are boxed, so only this shape exercises it (colorjs's
// `toGamutCSS(origin, { space } = {})`).
void test_captured_destructured_parameters() {
    expect_result("const o = { id: 'x' };"
                  "function f(n, { space } = {}) { const read = () => space; return read().id; }"
                  "return f(1, { space: o });",
                  "x");
    // The inner and outer reads must be the same binding.
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

// A name used only inside a template hole is still captured.
//
// A template literal is one node carrying its whole source, substitutions
// included (the parser does not break them out into children), so the tree
// walks that decide whether a local is boxed and whether `arguments` is
// materialised must scan the holes explicitly. Otherwise the enclosing frame
// never boxes the name and the nested function reads it as an undefined
// global. (p5.js builds its CDN URL with the version number in a hole.)
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

// `obj[key](...)` passes its arguments, not the key.
//
// call_computed reads its arguments from base+1 upwards, so the compiler must
// not evaluate the key into that register; if it did, every computed call
// with arguments would arrive shifted by one, with the key as argument 0.
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

// `arguments`: every value a call actually received.
//
// It has to be materialised at entry. Built where the name is mentioned, it
// would read the frame's registers as they are at that moment, after the
// surrounding expression may have reused the ones holding arguments past the
// last declared parameter.
void test_arguments() {
    expect_result("function f(a, b, c) { return arguments.length; } return f(1);", "1");
    expect_result("function f(a, b, c) { return arguments.length; } return f(1, 2, 3, 4);", "4");
    expect_result("function g() { return arguments[0] + ',' + arguments[1]; } return g('x', 'y');",
                  "x,y");
    // Array-like enough for the commonest thing done with it.
    expect_result("function h() { return Array.prototype.slice.call(arguments).join('|'); }"
                  "return h(1, 2, 3);",
                  "1|2|3");
    // One object per call, not one per mention.
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
    // A default may mention it (10.2.11 steps 15-22), body or no body.
    expect_result("function d(x = arguments[2], y = arguments.length, z) { return x + ':' + y; }"
                  "return d(undefined, undefined, 'third');",
                  "third:3");
    // Not an array (10.4.4): on Object.prototype, "[object Arguments]", with
    // Array.prototype.values as its iterator and `callee` (the function when
    // mapped, the %ThrowTypeError% accessor when strict).
    expect_result("function f() { return Array.isArray(arguments) + ',' + "
                  "Object.prototype.toString.call(arguments) + ',' + typeof arguments.map + ',' +"
                  "(arguments instanceof Array) + ',' + (arguments instanceof Object) + ',' +"
                  "[...arguments].join('|') + ',' + (arguments.callee === f); } return f(1, 2);",
                  "false,[object Arguments],undefined,false,true,1|2,true");
    expect_result("function f() { 'use strict'; try { return arguments.callee; } catch (e) {"
                  " return e.name; } } return f();",
                  "TypeError");
    expect_result("function f() { 'use strict'; return arguments; }"
                  "const d = Object.getOwnPropertyDescriptor(f(), 'callee');"
                  "const c = Object.getOwnPropertyDescriptor(Function.prototype, 'caller');"
                  "return (d.get === d.set) + ',' + (d.get === c.get) + ',' + d.configurable + ','"
                  " + Object.isFrozen(d.get) + ',' + d.get.length + ',' + d.get.name;",
                  "true,true,false,true,0,");
}

// A closure made by its own initialiser, in a block: `let y = () => y` boxes
// `y`, and the cell has to exist before the arrow is made or the arrow holds
// the register's undefined and never the cell. At a function's top level the
// name is hoisted and boxed at entry, so only the block form exercises this.
void test_self_referencing_block_closure() {
    expect_result("function f() { { let y = () => y; return typeof y(); } } return f();",
                  "function");
    expect_result("function f() { let y = () => y; return typeof y(); } return f();", "function");
    expect_result(
        "function f() { if (true) { const g = function () { return g; }; return g() === g; } }"
        "return f();",
        "true");
    expect_result("{ let y = () => y; var out = typeof y(); } return out;", "function");
}

// A named function expression binds its own name.
//
// `function me() {}` as an expression puts `me` in scope inside its own body
// and nowhere else. It is the only way an otherwise-anonymous function can
// reach itself, and every self-driving callback is written that way:
// `(function pump() { requestAnimationFrame(pump); })()`. Registering an
// undefined callback is not an error, so getting this wrong fails silently.
void test_named_function_expressions() {
    expect_result("var f = function me(n) { return n <= 0 ? 'done' : me(n - 1); };"
                  "return f(3);",
                  "done");
    expect_result("var g = function named() { return typeof named; }; return g();", "function");
    // ...and nowhere else: the name is not a declaration in the enclosing scope.
    expect_result("var g = function named() { return 1; }; return typeof named;", "undefined");
    // A parameter of the same name shadows the binding, which is what makes it
    // safe to add: it can only ever be read where nothing else defined the name.
    expect_result("var f = function me(me) { return me; }; return f(7);", "7");
    // The binding survives capture, so a nested function sees it too.
    expect_result("var f = function me(n) { return (function () { return typeof me; })(); };"
                  "return f(1);",
                  "function");
    // A declaration is unaffected: its name is in the enclosing scope as well.
    expect_result("function d() { return typeof d; } return d() + ',' + typeof d;",
                  "function,function");
}

// NamedEvaluation (8.4.5): an anonymous function, arrow or class takes the
// name of what it initialises: a binding, a property, a default, a field.
// Stack traces read the same name.
void test_anonymous_functions_take_the_binding_name() {
    expect_result("var f = function () {}; return f.name;", "f");
    expect_result("let g = () => 1; return g.name;", "g");
    expect_result("var c = class {}; return c.name;", "c");
    expect_result("var h; h = function () {}; return h.name;", "h");
    expect_result("var o = { m: function () {}, a: () => 1, k: class {} };"
                  "return o.m.name + ',' + o.a.name + ',' + o.k.name;",
                  "m,a,k");
    expect_result("var [d = function () {}] = []; return d.name;", "d");
    expect_result("var { e = () => 0 } = {}; return e.name;", "e");
    expect_result("function f(p = function () {}) { return p.name; } return f();", "p");
    expect_result("class C { x = function () {}; static y = () => 1; }"
                  "return new C().x.name + ',' + C.y.name;",
                  "x,y");
    expect_result("var z; z ?\?= function () {}; return z.name;", "z");
    // A function with its own name keeps it; a parenthesised anonymous one
    // is still anonymous and still takes the binding's.
    expect_result("var f = function own() {}; var g = (function () {});"
                  "return f.name + ',' + g.name;",
                  "own,g");
}

// An array pattern uses the iterator protocol (8.6.2), not `src[i]`: it
// accepts anything with a Symbol.iterator, steps exactly as many times as it
// has elements, and calls `return()` when it stops before the iterator is
// done. A throw out of a default does not close the iterator (see
// compile_array_pattern), and a `next()` that throws marks the record done,
// so nothing is closed after it either.
void test_array_patterns_iterate() {
    const std::string counter =
        "var steps = 0, closed = 0;"
        "var it = { [Symbol.iterator]() { return { next() { steps++; return { value: steps,"
        " done: steps > 5 }; }, return() { closed++; return {}; } }; } };";
    expect_result(
        counter + "var [a, b] = it; return a + ',' + b + ' steps ' + steps + ' closed ' + closed;",
        "1,2 steps 2 closed 1");
    // Exhausted by the pattern: nothing left to close.
    expect_result(
        counter +
            "var [a, b, c, d, e, f] = it; return f + ' steps ' + steps + ' closed ' + closed;",
        "undefined steps 6 closed 0");
    expect_result(counter + "var [x, ...rest] = it; return rest.join('') + ' closed ' + closed;",
                  "2345 closed 0");
    expect_result(counter + "var [, , third] = it; return third + ' steps ' + steps;", "3 steps 3");
    // A throw from a default is the original throw. (The iterator is not
    // closed on that path; see compile_array_pattern for why.)
    expect_result(counter + "try { var [q = (function () { throw new Error('dflt'); })()] ="
                            " { [Symbol.iterator]() { return { next() { return { done: false }; },"
                            " return() { closed++; return {}; } }; } }; }"
                            " catch (e) { return e.message; }",
                  "dflt");
    // A throwing next() is not closed after.
    expect_result("var closed = 0; try { var [z] = { [Symbol.iterator]() { return { next() {"
                  " throw new Error('step'); }, return() { closed++; } }; } }; }"
                  " catch (e) { return e.message + ' closed ' + closed; }",
                  "step closed 0");
    // A generator is pulled lazily: two elements, two resumes.
    expect_result("var pulled = 0; function* g() { for (;;) { pulled++; yield pulled; } }"
                  "var [m, n] = g(); return m + n + ' pulled ' + pulled;",
                  "3 pulled 2");
    // Not iterable, and null or undefined as an object pattern: TypeError.
    expect_result("try { var [w] = 5; } catch (e) { return e.constructor.name; }", "TypeError");
    expect_result("try { var {} = null; } catch (e) { return e.constructor.name; }", "TypeError");
    expect_result("try { (function ({}) {})(undefined); } catch (e) { return e.constructor.name; }",
                  "TypeError");
    // Assignment patterns take the same walk.
    expect_result(counter + "var a, b; [a, b] = it; return a + b + ' closed ' + closed;",
                  "3 closed 1");
    // for-of and spread see a user iterable too.
    expect_result(counter + "var got = []; for (var v of it) got.push(v); return got.join('');",
                  "12345");
    expect_result(counter + "return [...it].length;", "5");
    expect_result(counter + "return Math.max(...it);", "5");
}

} // namespace

// 10.2.4: `caller` and `arguments` are %ThrowTypeError% accessors on
// Function.prototype: a strict function, an arrow, a class and a built-in
// throw; a sloppy function answers null, as every browser does.
void test_restricted_properties() {
    expect_result("function f() {} return f.caller + ',' + f.arguments;", "null,null");
    expect_result("'use strict'; function f() {} try { return f.caller; } catch (e) { return "
                  "e.constructor.name; }",
                  "TypeError");
    expect_result("try { return (() => 1).arguments; } catch (e) { return e.constructor.name; }",
                  "TypeError");
    expect_result("try { return Math.abs.caller; } catch (e) { return e.constructor.name; }",
                  "TypeError");
    expect_result("const d = Object.getOwnPropertyDescriptor(Function.prototype, 'caller');"
                  "return (d.get === d.set) + ',' + d.configurable + ',' + d.enumerable;",
                  "true,true,false");
}

int main() {
    test_derived_constructors();
    test_builtin_subclasses();
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
    test_apply_reads_argument_properties();
    test_function_prototype_link();
    test_destructuring_in_a_block();
    test_call_type_error_is_catchable();
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
    test_self_referencing_block_closure();
    test_named_function_expressions();
    test_anonymous_functions_take_the_binding_name();
    test_array_patterns_iterate();
    test_restricted_properties();
    test_call_bind_this();
    REPORT("vm_functions");
}
