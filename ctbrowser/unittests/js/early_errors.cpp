// The early errors: source text this engine must REFUSE, and the valid
// near-miss beside each one that it must still run.
//
// `negative parse/SyntaxError: got runtime` was the single largest failure
// cause in test262 - 2,812 tests asserting that source text is a SyntaxError
// while this engine parsed it, ran it, and reported whatever happened next.
// lib/Script/compile/early_errors.cpp is the pass that answers them and its
// header says which families are deliberately absent.
//
// EVERY REFUSAL HERE IS PAIRED. The `refused` line proves the rule fires; the
// `accepted` line beside it proves the rule stops where it should, and it is
// the one that matters more: a false positive is not a wrong answer, it is a
// page that does not load. This engine compiles p5.js, Phaser, Babylon.js and
// Bootstrap as ratchet tests, and all four were run through this pass before it
// was committed - the accepted cases below are the shapes those bundles
// contain, written small enough to read.
//
// A REFUSAL MUST ALSO BE A SYNTAX ERROR AND NOT A REFUSAL. The engine has two
// ways to fail a compile: the source is not a program (a SyntaxError), or the
// compiler does not implement the construct. Only the first is what a
// specification early error is, and `ct262` tells them apart by the message's
// prefix - so `refused` checks the prefix rather than just `ok == false`.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"

#include <string>
#include <string_view>

namespace {

// Source that must not compile, with a SyntaxError rather than a refusal.
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

// Source that must compile AND run. Running matters: half the near-misses below
// are about scope, and a scope rule can be got wrong in a way that compiles to
// the wrong register rather than to no program at all.
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

// Source that must compile, run, and answer `expected` - for the cases where
// "it ran" is not enough and the SCOPE it ran in is the point.
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
    // ================================================================
    // 1. DUPLICATE LEXICAL DECLARATIONS - 14.2.1, 15.2.1, 16.1.1
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
    // A LOOP VARIABLE AND A BODY BINDING OF THE SAME NAME are two scopes, not
    // a redeclaration - this is the shape every bundle in vendor/ contains.
    accepted("for (let i = 0; i < 1; ++i) { let i = 9; }");
    accepted("for (const item of [1, 2]) { let item2 = item; }");

    // A `var` and a function of the same name at the TOP of a function body is
    // legal - both are var-scoped there - and so is a function declaration in a
    // block that shares its name with an outer binding.
    accepted("function h() { var k; function k() {} }");
    accepted("let m; { function m() {} }");
    // Annex B.3.3.2: a function declaration as the whole body of an `if` is
    // hoisted only where hoisting would not be an early error, so it may not
    // MAKE one. Five of test262's annexB tests are exactly this.
    accepted("let n = 1; if (true) function n() {}");

    // ================================================================
    // 2. A PARAMETER, AND THE BODY THAT MAY NOT REDECLARE IT - 15.2.1
    // ================================================================
    refused("function f(a) { let a; }");
    refused("function f(a) { const a = 1; }");
    refused("function f(a) { class a {} }");
    refused("(a) => { let a; };");
    // 14.15.1 says the same of a catch parameter and its block.
    refused("try {} catch (e) { let e; }");

    // `var` re-declaring a parameter is legal and common in transpiled code,
    // and so is a lexical binding in a NESTED block of the body.
    accepted("function f(a) { var a; return a; }");
    accepted("function f(a) { { let a = 1; return a; } }");
    accepted("try {} catch (e) { var e; }");
    accepted("try { null.x; } catch (e) { let inner = e; }");
    answers("function f(a) { { let a = 2; } return a; } return f(1);", "1");

    // ================================================================
    // 3. DUPLICATE PARAMETER NAMES - 15.1.2, 15.2.1, 15.3.1
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

    // A DUPLICATE IN A SIMPLE LIST IS SLOPPY-LEGAL, and this engine has no
    // strict mode - so it is accepted, deliberately. Refusing it would refuse
    // valid sloppy JavaScript.
    accepted("function f(a, a) { return a; }");
    accepted("function f(a, b = 1, ...rest) { return a; }");
    accepted("var f = (a, b) => a + b;");
    accepted("var o = { m(a, b) { return a; } };");

