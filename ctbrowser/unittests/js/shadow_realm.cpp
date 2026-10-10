// SHADOWREALM: a realm with intrinsics of its own (builtins'
// create_intrinsic_realm), and the callable boundary between it and its
// creator - primitives cross as they are, functions cross wrapped, and any
// other object, or anything thrown, is a TypeError of the side that asked.

#include "vm/expect.hpp"

namespace {

void test_evaluate() {
    expect_result("var r = new ShadowRealm(); return r.evaluate('1 + 1');", "2");
    expect_result("var r = new ShadowRealm(); r.evaluate('var x = \"in\"');"
                  "var x = 'out'; return r.evaluate('x') + '|' + x;",
                  "in|out");
    // Its own intrinsics: rewriting the shadow's Array.prototype leaves ours.
    expect_result("var r = new ShadowRealm();"
                  "r.evaluate('Array.prototype.push = function () { return \"shadow\"; }');"
                  "return [r.evaluate('[].push()'), [].push(1)].join();",
                  "shadow,1");
    expect_result("var r = new ShadowRealm();"
                  "return r.evaluate('Object.getPrototypeOf(globalThis) === Object.prototype');",
                  "true");
    // A class's fields and a computed key call natives the compiler's code
    // needs in every realm (context::create_empty_realm).
    expect_result("var r = new ShadowRealm();"
                  "return r.evaluate('class C { x = 1; [\"k\" + 2]() { return 3; } }"
                  " new C().x + new C().k2()');",
                  "4");
    // Each evaluation has its own lexical scope, as an indirect eval does.
    expect_result("var r = new ShadowRealm(); r.evaluate('let a = 1;');"
                  "return r.evaluate('typeof a');",
                  "undefined");
    // Symbols cross, and the registry is the agent's.
    expect_result("var r = new ShadowRealm(); var s = r.evaluate('Symbol.for(\"k\")');"
                  "return [s === Symbol.for('k'), Symbol.keyFor(s)].join();",
                  "true,k");
}

void test_boundary() {
    expect_result("var r = new ShadowRealm(); try { r.evaluate('({})'); return 'no'; }"
                  " catch (e) { return e instanceof TypeError; }",
                  "true");
    expect_result("var r = new ShadowRealm(); try { r.evaluate('throw 1'); return 'no'; }"
                  " catch (e) { return e.constructor === TypeError; }",
                  "true");
    expect_result("var r = new ShadowRealm(); try { r.evaluate('...'); return 'no'; }"
                  " catch (e) { return e.constructor === SyntaxError; }",
                  "true");
    expect_result("try { ShadowRealm.prototype.evaluate.call({}, '1'); return 'no'; }"
                  " catch (e) { return e.constructor === TypeError; }",
                  "true");
}

void test_wrapped_functions() {
    // A wrapped function: ours, its length and name copied, arguments and
    // results carried over the boundary both ways.
    expect_result("var r = new ShadowRealm(); var f = r.evaluate('(function add(a, b) {"
                  " return a + b; })');"
                  "return [typeof f, f.length, f.name, f(2, 3),"
                  " Object.getPrototypeOf(f) === Function.prototype].join();",
                  "function,2,add,5,true");
    expect_result("var r = new ShadowRealm(); var twice = r.evaluate('(g) => g() + g()');"
                  "return twice(function () { return 'x'; });",
                  "xx");
    expect_result("var r = new ShadowRealm(); var f = r.evaluate('(o) => o');"
                  "try { f({}); return 'no'; } catch (e) { return e.constructor === TypeError; }",
                  "true");
    expect_result("var r = new ShadowRealm(); var f = r.evaluate('() => { throw new Error; }');"
                  "try { f(); return 'no'; } catch (e) { return e.constructor === TypeError; }",
                  "true");
    // A new wrapper per crossing, with no properties of the target's.
    expect_result("var r = new ShadowRealm(); r.evaluate('function fn() {} fn.x = 1;');"
                  "var a = r.evaluate('fn'), b = r.evaluate('fn');"
                  "return [a === b, a.x].join();",
                  "false,");
    expect_result("var r = new ShadowRealm();"
                  "var f = r.evaluate('var p = Proxy.revocable(() => {}, {}); p.proxy');"
                  "r.evaluate('p.revoke()');"
                  "try { f(); return 'no'; } catch (e) { return e.constructor === TypeError; }",
                  "true");
}

// The shadow realm's intrinsics are roots: a collection between two
// evaluations keeps them, and its globals.
void test_survives_collection() {
    context cx;
    install_builtins(cx);
    const auto run = [&](std::string_view source) {
        const run_result r = cx.run(cx.own_program(compiler::compile(std::string{source})));
        return r.ok ? cx.to_string(cx.global("result")) : "<error: " + r.error + ">";
    };
    (void)run("var r = new ShadowRealm(); r.evaluate('var kept = [1, 2, 3].map(x => x * 2)');"
              " var result = '';");
    (void)cx.collect();
    const std::string got = run("var result = r.evaluate('kept.join() + [].concat(1).length');");
    if (got != "2,4,61") {
        std::printf("FAIL     shadow realm after a collection => %s\n", got.c_str());
        ++ctbrowser_test_failures;
    }
}

} // namespace

int main() {
    test_evaluate();
    test_boundary();
    test_wrapped_functions();
    test_survives_collection();
    REPORT("shadow_realm");
}
