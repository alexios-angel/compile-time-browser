//===- ctbrowser/unittests/js/early_errors.cpp - Early errors ---*- C++ -*-===//
//
// The early errors: source text this engine must refuse, and the valid
// near-miss beside each one that it must still run. lib/Script/compile/
// early_errors/ is the pass that enforces them, and its header,
// compile/early_errors.hpp, says which families are deliberately absent.
// Section numbers in the banners below are ECMA-262 clauses.
//
// Every refusal here is paired. The `refused` line proves the rule fires; the
// `accepted` line beside it proves the rule stops where it should, and that
// one matters more: a false positive is not a wrong answer, it is a page that
// does not load. The accepted cases are the shapes that the p5.js, Phaser,
// Babylon.js and Bootstrap bundles contain, written small enough to read.
//
// A refused source must fail as a SyntaxError, not as an unimplemented
// construct. The two are told apart by the error message's "parse error:"
// prefix (as `ct262` does), so `refused` checks the prefix, not just
// `ok == false`.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/script/script.hpp>

#include "check.hpp"

#include <string>
#include <string_view>

namespace {

/// Checks that `source` fails to compile with a SyntaxError, not as an
/// unimplemented construct.
void refused(std::string_view source) {
    using namespace ctbrowser::script;
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

/// refused(), compiling `source` as a module. `import` and `export` exist in no
/// other grammar, and a module's top level has rules a script's does not.
/// Only the compile is checked: running a module needs the loader, which
/// test/corpus/modules covers.
void module_refused(std::string_view source) {
    using namespace ctbrowser::script;
    const program prog = compiler::compile(source, script_kind::module_);
    if (prog.ok) {
        std::printf("FAIL     accepted as a module, and must not: %s\n",
                    std::string{source}.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    if (!prog.error.starts_with("parse error:")) {
        std::printf("FAIL     refused as a gap rather than a SyntaxError: %s\n  -> %s\n",
                    std::string{source}.c_str(), prog.error.c_str());
        ++ctbrowser_test_failures;
    }
}

/// Checks that `source` compiles as a module (see module_refused()).
void module_accepted(std::string_view source) {
    using namespace ctbrowser::script;
    const program prog = compiler::compile(source, script_kind::module_);
    if (!prog.ok) {
        std::printf("FAIL     refused as a module, and must not: %s\n  -> %s\n",
                    std::string{source}.c_str(), prog.error.c_str());
        ++ctbrowser_test_failures;
    }
}

/// Checks that `source` compiles and runs without throwing. Running matters:
/// half the near-misses below are about scope, and a scope rule can be wrong
/// in a way that compiles to the wrong register rather than to no program.
void accepted(std::string_view source) {
    using namespace ctbrowser::script;
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
    if (!ran.ok) {
        std::printf("FAIL     compiled and then threw: %s\n  -> %s\n", std::string{source}.c_str(),
                    ran.error.c_str());
        ++ctbrowser_test_failures;
    }
}

/// Checks that `source` compiles, runs and returns `expected`, for the cases
/// where "it ran" is not enough and the scope it ran in is the point.
void answers(std::string_view source, std::string_view expected) {
    using namespace ctbrowser::script;
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
    const std::string got = ran.ok ? cx.to_string(ran.returned) : std::string{"THREW"};
    if (got != expected) {
        std::printf("FAIL     %s\n  -> %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
                    std::string{expected}.c_str());
        ++ctbrowser_test_failures;
    }
}

} // namespace

int main() {
    // Script and eval use [~Await]; the embedding API and modules use [+Await].
    for (const std::string_view source :
         {"function await(n) { return n + 1; } if (await(41) !== 42) throw 0;",
          "'use strict'; function await() { return 42; } if (await() !== 42) throw 0;",
          "var await = 41; await += 1; if (await !== 42) throw 0;",
          "class await {} if (!(new await() instanceof await)) throw 0;",
          "async function await() {} if (typeof await !== 'function') throw 0;"}) {
        using namespace ctbrowser::script;
        const program prog = compiler::compile_script(source);
        check(prog.ok, source);
        if (prog.ok) {
            context cx;
            install_builtins(cx);
            check(cx.run(prog).ok, source);
        }
        module_refused(source);
    }
    for (const std::string_view source : {"await 1;", "await Promise.resolve(1);"}) {
        using namespace ctbrowser::script;
        const program script = compiler::compile_script(source);
        check(!script.ok && script.error.starts_with("parse error:"), source);
        const program eval = compiler::compile_for_eval(source);
        check(!eval.ok && eval.error.starts_with("parse error:"), source);
        accepted(source);
        module_accepted(source);
    }
    answers("return eval('function await(n) { return n + 1; } await(41);');", "42");
    answers("return (0, eval)('function await() { return 42; } await();');", "42");
    answers("return await (async function() {"
            " return eval('function await() { return 42; } await();'); })();",
            "42");
    answers("try { eval('await 1;'); } catch (e) { return e.name; }", "SyntaxError");
    answers("try { (0, eval)('await 1;'); } catch (e) { return e.name; }", "SyntaxError");
    answers("async function f() { return (await /x/).test('x'); } return await f();", "true");
    module_accepted("const r = await /x/;");

    // A statement body may be `let` followed by a newline - the identifier -
    // but never `let [` (14.5's lookahead), newline or comment between.
    refused("if (false) let\n[a] = 0;");
    refused("L: let /* x */\n[a] = 0;");
    accepted("if (false) let\nx = 1;");
    // A call as an assignment target is web-compat in sloppy code: the call
    // runs, then the ReferenceError - the right side never does.
    answers("var log = ''; function f() { log += 'f'; return {}; } function g() { log += 'g'; }"
            " var r = []; try { f() = g(); } catch (e) { r.push(e.name); }"
            " try { f() += g(); } catch (e) { r.push(e.name); }"
            " try { f()++; } catch (e) { r.push(e.name); } return r.join() + ':' + log;",
            "ReferenceError,ReferenceError,ReferenceError:fff");
    answers("var n = 0; function f() { ++n; return {}; } var r = [];"
            " try { for (f() in [1]) {} } catch (e) { r.push(e.name); }"
            " try { for (f() of [1]) {} } catch (e) { r.push(e.name); }"
            " for (f() in {}) {} return r.join() + ':' + n;",
            "ReferenceError,ReferenceError:2");
    refused("'use strict'; function f() {} f() = 1;");
    refused("function f() {} f() &&= 1;");
    refused("var a; a?.b() = 1;");
    // A binding the parser took leniently from a token that is no name.
    refused("var 1 = 1;");
    // A module's top level is [~Return], and a default-exported declaration
    // ends at its `}`; parenthesised it is an expression and may be called.
    module_refused("return 1;");
    module_refused("export default function () {}();");
    module_accepted("export default (function () { return 1; })();");
    // By token: a comment is neither the parenthesis nor proof there is none.
    module_accepted("export default (/* c */ function () { return 1; })();");
    module_refused("export default // (\n function () {}();");
    // B.3.1: a label's item may be a (labelled) function declaration in
    // sloppy code - never in strict code, nor as an `if` body.
    accepted("label1: label2: function f() {}");
    refused("'use strict'; label1: label2: function f() {}");
    refused("if (1) label1: function f() {}");
    refused("function f(1, 1) {}");
    // An escaped reserved word is still that word (12.7.1.1).
    refused("var \\u0069mport = 1;");
    refused("var \\u{64}efault = 1;");
    refused("'use strict'; var \\u0070ublic = 1;");
    accepted("var \\u0070ublic = 1, \\u0061b = 2;");
    accepted("var $a, _b, \\u0061c, \xc3\xa9t\xc3\xa9;");

    // ================================================================
    // 1. Duplicate lexical declarations - 14.2.1, 15.2.1, 16.1.1
    // ================================================================
    refused("let x; let x;");
    refused("let x; const x = 1;");
    refused("const x = 1; class x {}");
    refused("class C {} class C {}");
    refused("let x; var x;");
    refused("var x; let x;");
    refused("{ let y; var y; }");
    refused("let f; function f() {}");
    refused("function g() { let a; var a; }");
    refused("switch (0) { case 1: let s; default: let s; }");
    // A `for` head is a scope, and its own names must be distinct.
    refused("for (let i = 0, i = 1; ; ) {}");
    // ...and the body's `var` reaches into it. 14.7.4.1.
    refused("for (let i = 0; ; ) { var i; }");

    // ...and the near-misses. Shadowing is the whole point of block scope.
    accepted("let x = 1; { let x = 2; }");
    accepted("let x = 1; { var y = 2; }");
    accepted("var x; var x;");
    accepted("function f() {} function f() {}");
    accepted("{ let s = 1; } { let s = 2; }");
    accepted("switch (0) { case 1: { let s; break; } default: { let s; } }");
    accepted("for (let i = 0; i < 1; ++i) { let i2 = i; }");
    // A loop variable and a body binding of the same name are two scopes, not
    // a redeclaration; every bundle in vendor/ contains this shape.
    accepted("for (let i = 0; i < 1; ++i) { let i = 9; }");
    accepted("for (const item of [1, 2]) { let item2 = item; }");

    // A `var` and a function of the same name at the top of a function body is
    // legal (both are var-scoped there), and so is a function declaration in a
    // block that shares its name with an outer binding.
    accepted("function h() { var k; function k() {} }");
    accepted("let m; { function m() {} }");
    // Annex B.3.3.2: a function declaration as the whole body of an `if` is
    // hoisted only where hoisting would not be an early error, so it may not
    // make one.
    accepted("let n = 1; if (true) function n() {}");

    // ================================================================
    // 2. A parameter, and the body that may not redeclare it - 15.2.1
    // ================================================================
    refused("function f(a) { let a; }");
    refused("function f(a) { const a = 1; }");
    refused("function f(a) { class a {} }");
    refused("(a) => { let a; };");
    // 14.15.1 says the same of a catch parameter and its block.
    refused("try {} catch (e) { let e; }");

    // `var` re-declaring a parameter is legal and common in transpiled code,
    // and so is a lexical binding in a nested block of the body.
    accepted("function f(a) { var a; return a; }");
    accepted("function f(a) { { let a = 1; return a; } }");
    accepted("try {} catch (e) { var e; }");
    accepted("try { null.x; } catch (e) { let inner = e; }");
    answers("function f(a) { { let a = 2; } return a; } return f(1);", "1");

    // ================================================================
    // 3. Duplicate parameter names - 15.1.2, 15.2.1, 15.3.1
    // ================================================================
    // Only where the specification says so without strict mode: a list that is
    // not simple, an arrow, or a method.
    refused("function f(a, a = 1) {}");
    refused("function f(a, ...a) {}");
    refused("function f(a, [a]) {}");
    refused("var f = (a, a) => a;");
    refused("var o = { m(a, a) {} };");
    refused("class C { m(a, a) {} }");
    // 15.1.1: a rest parameter is the last one.
    refused("function f(...rest, last) {}");

    // A duplicate in a simple parameter list is legal in sloppy code, so it is
    // accepted here; section 19 checks that strict code refuses it.
    accepted("function f(a, a) { return a; }");
    accepted("function f(a, b = 1, ...rest) { return a; }");
    accepted("var f = (a, b) => a + b;");
    accepted("var o = { m(a, b) { return a; } };");

    // ================================================================
    // 4. `const` without an initialiser - 14.3.1.1
    // ================================================================
    refused("const c;");
    refused("const a = 1, b;");
    refused("for (const i; ; ) {}");

    accepted("const c = 1;");
    accepted("const a = 1, b = 2;");
    // A for-in/of head is the one `const` binding with no initialiser of its
    // own, and it must stay legal.
    accepted("for (const item of [1, 2]) { }");
    accepted("for (const key in { a: 1 }) { }");

    // ================================================================
    // 5. An assignment target that is not one - 13.15.1
    // ================================================================
    refused("1 = 2;");
    refused("'a' = 2;");
    refused("null = 1;");
    refused("true = 1;");
    refused("this = 1;");
    refused("(a, b) = 2;");
    refused("a + b = 2;");
    refused("(x => x) = 1;");
    refused("x?.y = 1;");
    refused("++(a + b);");
    refused("(a + b)--;");
    refused("[a] += b;");
    refused("1 += 2;");

    // The simple targets, and the two literals that are patterns rather than
    // operands.
    accepted("var a, b, o = {}, xs = [1, 2];");
    accepted("var a; a = 1;");
    accepted("var o = {}; o.x = 1; o['y'] = 2;");
    accepted("var a; (a) = 1;");
    accepted("var a, b, xs = [1, 2]; [a, b] = xs;");
    accepted("var a; ({ a } = { a: 1 });");
    accepted("var o = { p: 0 }; [o.p] = [1];");
    accepted("var i = 0; i++; ++i; i += 1;");
    // A call is refused in strict code only: sloppy code gets the web-compat
    // runtime ReferenceError (see the top of main and web_compat_call).
    refused("'use strict'; function f() { return {}; } f() = 1;");
    refused("'use strict'; f() += 1;");
    refused("'use strict'; f()++;");

    // ================================================================
    // 6. `break`, `continue` and labels - 13.8.1, 13.9.1, 14.13.1
    // ================================================================
    refused("break;");
    refused("continue;");
    refused("if (true) { break; }");
    refused("while (0) { function f() { break; } }");
    refused("break nowhere;");
    refused("outer: while (0) { continue elsewhere; }");
    // A label that is not on a loop cannot be continued to, only broken out of.
    refused("block: { continue block; }");
    refused("a: a: while (0) {}");

    accepted("while (0) { break; }");
    accepted("for (;;) { break; }");
    accepted("do { continue; } while (0);");
    accepted("switch (0) { case 0: break; }");
    accepted("outer: while (0) { break outer; }");
    // A bounded loop, not `for (;;)`, because `accepted` also runs the source.
    // The counter is incremented in the body so the loop ends whether
    // `continue` goes to the update or to the test.
    accepted("var i = 0; outer: for (; i < 2; ) { ++i; continue outer; }");
    // A label on a labelled loop still names a loop, which is what makes this
    // legal - the label set is unwrapped rather than tested one deep.
    accepted("var j = 0; a: b: for (; j < 2; ) { ++j; continue a; }");
    accepted("block: { break block; }");
    // Two labels of one name in sequence are fine; only nesting is an error.
    accepted("a: ; a: ;");
    accepted("function f() { L: for (;;) { break L; } } f();");

    // ================================================================
    // 7. `new.target` and `super` outside anything that has one
    // ================================================================
    refused("super.x;");
    refused("super();");
    refused("function f() { super.x; }");

    accepted("function f() { return new.target; } f();");
    accepted("function f() { var g = () => new.target; return g(); } f();");
    accepted("class C { m() { return super.toString; } }");
    accepted("class C { constructor() { this.x = 1; } } new C();");
    accepted("var o = { m() { return super.toString; } };");
    // `return` at the top level is not refused: it is this engine's embedding
    // contract, the way a compiled program hands a value back. See the note on
    // `nk::return_stmt` in early_errors/statements.cpp.
    answers("return 42;", "42");
    // `new.target` at the top level is accepted for the same reason: 16.1.1
    // admits both only inside a function, and the top level is a function body
    // in this engine. With no constructor it is `undefined`.
    answers("return String(new.target);", "undefined");
    answers("var f = () => new.target; return String(f());", "undefined");
    // `super` is different: it needs a home object, which no top level has.

    // ================================================================
    // 8. `delete` of a private member - 13.5.1.1
    // ================================================================
    // The one `delete` rule that does not need strict mode. `delete x` on a
    // plain identifier is an error only in strict code, so it is accepted here.
    refused("class C { #x; m() { delete this.#x; } }");
    refused("class C { #x; m() { delete (this.#x); } }");

    accepted("var o = { p: 1 }; delete o.p;");
    accepted("var o = { p: 1 }; delete o['p'];");
    accepted("delete undeclared;");
    accepted("class C { #x = 1; m() { return this.#x; } } new C().m();");

    // ================================================================
    // 9. More of 15.7.1, and the contextual names - 15.5.1, 15.8.1
    // ================================================================
    // A field initialiser may not mention `arguments`, through an arrow but
    // not through a function; no field may be named `constructor`, static or
    // not; `extends` takes a left-hand-side expression; `await` is not a name
    // in an async function and `yield` is not one in a generator.
    refused("class C { x = arguments; }");
    refused("class C { x = () => arguments; }");
    refused("class C { static #x = arguments.length; }");
    accepted("class C { x = function () { return arguments; }; }");
    refused("class C { static 'constructor' = 1; }");
    refused("class C { constructor = 1; }");
    refused("class C extends () => {} {}");
    refused("class C extends a = b {}");
    accepted("var b = class {}; var a = class extends b {}; var c = class extends (b) {};");
    refused("class C { async m() { var await; } }");
    refused("class C { async m(await) {} }");
    refused("class C { *g() { var yield; } }");
    refused("async function f() { var await; }");
    refused("function* g() { var yield; }");
    // Outside a generator, sloppy `yield` is a name (the parser tracks the
    // generator context; an arrow's body leaves it).
    accepted("function f() { var yield = 1; return yield; } f();");
    accepted("var yield = 23; var f = (x = yield) => x; f();");
    accepted("function* g() { var f = () => { var yield = 1; return yield; }; yield f(); }");
    accepted("async function f() { var g = () => 1; return g(); }");

    // ================================================================
    // 10. import() is a call expression - 13.3.10
    // ================================================================
    // Grammar, so the parser's refusal: one or two arguments, never a
    // `new`, and a bare `import` is not an expression.
    refused("new import('x');");
    refused("new import('x').prop;");
    refused("import('x', {}, '');");
    refused("typeof import;");
    accepted("var p = () => import('x', {});");
    accepted("var p = () => import('x',);");

    // ================================================================
    // 11. Two `__proto__` in one object literal - B.3.1
    // ================================================================
    refused("var o = { __proto__: null, __proto__: null };");
    refused("({ __proto__: 1, __proto__: 2 });");

    // Only the plain-data form sets the prototype, so only the plain-data form
    // may not repeat. A shorthand, a method and a computed key are all other
    // properties that happen to be spelled the same.
    accepted("var o = { __proto__: null };");
    accepted("var __proto__ = 1; var o = { __proto__, __proto__: null };");
    accepted("var o = { ['__proto__']: 1, ['__proto__']: 2 };");
    accepted("var o = { __proto__() { return 1; }, __proto__: null };");
    // A duplicate ordinary key is legal: the last one wins, and minifiers rely
    // on it.
    answers("var o = { a: 1, a: 2 }; return o.a;", "2");

    // ================================================================
    // 12. The class body - 15.7.1
    // ================================================================
    refused("class C { constructor() {} constructor() {} }");
    refused("class C { get constructor() { return 1; } }");
    refused("class C { *constructor() {} }");
    refused("class C { constructor = 1; }");
    refused("class C { static prototype() {} }");
    refused("class C { static prototype = 1; }");

    accepted("class C { constructor() { this.x = 1; } m() {} } new C();");
    // Not an early error when the key is computed, but a runtime TypeError:
    // the method is defined (DefineMethodProperty), and a class's own
    // `prototype` is not configurable (test262 static-method-non-configurable-err).
    answers("try { class C { static ['prototype']() { return 1; } } return 'defined'; }"
            " catch (e) { return e.name; }",
            "TypeError");
    accepted("class C { static m() {} get x() { return 1; } }");
    // `constructor` on the static side is an ordinary name, and a computed key
    // is never the constructor.
    accepted("class C { static constructor() { return 1; } }");
    accepted("class C { ['constructor']() { return 1; } }");

    // ================================================================
    // 13. A rest element that is not last - 13.15.5.1
    // ================================================================
    refused("var a, b, c; [a, ...b, c] = [1, 2, 3];");
    refused("function f([a, ...b, c]) {}");

    accepted("var a, b; [a, ...b] = [1, 2, 3];");
    accepted("function f([a, ...b]) { return b; } f([1, 2]);");
    accepted("var xs = [1, 2]; var ys = [0, ...xs, 3];");
    // A spread in a call or an array literal is not a rest element and may
    // appear anywhere; this rule has to keep that distinction.
    accepted("function f() { return arguments.length; } f(1, ...[2, 3], 4);");

    // ================================================================
    // 14. A declaration is not a statement - the grammar, not a clause
    // ================================================================
    // The body of an `if`, a loop or a labelled statement is a Statement, and
    // `let`, `const`, `class`, a generator and an async function are
    // Declarations. `if (true) let x = 1;` does not parse anywhere.
    refused("if (true) let x = 1;");
    refused("if (true) const x = 1;");
    refused("if (true) class C {}");
    refused("if (true) function* g() {}");
    refused("if (true) async function g() {}");
    refused("for (;;) let x = 1;");
    refused("while (0) let x = 1;");
    refused("do let x = 1; while (0);");
    refused("while (0) function f() {}");
    refused("label: class C {}");

    // Annex B is the narrow exception: in sloppy code, B.3.3 admits a plain
    // function declaration as an `if` clause and B.3.2 admits one as a
    // labelled item. A `var` was never a Declaration in this sense.
    accepted("if (true) var x = 1;");
    accepted("if (true) function f() {}");
    accepted("if (false) function f() {} else function g() {}");
    accepted("label: function f() {}");
    accepted("if (true) { let x = 1; }");
    accepted("while (0) { let x = 1; }");
    accepted("for (;;) { let x = 1; break; }");
    // `let` in statement position followed by a newline is an identifier, and
    // a semicolon is inserted after it.
    accepted("if (false) let \nx = 1;");

    // ================================================================
    // 15. The private names of a class body - 15.7.1
    // ================================================================
    refused("class C { #m() {} #m() {} }");
    refused("class C { #x; #x; }");
    refused("class C { get #m() { return 1; } get #m() { return 2; } }");
    refused("class C { #m() {} get #m() { return 1; } }");
    refused("class C { #constructor; }");

    // One getter and one setter of the same name is how a private accessor is
    // written, and it is the one duplicate the specification allows.
    accepted("class C { get #m() { return 1; } set #m(v) {} }");
    accepted("class C { #m() {} #n() {} }");
    accepted("class C { static #m() {} #n() {} }");
    // ...and a static half and an instance half of one name are still a
    // duplicate: the get/set exception needs both to be on the same side.
    refused("class C { static get #m() { return 1; } set #m(v) {} }");

    // A bare loop target is an assignment to a name, even without a declaration.
    refused("class C { #x; m() { for (#x in []) {} } }");
    refused("class C { #x; m() { for (#x of []) {} } }");
    refused("'use strict'; for (eval in {}) {}");
    refused("'use strict'; for (arguments of []) {}");
    refused("'use strict'; for (\\u0065val of []) {}");
    refused("async function f() { for await (#x of []) {} }");
    accepted("for (eval in {}) {} for (arguments of []) {}");
    answers("class C { #x; m() { for (this.#x in { key: 1 }) {} return this.#x; } }"
            " return new C().m();",
            "key");
    answers("class C { #x; m() { for (this.#x of [42]) {} return this.#x; } }"
            " return new C().m();",
            "42");
    answers("class C { #x; m() { for (; #x in this;) { return true; } } }"
            " return new C().m();",
            "true");

    // Arrow/yield expressions need grouping when used as operator operands.
    refused("class C { #x; m() { return #x in () => {}; } }");
    refused("class C { #x; m() { return #x in +() => {}; } }");
    refused("return 'length' in () => {};");
    refused("return 1 + x => x;");
    refused("return void () => {};");
    refused("return false || async () => {};");
    refused("return (() => {} + 1);");
    refused("return (() => {} || 1);");
    refused("function f(x) {} f(() => {} + 1);");
    refused("function* g() { return 1 + yield 2; }");
    answers("class C { #x; m() { return #x in /* ( */ (/* ) */ () => {}); } }"
            " return new C().m();",
            "false");
    answers("return 'length' in (async () => {});", "true");
    answers("return typeof ((() => {} /* ( */) + 1);", "string");
    answers("return typeof (false || (/* ) */ x => x));", "function");
    answers("return typeof (true ? () => {} : async () => {});", "function");
    answers("function* g() { return 1 + /* ( */ (yield 2); } var i = g();"
            " return i.next().value + ':' + i.next(3).value;",
            "2:4");

    // ================================================================
    // 16. `super()` - 15.7.1
    // ================================================================
    // A SuperCall is admitted in exactly one place: the constructor of a class
    // that has a heritage.
    refused("class C { constructor() { super(); } }");
    refused("class C { m() { super(); } }");
    refused("class B {} class C extends B { m() { super(); } }");
    refused("var o = { m() { super(); } };");

    accepted("class B {} class C extends B { constructor() { super(); } } new C();");
    // An arrow inherits the constructor's permission, which is how a derived
    // class defers the call.
    accepted("class B {} class C extends B { constructor() { var f = () => super(); f(); } }");
    accepted("class B {} class C extends B { m() { return super.toString; } }");

    // ================================================================
    // 17. A destructuring assignment is a pattern, not a literal - 13.15.5
    // ================================================================
    // An ArrayLiteral or ObjectLiteral on the left of `=` is reinterpreted as a
    // pattern, and the reinterpretation is not total: every element has to be
    // something a value can be assigned to, or another pattern.
    refused("[[(x, y)]] = [[]];");
    refused("[1] = [];");
    refused("[a?.b] = [];");
    refused("({ x: { get x() {} } } = { x: {} });");
    // A rest element is last and has no default, in a pattern as in a
    // parameter list.
    refused("[...x = 1] = [];");
    refused("var rest, b; ({ ...rest, b } = {});");

    accepted("var a, b; ({ x: a, y: b } = { x: 1, y: 2 });");
    accepted("var a, r; ({ a, ...r } = { a: 1, b: 2 });");
    accepted("var a, o = { p: 0 }; [o.p, a] = [1, 2];");
    accepted("var a, b; [a = 1, b = 2] = [];");
    accepted("var a; [, a] = [1, 2];");
    accepted("var o = {}; ({ a: o.x = 1 } = {});");
    // A spread in a literal is not a rest element and none of this applies to
    // it: `[...a, 1]` and `{ ...b, c: 1 }` are ordinary constructions.
    accepted("var a = [1], b = { p: 1 }; var c = [...a, 2]; var d = { ...b, q: 2 };");

    // A quoted `__proto__` names the same thing a plain one does, and a
    // computed one does not, including when comments separate it from `[`. The
    // parser's quoted-name bit preserves the distinction; escaped names are cooked.
    refused("({ __proto__: 1, '__proto__': 2 });");
    refused("({ __proto__: null, '\\x5f_proto__': null });");
    refused("({ '\\u005f_proto__': null, '__proto__': null });");
    refused("({ '__proto__': null, '\\u{5f}_proto__': null });");
    accepted("var o = { ['__proto__']: 1, __proto__: null };");
    accepted("({ [/* name */ '__proto__']: 1, __proto__: null });");
    accepted("({ [// name\n '__proto__']: 1, '__proto__': null });");
    accepted("({ [/* name */ '\\x5f_proto__']: 1, __proto__: null });");
    accepted("({ ['__proto__']: 1, ['__proto__']: 2, __proto__: null });");

    // ================================================================
    // 18. A numeric literal's own grammar - 12.9.3
    // ================================================================
    // The lexer is total on purpose: it takes `0` plus a radix letter plus
    // every identifier character, and a decimal run of digits, dots and
    // underscores, without asking whether the result is a number. So `0b2` and
    // `1__0` are tokens that parse and are not literals.
    refused("var x = 0b2;");
    refused("var x = 0o8;");
    refused("var x = 0x;");
    refused("var x = 1__0;");
    refused("var x = 1_;");
    refused("var x = 0b_1;");
    refused("var x = 1e_5;");
    refused("var x = 0_1;");
    // A BigInt is an integer, and it has no leading zero.
    refused("var x = 1.5n;");
    refused("var x = 1e1n;");
    refused("var x = 01n;");
    refused("var x = 08n;");

    accepted("var x = 0;");
    accepted("var x = 0.5;");
    accepted("var x = .5;");
    accepted("var x = 1e10 + 1E+10 + 1.5e-10;");
    accepted("var x = 1_000 + 0.000_1;");
    accepted("var x = 0x1F + 0b1010 + 0o17;");
    accepted("var x = 1n + 0n + 1_000n + 0xFFn;");
    accepted("var o = { 0: 1, 0.5: 2, 1e3: 3 };");
    // A legacy octal and a non-octal decimal literal are legal in sloppy code,
    // so both are accepted here; section 19 checks that strict code refuses them.
    accepted("var x = 01;");
    accepted("var x = 08;");

    // ================================================================
    // 19. Strict mode code - 11.2.2, 13.1.1, 13.5.1.1, 12.9.3.1, 15.2.1
    // ================================================================
    // Legal sloppy JavaScript, an early error once a directive, a class body
    // or a module makes the code strict. The sloppy form of each stays
    // accepted, which is what keeps this a deliberate list.
    accepted("var x = 1; delete x;");
    accepted("var eval; eval = 1;");
    accepted("var args = function () { arguments = 1; };");
    accepted("function f(a, a) {} var o = 010; var n8 = 08;");
    refused("'use strict'; var x = 1; delete x;");
    refused("'use strict'; var eval;");
    refused("'use strict'; eval = 1;");
    refused("'use strict'; arguments++;");
    refused("\"use strict\"; function f(a, a) {}");
    refused("'use strict'; var package = 1;");
    refused("'use strict'; var n = 010;");
    refused("'use strict'; var n = 08;");
    refused("'use strict'; try {} catch (eval) {}");
    refused("'use strict'; var f = function eval() {};");
    refused("class C { m() { var eval; } }");
    refused("function g() { 'use strict'; delete g; }");
    // Reading `eval`/`arguments` is fine; a nested function inherits.
    accepted("'use strict'; var v = eval; function f() { return arguments; }");
    accepted("'use strict'; var n = 0x10; var m = 0.5; var big = 0n;");
    refused("'use strict'; (function () { function inner(a, a) {} })();");
    // A directive is only one at the top of the body.
    accepted("var before = 1; 'use strict'; function f(a, a) {}");

    // --- modules (16.2.1): an ImportDeclaration and an ExportDeclaration are
    // ModuleItems, not statements, so they appear nowhere but a module's own
    // top level, and a script's grammar has neither.
    refused("export var x = 1;");
    refused("import x from './m.js';");
    module_refused("{ export default null; }");
    module_refused("() => { import v from './m.js'; };");
    module_refused("function f() { export var x; }");
    module_refused("if (true) export default null;");
    module_accepted("export var x = 1; export default 42;");
    module_accepted("import x from './m.js'; export { x as y };");
    // An exported name may not be exported twice, and an exported binding
    // must be one this module declares, unless it came `from` another.
    module_refused("var x; export { x }; export { x };");
    module_refused("var x, y; export default x; export { y as default };");
    module_refused("export function f() {} export class f {}");
    module_refused("export { Number };");
    module_accepted("var x; export { x }; export { x as other };");
    module_accepted("export { Number } from './m.js';");
    module_accepted("let a = 1; function b() {} export { a, b };");
    // `import defer` (16.2.2) is deferral only before `*`; `defer` is an
    // ordinary identifier otherwise, and a deferred import is namespace-only.
    module_accepted("import defer * as ns from './m.js'; export { ns };");
    module_accepted("import defer from './m.js'; export { defer };");
    module_refused("import defer * as ns, { x } from './m.js';");
    // A module's top-level function declarations are lexical, so two of a
    // name are a redeclaration where a script allows them.
    accepted("function x() {} function x() {}");
    module_refused("function x() {} function x() {}");
    module_refused("var x; function x() {}");
    module_accepted("function x() {} function y() {}");
    // An import binds strictly and lexically; so does a named default export.
    module_refused("import { eval } from './m.js';");
    module_refused("import { x as arguments } from './m.js';");
    module_refused("import { a } from './m.js'; let a;");
    module_refused("class F {} export default function F() {}");
    module_refused("var G; export default function* G() {}");
    module_accepted("export default class C {} export { C as D };");
    // ...but a default expression binds nothing, so its name is no duplicate.
    module_accepted("const y = 42; export default y; export { y as z };");
    module_accepted("export default function F() {} export { F as G };");
    // A string ModuleExportName must be well-formed Unicode (16.2.2.1).
    module_refused("var x; export { x as '\\uD83D' };");
    module_refused("import { '\\uDC00' as y } from './m.js';");
    module_refused("export * as \"\\u{D83D}\" from './m.js';");
    module_accepted("var x; export { x as '\\uD83D\\uDE00' };");

    // 14.12.1: one `default` per switch.
    refused("switch (1) { case 0: default: break; default: }");
    accepted("switch (1) { case 0: default: break; case 1: }");
    // 14.14: no line break between `throw` and its expression - and a
    // parenthesis that opens on the throw's line is not one.
    refused("try { throw\n1; } catch (e) {}");
    accepted("try { throw 1; } catch (e) {}");
    accepted("try { throw (\n1); } catch (e) {}");
    accepted("var thrower; try { thrower\n1; } catch (e) {}");
    // A "use strict" body makes a declaration's own name strict code.
    refused("function arguments() { 'use strict'; }");
    refused("function eval() { 'use strict'; }");
    accepted("function evaluate() { 'use strict'; }");
    // Tokens the parser took where only a name may stand.
    refused("try {} catch (\"22\") {}");
    accepted("try {} catch (e) {} try {} catch ({message}) {} try {} catch {}");
    refused("var o = {}; o.\"x\";");
    accepted("var o = {a: {}}; o.a.b; o?.a; o.default; o.$x; o._y;");
    refused("catch");
    refused("finally");
    accepted("debugger;");

    REPORT("early_errors");
}