    // ================================================================
    // 4. `const` WITHOUT AN INITIALISER - 14.3.1.1
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
    // 5. AN ASSIGNMENT TARGET THAT IS NOT ONE - 13.15.1
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
    // A CALL is refused here, and the note in early_errors.cpp says why it is
    // the second-best answer: a browser may make `f() = 1` a runtime
    // ReferenceError instead, and this engine's compiler refuses a call target
    // outright either way - so the only thing being chosen is which KIND of
    // failure it is.
    refused("function f() { return {}; } f() = 1;");
    refused("f() += 1;");
    refused("f()++;");

    // ================================================================
    // 6. `break`, `continue` AND LABELS - 13.8.1, 13.9.1, 14.13.1
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
    accepted("outer: for (;;) { continue outer; }");
    // A label on a labelled loop still names a loop, which is what makes this
    // legal - the label set is unwrapped rather than tested one deep.
    accepted("a: b: for (;;) { continue a; }");
    accepted("block: { break block; }");
    // Two labels of one name in SEQUENCE are fine; only nesting is an error.
    accepted("a: ; a: ;");
    accepted("function f() { L: for (;;) { break L; } } f();");

    // ================================================================
    // 7. `new.target` AND `super` OUTSIDE ANYTHING THAT HAS ONE
    // ================================================================
    refused("new.target;");
    refused("var f = () => new.target;");
    refused("super.x;");
    refused("super();");
    refused("function f() { super.x; }");

    accepted("function f() { return new.target; } f();");
    accepted("function f() { var g = () => new.target; return g(); } f();");
    accepted("class C { m() { return super.toString; } }");
    accepted("class C { constructor() { this.x = 1; } } new C();");
    accepted("var o = { m() { return super.toString; } };");
    // `return` at the top level is NOT refused: it is this engine's embedding
    // contract, the way a compiled program hands a value back. See the note in
    // early_errors.cpp.
    answers("return 42;", "42");

    // ================================================================
    // 8. `delete` OF A PRIVATE MEMBER - 13.5.1.1
    // ================================================================
    // The one `delete` rule that does not need strict mode, and 192 test262
    // files. `delete x` on a plain identifier IS strict-only and is accepted
    // here, deliberately.
    refused("class C { #x; m() { delete this.#x; } }");
    refused("class C { #x; m() { delete (this.#x); } }");

    accepted("var o = { p: 1 }; delete o.p;");
    accepted("var o = { p: 1 }; delete o['p'];");
    accepted("delete undeclared;");
    accepted("class C { #x = 1; m() { return this.#x; } } new C().m();");

    // ================================================================
    // 9. TWO `__proto__` IN ONE OBJECT LITERAL - B.3.1
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
    // A duplicate ORDINARY key is legal and always has been - the last one
    // wins, and minifiers rely on it.
    answers("var o = { a: 1, a: 2 }; return o.a;", "2");

    // ================================================================
    // 10. THE CLASS BODY - 15.7.1
    // ================================================================
    refused("class C { constructor() {} constructor() {} }");
    refused("class C { get constructor() { return 1; } }");
    refused("class C { *constructor() {} }");
    refused("class C { constructor = 1; }");
    refused("class C { static prototype() {} }");
    refused("class C { static prototype = 1; }");

    accepted("class C { constructor() { this.x = 1; } m() {} } new C();");
    accepted("class C { static ['prototype']() { return 1; } }");
    accepted("class C { static m() {} get x() { return 1; } }");
    // `constructor` on the STATIC side is an ordinary name, and a computed key
    // is never the constructor.
    accepted("class C { static constructor() { return 1; } }");
    accepted("class C { ['constructor']() { return 1; } }");

    // ================================================================
    // 11. A REST ELEMENT THAT IS NOT LAST - 13.15.5.1
    // ================================================================
    refused("var a, b, c; [a, ...b, c] = [1, 2, 3];");
    refused("function f([a, ...b, c]) {}");

    accepted("var a, b; [a, ...b] = [1, 2, 3];");
    accepted("function f([a, ...b]) { return b; } f([1, 2]);");
    accepted("var xs = [1, 2]; var ys = [0, ...xs, 3];");
    // A SPREAD IN A CALL OR AN ARRAY LITERAL IS NOT A REST ELEMENT and may
    // appear anywhere - which is the distinction this rule has to keep.
    accepted("function f() { return arguments.length; } f(1, ...[2, 3], 4);");

    // ================================================================
    // 12. A DECLARATION IS NOT A STATEMENT - the grammar, not a clause
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

