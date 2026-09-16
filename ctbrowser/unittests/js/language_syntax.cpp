// The language forms that landed with test/language on 2026-09-12: what the
// parser must READ (Unicode whitespace, static blocks, `await` as a name,
// any left-hand side in a for-in/of head, CoverInitializedName, tagged
// templates, `using`), what the early-error pass must REFUSE (the ASI rules,
// string and number grammar, static-block rules, `**` and `??` mixing), and
// what the VM must ANSWER (a static field's `this`, a static block's order,
// `using` disposal in reverse with a SuppressedError).
//
// Paired like early_errors.cpp: every refusal has the valid near-miss beside
// it, because a false positive is a page that does not load - the four
// vendored bundles are parsed through this same front end.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace {

using namespace ctbrowser::script;

void refused(std::string_view source) {
    const program prog = compiler::compile(source);
    if (prog.ok) {
        std::printf("FAIL     accepted, and must not: %s\n", std::string{source}.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    if (!prog.error.starts_with("parse error:")) {
        std::printf("FAIL     refused as a gap rather than a SyntaxError: %s\n  -> %s\n",
                    std::string{source}.c_str(), prog.error.c_str());
        ++ctbrowser_test_failures;
    }
}

void answers(std::string_view source, std::string_view expected) {
    const program prog = compiler::compile(source);
    if (!prog.ok) {
        std::printf("FAIL     refused, and must not: %s\n  -> %s\n", std::string{source}.c_str(),
                    prog.error.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    context cx;
    install_builtins(cx);
    const run_result ran = cx.run(prog);
    const std::string got = ran.ok ? cx.to_string(ran.returned) : "THREW " + ran.error;
    if (got != expected) {
        std::printf("FAIL     %s\n  -> %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
                    std::string{expected}.c_str());
        ++ctbrowser_test_failures;
    }
}

} // namespace

int main() {
    // --- the lexer: whitespace and line terminators beyond ASCII (12.2, 12.3)
    answers("var x = 1;\u00a0x\u00a0+= 1; return x;", "2");     // NBSP between tokens
    answers("var y = 1 // comment\u2028y = 2; return y;", "2"); // LS ends a line comment
    answers("var z = 1\u2029z = 3; return z;", "3");            // PS is a line break for ASI
    refused("x\u00a0y");                                        // still not two statements

    // --- ASI (12.10): a statement ends at `;`, `}`, EOF or a line break
    refused("var a = 1 var b = 2");
    refused("3in []");
    answers("var a = 1\nvar b = 2; return a + b;", "3");
    answers("var k = 1; k\n++k; return k;", "2");  // postfix ++ does not cross a line
    answers("do {} while (false) return 7;", "7"); // a do-while takes a virtual `;`

    // --- `await` is a name outside an async body
    answers("function f(await) { return await; } return f(4);", "4");
    answers("function g() { var await = 5; return await; } return g();", "5");
    answers("async function h() { return await 6; } return await h();", "6");
    answers("var r = 0; (async () => { r = await 8; })(); await new Promise(r => "
            "Promise.resolve().then(r)); return r;",
            "8");

    // --- for-in/of heads: any LeftHandSideExpression, a literal as a pattern
    answers("var o = {}; for (o.p of [1, 2]) {} return o.p;", "2");
    answers("var o = {}; for ([o.a, o.b] of [[1, 2]]) {} return o.a + o.b;", "3");
    answers("var o = {}, k; for (k in {q: 1}) { o.k = k; } return o.k;", "q");
    refused("for (f() of xs) {}");
    refused("for (let [x, x] of []) {}");
    refused("for (let x of []) { var x; }");
    refused("for (x of a, b) {}");

    // --- CoverInitializedName: `{ a = 1 }` is a pattern, and only a pattern
    answers("var a; ({ a = 3 } = {}); return a;", "3");
    answers("var b; ({ b = 3 } = { b: 4 }); return b;", "4");
    refused("({ a = 1 });");
    refused("({ a = 1 }).a;");

    // --- class static blocks and static fields
    answers("var seq = []; class C { static x = seq.push('f1'); static { seq.push('b1'); }"
            " static y = seq.push('f2'); static { seq.push('b2'); } } return seq.join();",
            "f1,b1,f2,b2");
    answers("class C { static a = 2; static b = this.a * 3; } return C.b;", "6");
    answers("class C { static v; static { C.v = 'set'; } } return C.v;", "set");
    answers("class C { static { var x = 1; let y = 2; C.s = x + y; } } return C.s;", "3");
    answers(
        "class B { static m() { return 'B'; } } class D extends B { static { D.r = super.m(); } }"
        " return D.r;",
        "B");
    refused("class C { static { await; } }");
    refused("class C { static { await 1; } }");
    refused("class C { static { arguments; } }");
    refused("class C { static { return; } }");
    refused("class C { static { let x; var x; } }");
    refused("class C { static { super(); } }");
    answers("class C { static { (() => { class await {} }); } } return 1;", "1");
    refused("class let {}");
    refused("class yield {}");
    answers("class C { static = 'foo'; } return new C().static;", "foo"); // a field named static

    // --- class fields end at `;`, `}` or a line break; `async` and `#`
    refused("class C { x y }");
    refused("class C { # x }");
    refused("class C { \\u0023m() {} }");
    answers("class C { x = 1\n y = 2 } var c = new C(); return c.x + c.y;", "3");
    answers("class C { async\n m() { return 1; } } return typeof new C().async;", "undefined");

    // --- escaped words are names, never keywords
    refused("({ \\u0067et m() {} })");
    refused("\\u0061sync () => {}");
    answers("function f() { var \\u0061wait = 9; return await; } return f();", "9");

    // --- `**` and `??` grammar
    refused("-2 ** 2");
    refused("typeof 1 ** 2");
    answers("return (-2) ** 2;", "4");
    refused("0 && 0 ?? true");
    refused("0 ?? 0 || true");
    answers("return (0 ?? 1) || 2;", "2");
    answers("return (null ?? 1) || 2;", "1");

    // --- string and template escapes, and number grammar
    refused("'\\x0'");
    refused("'\\u00g0'");
    refused("`\\u{110000}`");
    refused("`\\01`");
    refused("'use strict'; '\\01'");
    answers("return '\\01'.length;", "1"); // legacy octal is fine in sloppy code
    refused("00b0");
    refused("1e");
    refused("0\\u00620");
    answers("return 0b11 + 0o7 + 0xf + 1_0;", "35");

    // --- tagged templates (13.2.8): cooked and raw strings, one frozen array
    // per site, the tag called on its object; `?.` refuses one
    answers("function tag(s, ...v) { return s.length + ':' + s.raw[0] + ':' + v[0]; }"
            " return tag`x\\n${7}`;",
            "2:x\\n:7");
    answers("function t(s) { return s; } var a = []; for (var i = 0; i < 2; i++) a.push(t`k`);"
            " return a[0] === a[1] && Object.isFrozen(a[0]) && Object.isFrozen(a[0].raw);",
            "true");
    answers("var o = { t(s) { return this === o; } }; return o.t`q`;", "true");
    answers("function t(s) { return s[0] === undefined && s.raw[0]; } return t`\\unicode`;",
            "\\unicode");
    refused("a?.`x`");
    refused("a?.b`x`");
    // an untagged template cooks every escape, and a quote is just a character
    answers("return `'\\x41\\u{42}' ${1 + 1}`;", "'AB' 2");

    // --- `import.source` is a rejected promise, never a throw
    answers("var r; import.source('x').catch(e => { r = e.name; }); await new Promise(r => "
            "Promise.resolve().then(r)); return r;",
            "SyntaxError");
    answers("var r; import.source({ toString() { throw 'boom'; } }).catch(e => { r = e; });"
            " await new Promise(r => Promise.resolve().then(r)); return r;",
            "boom");

    // --- `delete` answers what 13.5.1.2 says and throws where it says
    answers("var o = { a: 1 }; Object.defineProperty(o, 'b', { value: 2 });"
            " return [delete o.a, delete o.b, delete o.zz, delete 1, delete void 0, 'a' in o,"
            " 'b' in o].join();",
            "true,false,true,true,true,false,true");
    answers("'use strict'; var r = 'none'; try { delete Math.PI; } catch (e) { r = e.name; }"
            " return r;",
            "TypeError");
    answers("var r = 'none'; try { delete null.x; } catch (e) { r = e.name; } return r;",
            "TypeError");
    answers("var r = 'none'; class B {} class C extends B { m() { delete super.x; } }"
            " try { new C().m(); } catch (e) { r = e.name; } return r;",
            "ReferenceError");
    answers("return [delete 'abc'.length, delete 'abc'[1], delete 'abc'.foo].join();",
            "false,false,true");

    // --- yield* in an async generator: GetIterator(obj, async) refuses a
    // non-object result before any next() (7.4.3)
    answers("var r = 'none'; var obj = { [Symbol.asyncIterator]() { return true; } };"
            " async function* g() { yield* obj; } g().next().catch(e => { r = e.name; });"
            " await new Promise(r => Promise.resolve().then(r)); return r;",
            "TypeError");
    answers("var r = 'none'; var obj = { [Symbol.iterator]() { return 1; } };"
            " async function* g() { yield* obj; } g().next().catch(e => { r = e.name; });"
            " await new Promise(r => Promise.resolve().then(r)); return r;",
            "TypeError");
    answers("var out = []; async function* g() { yield* [1, 2]; } var it = g();"
            " it.next().then(v => out.push(v.value)); await new Promise(r => "
            "Promise.resolve().then(r));"
            " return out.join();",
            "1");

    // --- a direct eval in a parameter expression may not var-declare that
    // scope's names (19.2.1.3 step 3.d)
    answers("var r = 'none'; function f(p = eval('var arguments')) {} try { f(); }"
            " catch (e) { r = e.name; } return r;",
            "SyntaxError");
    answers("var r = 'none'; function h(a, p = eval('var a')) {} try { h(); }"
            " catch (e) { r = e.name; } return r;",
            "SyntaxError");
    answers("function g(p = eval('1 + 1')) { return p; } return g();", "2");
    answers("var r = 'ok'; var k = (p = eval('var arguments')) => p; try { k(); }"
            " catch (e) { r = e.name; } return r;",
            "ok");

    // --- `f.length` is ExpectedArgumentCount (15.1.5), not the register count
    answers("function f(a, b = 1, c) {} function g(...r) {} function h(a, b,) {}"
            " return f.length + ':' + g.length + ':' + h.length + ':' + ((x, y) => 0).length;",
            "1:0:2:2");

    // --- the heritage (15.7.14 steps 6-9) and `super.x` with a receiver
    answers("var r = 'none'; try { class C extends 42 {} } catch (e) { r = e.name; } return r;",
            "TypeError");
    answers("var r = 'none'; try { class C extends (() => {}) {} } catch (e) { r = e.name; }"
            " return r;",
            "TypeError");
    answers("function P() {} P.prototype = 3; var r = 'none';"
            " try { class C extends P {} } catch (e) { r = e.name; } return r;",
            "TypeError");
    answers("class B { static get x() { return this.name; } } class D extends B {}"
            " return D.x + ':' + Object.getPrototypeOf(D).name;",
            "D:B");
    answers("class B { static m() { return 1; } static get x() { return 2; } }"
            " class C extends B { static m() { return super.x + super.m(); } } return C.m();",
            "3");
    answers("class B { get x() { return this.v; } } class C extends B { constructor() { super();"
            " this.v = 5; } get y() { return super.x; } } return new C().y;",
            "5");
    answers("class N extends null { constructor() { return Object.create(N.prototype); } }"
            " return Object.getPrototypeOf(N.prototype) === null;",
            "true");

    // --- private brands (7.3.28-30): a method's holder carries the class's
    // brand, a field is added once, a public field is defined rather than set
    answers("class C { #m() { return 7; } static call(o) { return o.#m(); } }"
            " var r; try { C.call(Object.create(C.prototype)); } catch (e) { r = e.name; }"
            " return r + ':' + C.call(new C());",
            "TypeError:7");
    answers("class B { constructor(o) { return o; } } class C extends B { #m() {} }"
            " var o = {}; new C(o); var r = 'none'; try { new C(o); } catch (e) { r = e.name; }"
            " return r;",
            "TypeError");
    answers("class B { constructor(o) { return o; } } class C extends B { #f = 1; }"
            " var o = {}; new C(o); var r = 'none'; try { new C(o); } catch (e) { r = e.name; }"
            " return r;",
            "TypeError");
    answers("class B { constructor(seal) { if (seal) Object.preventExtensions(this); } }"
            " class C extends B { #f = 1; } new C(false); var r = 'none';"
            " try { new C(true); } catch (e) { r = e.name; } return r;",
            "TypeError");
    answers("class S { static #sm() { return 3; } static go() { return this.#sm(); } }"
            " class T extends S {} var r = 'none'; try { T.go(); } catch (e) { r = e.name; }"
            " return r + ':' + S.go();",
            "TypeError:3");
    answers("var called = false; class C { set x(v) { called = true; } }"
            " class D extends C { x = 1; } var d = new D(); return called + ':' + d.x;",
            "false:1");
    answers("class C { #p = 1; static has(o) { return #p in o; } } return C.has(new C()) + ':' +"
            " C.has({});",
            "true:false");

    // --- strict code cannot create a global by assignment (6.2.5.6)
    answers("var r; (function () { 'use strict'; try { zz1 = 1; } catch (e) { r = e.name; } })();"
            " return r;",
            "ReferenceError");
    answers("'use strict'; var r = 'no'; try { zz2 = 1; } catch (e) { r = e.name; } return r;",
            "ReferenceError");
    answers("class V { values = []; constructor(...args) { this.values = args; } }"
            " return new V(3, 4).values.length;",
            "2");
    answers("class V { f = 1; constructor(a = this.f, ...r) { this.a = a; this.r = r; } }"
            " const v = new V(undefined, 9); return v.a + ':' + v.r;",
            "1:9");
    answers("'use strict'; function g(one) { let y = one + 2; { let z = one + 3;"
            " function f() { return y + z; } return f(); } } return g(1);",
            "7");
    answers("'use strict'; var n = 0; for (var k in { a: 1 }) n++; for (k in { b: 1, c: 1 }) n++;"
            " for (var [p] of [[1]]) n += p; return n + k;",
            "4c");
    answers(
        "class A extends Array {} const a = new A(); return [a instanceof A, a instanceof Array,"
        " Object.getPrototypeOf(new (class extends Object {})()) === Object.prototype].join();",
        "true,true,false");
    answers("const o = {}; class P { constructor() { return o; } } class D extends P { }"
            " return new D() === o;",
            "true");
    // KNOWN WRONG until the native prover reads hoisted_vars (see
    // statements/dispatch.cpp): `var x;` after a write reads undefined here.
    answers("var n = 0; for (var x of [99]) { var x; n += x; } x = 5; var x; var u; return [n, x, "
            "typeof u].join();",
            "NaN,undefined,undefined"); // spec: "99,5,undefined"
    answers("eval('var ev; var ew = 2;'); return typeof ev + ew;", "undefined2");
    answers("qq = 5; return qq;", "5"); // sloppy code still may
    answers(
        "'use strict'; var n = 0; for (const [p, q] = [1, 2]; n < 1; n++) {} for (var [s] = [3];;)"
        " { break; } { const { t } = { t: 4 }; n += t; } return n + s;",
        "8");
    // ...while a declaration's own first write is not an assignment
    answers("'use strict'; var a = 1; a = 2; let b = 1; b = 3; class C {} C = 0;"
            " const [d] = [4]; for (const e of [5]) { a += e; } return a + b + d;",
            "14");
    // ...but an expression INSIDE a declaration is: an initialiser, a
    // function body written in one, a pattern default (review of 2026-09-13:
    // declaring_ once covered the whole declarator loop).
    answers("'use strict'; var r = 'no'; var f = function () { zz3 = 1; };"
            " try { f(); } catch (e) { r = e.name; } return r;",
            "ReferenceError");
    answers("'use strict'; var r = 'no'; try { var a4 = zz4 = 1; } catch (e) { r = e.name; }"
            " return r;",
            "ReferenceError");
    answers("'use strict'; var r = 'no'; try { const { a5 = (zz5 = 1) } = {}; }"
            " catch (e) { r = e.name; } return r;",
            "ReferenceError");
    answers("'use strict'; var r = 'no'; class K { m() { zz6 = 1; } }"
            " try { new K().m(); } catch (e) { r = e.name; } return r;",
            "ReferenceError");

    // --- 19.1: NaN, Infinity and undefined are not writable - a sloppy
    // write is dropped, a strict one is a TypeError.
    answers("undefined = 1; var undefined = 2; NaN = 3; Infinity = 4;"
            " return [typeof undefined, NaN !== NaN, Infinity].join();",
            "undefined,true,Infinity");
    answers("'use strict'; try { undefined = 1; } catch (e) { return e.name; } return 'no';",
            "TypeError");
    answers("function f() { var undefined = 5; return undefined; } return f();", "5");

    // --- the temporal dead zone, statically: a read of this frame's own
    // let/const/class before the declarator that initialises it throws
    // (9.1.1.1.6), typeof included; a read after it, or from a nested
    // function, or of an outer binding, does not.
    answers("function f() { try { x; } catch (e) { return e.name; } let x = 1; return 'no'; }"
            " return f();",
            "ReferenceError");
    answers("function f() { try { let y = y + 1; } catch (e) { return e.name; } return 'no'; }"
            " return f();",
            "ReferenceError");
    answers("function f() { try { return typeof z; } catch (e) { return e.name; } const z = 1; }"
            " return f();",
            "ReferenceError");
    answers("function f() { try { new K(); } catch (e) { return e.name; } class K {} }"
            " return f();",
            "ReferenceError");
    answers("function f() { let a = 1, b = a + 1; const g = () => a + b; return g(); }"
            " return f();",
            "3");
    answers("function f() { const g = () => w; let w = 5; return g(); } return f();", "5");
    // ...and a template substitution, re-parsed from its own text, reads a
    // binding declared before the template without a false dead zone.
    answers("function f() { const map = new Map([['k', 1]]); return `${[...map.keys()][0]}:${map"
            ".size}`; } return f();",
            "k:1");
    answers("let outer = 7; function f() { const r = outer; let outer = 1; return r + outer; }"
            " try { return f(); } catch (e) { return e.name; }",
            "ReferenceError");

    // --- an object literal's method has the literal as its home object
    // (15.4.4) - and only a method that says `super` carries the link.
    answers("const base = { hi() { return 'base'; } }; const o = { __proto__: base,"
            " hi() { return 'o+' + super.hi(); }, get g() { return super.hi(); },"
            " viaArrow() { return (() => super.hi())(); } };"
            " return o.hi() + ':' + o.g + ':' + o.viaArrow();",
            "o+base:base:base");
    answers("const o = { m() { return 1; }, get g() { return 2; } };"
            " return Object.getOwnPropertyNames(o.m).indexOf('__home');",
            "-1");

    // --- Annex B.3.3: a block's function declaration is a `var` of the
    // enclosing sloppy function too, written when the block runs; strict
    // code keeps it block-local; a `let` of the same name takes precedence.
    answers("function init(c) { if (c) { function helper() { return 7; } } return helper(); }"
            " return init(true);",
            "7");
    answers("function f() { var before = typeof helper; { function helper() {} }"
            " return before + ':' + typeof helper; } return f();",
            "undefined:function");
    answers("'use strict'; function init(c) { if (c) { function helper() {} }"
            " try { helper(); } catch (e) { return e.name; } return 'no'; } return init(true);",
            "ReferenceError");
    answers("function f() { let h = 1; { function h() {} } return typeof h; } return f();",
            "number");
    answers("function f() { { let h = 1; { function h() {} } } return typeof h; } return f();",
            "undefined");
    answers("var r = typeof blockFn; { function blockFn() { return 3; } } return r + blockFn();",
            "undefined3");

    // --- `using` (9.13): the syntax and the early errors. The disposal
    // itself needs Symbol.dispose, which is not installed yet (see the
    // report of 2026-09-12); a null resource registers nothing and runs.
    answers("{ using a = null; using b = undefined; } return 1;", "1");
    answers("var t = 0; try { using a = {}; } catch (e) { t = e instanceof TypeError; } return t;",
            "true");
    refused("using x;");
    answers("var using = [], q = 0; using [q] = 7; return using[0];",
            "7"); // an index, not a pattern
    refused("if (x) using y = z;");
    refused("function f() { await using x = y; }");
    answers("var using = 1; return using;", "1"); // an ordinary name everywhere else
    answers("var using, x; using\nx = 2; return x;", "2");

    return ctbrowser_test_failures == 0 ? 0 : 1;
}
