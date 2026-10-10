//===- ctbrowser/unittests/js/direct_eval.cpp - Direct eval scoping -------===//
//
// A direct eval sees its caller's bindings (ECMA-262 19.2.1.1 PerformEval).
//
// `eval(src)` written in a function runs `src` in that function's scope: its
// locals, the enclosing functions' locals and `arguments` are read and written
// through the same cells the function uses (compiler_impl::compile_direct_eval,
// direct_eval_name). css/mediaqueries/test_media_queries.html depends on this:
// it evaluates a template of a loop variable that way 176 times.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

int main() {
    // Reads, at every depth, and the template shape the WPT file uses.
    js_expect("(function(){ var v = 7; return eval('v + 1'); })()", "8");
    js_expect("(function(){ var a = 1; return (function(){ var b = 2;"
              " return eval('a + b'); })(); })()",
              "3");
    js_expect("(function(){ var value = 3; return eval('`${value}px`'); })()", "3px");
    js_expect("(function(){ let x = 'block'; { let x = 'inner'; return eval('x'); } })()", "inner");
    // Writes land in the caller's binding.
    js_expect("(function(){ var n = 1; eval('n = n + 41'); return n; })()", "42");
    js_expect("(function(){ var n = 1; (function(){ eval('n = 5'); })(); return n; })()", "5");
    // A `var` of a name the caller binds is that binding; `var n;` alone keeps it.
    js_expect("(function(){ var n = 1; eval('var n = 9'); return n; })()", "9");
    js_expect("(function(){ var n = 1; eval('var n;'); return n; })()", "1");
    // ...and so is a function it declares, at its top or (B.3.2.3) in a block.
    js_expect("(function(){ var g = 1; eval('function g() { return 2; }'); return g(); })()", "2");
    js_expect("(function(f){ eval('{ function f() {} }'); return typeof f; })(1)", "function");
    // A sloppy eval's `var` may not reach through a `let`, `const` or `class`
    // of its caller's function (19.2.1.3 step 3) - a catch parameter is fine.
    js_expect("(function(){ let x; try { eval('var x;'); } catch (e) {"
              " return e.constructor.name; } return 'none'; })()",
              "SyntaxError");
    js_expect("(function(){ { class K {} try { eval('function K() {}'); } catch (e) {"
              " return e.constructor.name; } } return 'none'; })()",
              "SyntaxError");
    js_expect("(function(){ try { throw 1; } catch (c) { eval('var c = 2;'); return c; } })()",
              "2");
    js_expect("(function(){ let x; eval('\"use strict\"; var x = 1;'); return typeof x; })()",
              "undefined");
    // `this` and `arguments` are the caller's.
    js_expect("(function(){ return eval('this.tag'); }).call({tag: 'me'})", "me");
    js_expect("(function(){ return eval('arguments.length'); })(1, 2, 3)", "3");
    // The completion value, and an eval that is not %eval% is a plain call.
    js_expect("(function(){ var k = 2; return eval('if (k) { k * 10 } else { 0 }'); })()", "20");
    js_expect("(function(){ var eval = function (s) { return 'mine:' + s; };"
              " return eval('x'); })()",
              "mine:x");
    // A closure made inside the eval keeps the caller's cell.
    js_expect("(function(){ var c = 0; var inc = eval('(function(){ return ++c; })');"
              " inc(); inc(); return c; })()",
              "2");
    // A strict eval's `var` is its own (a new variable environment), whether
    // the caller or the source is strict.
    js_expect("(function(){ 'use strict'; var n = 0; eval('var n = 1'); return n; })()", "0");
    js_expect("(function(){ var n = 0; eval(\"'use strict'; var n = 1; n\"); return n; })()", "0");
    js_expect("(function(){ 'use strict'; return eval('var n = 2; function g() { return n; } g()');"
              " })()",
              "2");
    // An eval inside the eval sees what the outer one was handed.
    js_expect("(function(){ var v = 'a'; return eval(\"var v = 'b'; eval('v')\"); })()", "b");
    // A destructured parameter is still readable after a direct eval.
    js_expect("(function(...[a = 1]){ eval(''); return a; })()", "1");
    js_expect("(function({k}){ eval(''); return k; })({k: 'k'})", "k");
    // Where the eval is decides what its source may say (19.2.1.1 steps 4-7):
    // a strict caller's eval is strict code from its first line...
    js_expect("(function(){ 'use strict'; try { eval('var public = 1'); } catch (e) {"
              " return e.constructor.name; } return 'none'; })()",
              "SyntaxError");
    js_expect("(function(){ try { eval('var public = 1'); } catch (e) {"
              " return e.constructor.name; } return 'none'; })()",
              "none");
    // ...`new.target` needs a function, `return` is never the eval's...
    js_expect("(function(){ return eval('typeof new.target'); })()", "undefined");
    js_expect("(function(){ try { (0, eval)('new.target'); } catch (e) {"
              " return e.constructor.name; } return 'none'; })()",
              "SyntaxError");
    js_expect("(function(){ try { eval('return 1'); } catch (e) {"
              " return e.constructor.name; } return 'none'; })()",
              "SyntaxError");
    // ...`super.x` a method, whose home object the eval shares...
    js_expect("({ __proto__: { q: 5 }, m() { return eval('super.q'); } }).m()", "5");
    js_expect("(function(){ class A { get x() { return 1; } } class B extends A {"
              " y = eval('super.x'); z = () => super.x; } var b = new B();"
              " return b.y + b.z(); })()",
              "2");
    // ...the eval's `new.target` is its caller's...
    js_expect("(function(){ function F() { this.t = eval('new.target'); }"
              " return new F().t === F; })()",
              "true");
    // ...as an arrow's is its enclosing function's, eval or not...
    js_expect("(function(){ function F() { this.a = (() => new.target)();"
              " this.b = (() => eval('new.target'))(); }"
              " var f = new F(), g = {}; F.call(g);"
              " return (f.a === F) + ',' + (f.b === F) + ',' + g.a; })()",
              "true,true,undefined");
    // ...its `let`, `const` and `class` are its own, strict or not...
    js_expect("(function(){ eval('let q = 1; class K {}'); return typeof q + typeof K; })()",
              "undefinedundefined");
    js_expect("(function(){ try { eval('typeof w; let w;'); } catch (e) {"
              " return e.constructor.name; } return 'none'; })()",
              "ReferenceError");
    js_expect("(function(){ return eval('let v = 3; (() => v)()'); })()", "3");
    // ...the private names of the classes around it are its too...
    js_expect("(function(){ class C { #m = 44; v = eval('this.#m'); } return new C().v; })()",
              "44");
    js_expect("(function(){ class C { #x = 1; static #s() { return 2; }"
              " m() { return eval('this.#x + C.#s() + (#x in this)'); } }"
              " return new C().m(); })()",
              "4");
    // ...and a class field initialiser may not say `arguments`.
    js_expect("(function(){ class C { x = eval('arguments'); } try { new C(); } catch (e) {"
              " return e.constructor.name; } return 'none'; })()",
              "SyntaxError");
    // A new `var` or function belongs to the caller's function (19.2.1.3 steps
    // 16-17), not the global object: the rest of the body and its closures see it,
    // the global never does, and a `delete` removes it.
    js_expect("(function(){ eval('var fresh = 1; function made() { return 2; }');"
              " return fresh + made() + ',' + typeof globalThis.fresh + ','"
              " + (() => typeof made)(); })()",
              "3,undefined,function");
    js_expect("(function(){ eval('var gone = 1; delete gone;'); return typeof gone; })()",
              "undefined");
    js_expect("(function(){ eval('{ function blk() {} }'); return typeof blk + ','"
              " + typeof globalThis.blk; })()",
              "function,undefined");
    // Calling a function the eval declared passes undefined as `this`
    // (globalThis in sloppy mode), not the environment object.
    js_expect("(function(){ eval('function who() { return this === undefined ||"
              " this === globalThis; }'); return who(); })()",
              "true");
    // ...and one made by an eval in a parameter default is the body's.
    js_expect("((p = eval('var fromParam = 5')) => fromParam)()", "5");
    // An eval in a function inside it sees them too, and makes its own there.
    js_expect("(function(){ eval('var outer = 1'); return (function(){"
              " eval('var inner = outer + 1'); return eval('inner + outer'); })()"
              " + ',' + typeof inner; })()",
              "3,undefined");
    // Found through a `with` around the call, such a function still gets no
    // environment receiver, while the with object's own method gets the object.
    js_expect("(function(o){ eval('function who() { return this; }'); with (o) {"
              " return [who() === globalThis, own() === o].join(); } })"
              "({ own() { return this; } })",
              "true,true");
    // Strict code in a default reads through the object, and makes nothing on it.
    js_expect("(function(a = eval('\"use strict\"; var q = 1; typeof zz + q')) { return a +"
              " ',' + typeof q; })()",
              "undefined1,undefined");
    REPORT("direct_eval");
}