    // Annex B is the exception and it is narrow: B.3.3 admits a plain function
    // declaration as an `if` clause and B.3.2 as a labelled item, in sloppy
    // code - which is all the code there is here. A `var` was never a
    // Declaration in this sense.
    accepted("if (true) var x = 1;");
    accepted("if (true) function f() {}");
    accepted("if (false) function f() {} else function g() {}");
    accepted("label: function f() {}");
    accepted("if (true) { let x = 1; }");
    accepted("while (0) { let x = 1; }");
    accepted("for (;;) { let x = 1; break; }");
    // `let` IN STATEMENT POSITION FOLLOWED BY A NEWLINE IS AN IDENTIFIER, and
    // a semicolon is inserted after it. Twelve test262 files are this shape.
    accepted("if (false) let \nx = 1;");

    // ================================================================
    // 13. THE PRIVATE NAMES OF A CLASS BODY - 15.7.1
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
    // ...and a static half and an instance half of ONE name are still a
    // duplicate: the get/set exception needs both to be on the same side.
    refused("class C { static get #m() { return 1; } set #m(v) {} }");

    // ================================================================
    // 14. `super()` - 15.7.1
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
    // 15. A DESTRUCTURING ASSIGNMENT IS A PATTERN, NOT A LITERAL - 13.15.5
    // ================================================================
    // An ArrayLiteral or ObjectLiteral on the left of `=` is REINTERPRETED as a
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
    // A SPREAD IN A LITERAL IS NOT A REST ELEMENT and none of this applies to
    // it - `[...a, 1]` and `{ ...b, c: 1 }` are ordinary constructions.
    accepted("var a = [1], b = { p: 1 }; var c = [...a, 2]; var d = { ...b, q: 2 };");

    // A QUOTED `__proto__` NAMES THE SAME THING a plain one does, and a
    // COMPUTED one does not - the difference survives only in the source, since
    // the parser gives both the same shape.
    refused("({ __proto__: 1, '__proto__': 2 });");
    accepted("var o = { ['__proto__']: 1, __proto__: null };");

    // ================================================================
    // 16. A NUMERIC LITERAL'S OWN GRAMMAR - 12.9.3
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
    // A BigInt is an INTEGER, and it has no leading zero.
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
    // A LEGACY OCTAL AND A NON-OCTAL DECIMAL ARE LEGAL SLOPPY JavaScript, and
    // this engine has no strict mode - so both are accepted, deliberately.
    accepted("var x = 01;");
    accepted("var x = 08;");

    // ================================================================
    // 17. A RESERVED WORD IS NOT AN IDENTIFIER - 13.1.1
    // ================================================================
    // The parser is deliberately lenient about a keyword in expression
    // position, and it HAS to be: `of`, `get`, `set`, `static`, `async`, `let`,
    // `await` and `yield` are contextual, so `const of = 1` and
    // `function set(x)` are valid JavaScript and p5.js has both. What that
    // leniency also accepts is `typeof import`, which is not.
    refused("var f = () => typeof import;");
    refused("var x = [default] = [];");

    // Every contextual keyword, still a name.
    accepted("var of = 1; var get = 2; var set = 3; var async = 4; var yield = 5; var let2 = 6;");
    accepted("function set(x) { return x; } function get() { return 1; } get();");
    // A reserved word is still a PROPERTY name, in a literal and on a member -
    // that is a different production and always was legal.
    accepted("var o = { if: 1, class: 2, default: 3 }; o.if + o.class + o.default;");
    accepted("var o = {}; o.default = 1; o['class'] = 2;");
    accepted("var o = { get x() { return 1; }, set x(v) {} }; o.x;");
    // `with` IS ABSENT FROM THE LIST, and not by oversight: this parser has no
    // `with` statement, so `with (o) {}` arrives as a CALL of something named
    // `with` - and refusing that would refuse every `with` in the corpus and
    // every `import ... with {}` attribute clause.

    // ================================================================
    // 18. WHAT IS STILL ACCEPTED, ON PURPOSE
    // ================================================================
    // Each of these is an early error in STRICT mode and legal sloppy
    // JavaScript, and this engine has no strict mode. They are here so that
    // adding one is a deliberate change to this file rather than a surprise.
    accepted("var x = 1; delete x;");
    accepted("var eval; eval = 1;");
    accepted("var args = function () { arguments = 1; };");

    REPORT("early_errors");
}
