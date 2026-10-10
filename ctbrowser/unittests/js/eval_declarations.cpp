//===- ctbrowser/unittests/js/eval_declarations.cpp - Eval declarations ---===//
//
// EvalDeclarationInstantiation (ECMA-262 19.2.1.3) for the global variable
// environment: an indirect eval's, and a direct one's from global code. A
// `var` or function of the eval colliding with a global `let` is a
// SyntaxError, and one the global object cannot take is a TypeError, both
// raised before any binding exists. A function is made with
// CreateGlobalFunctionBinding(fn, fo, true): writable, enumerable and
// configurable, over a configurable property the global object had. Also
// checks that a class's `prototype` is { writable: false, enumerable: false,
// configurable: false } (15.7.14, MakeConstructor(F, false, proto)).
// "Step" numbers below refer to 19.2.1.3.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/script/script.hpp>

#include "check.hpp"

#include <initializer_list>
#include <string>
#include <string_view>

namespace {

using namespace ctbrowser::script;

/// Runs each script in turn in one context; answers "ok", the last script's
/// error, or "COMPILE <error>".
std::string last_of(std::initializer_list<std::string_view> scripts) {
    context cx;
    install_builtins(cx);
    std::string answer = "ok";
    for (const std::string_view source : scripts) {
        const program & compiled = cx.own_program(compiler::compile(source));
        if (!compiled.ok) { return "COMPILE " + compiled.error; }
        const run_result ran = cx.run(compiled);
        answer = ran.ok ? std::string{"ok"} : ran.error;
    }
    return answer;
}

/// Checks that last_of(scripts) contains `want`.
void expect(std::initializer_list<std::string_view> scripts, std::string_view want) {
    const std::string got = last_of(scripts);
    if (got.find(want) == std::string::npos) {
        std::string listed;
        for (const std::string_view s : scripts) { listed += "[" + std::string{s} + "] "; }
        std::printf("FAIL     %s\n  -> %s (want %s)\n", listed.c_str(), got.c_str(),
                    std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

} // namespace

int main() {
    // Step 3.a, direct and indirect.
    expect({"let x;", "eval('var x;');"}, "SyntaxError");
    expect({"let x;", "(0, eval)('var x;');"}, "SyntaxError");
    expect({"let x;", "(0, eval)('function x() {}');"}, "SyntaxError");
    // An Annex B block function is not a var name (B.3.2.3): a global `let`
    // of it skips it, with no error and no binding, and so does a global
    // object that cannot take it.
    expect({"let f = 1;", "(0, eval)('if (false) { function f() {} }');"
                          "if (f !== 1) { throw 0; }"},
           "ok");
    expect({"let f = 1;", "eval('{ function f() {} }');"}, "ok");
    expect({"Object.preventExtensions(globalThis);", "(0, eval)('{ function g() {} }');"}, "ok");
    expect({"let f;", "(0, eval)('var f; { function f() {} }');"}, "SyntaxError");
    // ...but not from a function, whose own variables they are.
    expect({"let x;", "(function () { eval('var x;'); })();"}, "ok");
    // An eval's bindings are configurable, so a later script's `let` shadows
    // them rather than colliding (16.1.7 step 3.c's NOTE).
    expect({"eval('var ev; function ef() {}');", "let ev = 1; const ef = 2;"
                                                 "if (ev !== 1 || ef !== 2) { throw 0; }"},
           "ok");
    // Steps 8-10: NaN is not configurable and not writable.
    expect({"eval('function NaN() {}');"}, "TypeError");
    expect({"try { eval('var fresh; function NaN() {}'); } catch (e) {}"
            "if (Object.getOwnPropertyDescriptor(globalThis, 'fresh')) { throw 0; }"},
           "ok");
    expect({"Object.preventExtensions(globalThis);", "(0, eval)('var fresh;');"}, "TypeError");
    // CreateGlobalFunctionBinding(fn, fo, true) over a configurable property.
    expect({"Object.defineProperty(globalThis, 'f',"
            " {value: 0, writable: false, enumerable: false, configurable: true});"
            "eval('function f() { return 1; }');"
            "var d = Object.getOwnPropertyDescriptor(globalThis, 'f');"
            "if (!d.writable || !d.enumerable || !d.configurable || f() !== 1) { throw 0; }"},
           "ok");
    // A class's prototype.
    expect({"class C {} var d = Object.getOwnPropertyDescriptor(C, 'prototype');"
            "if (d.writable || d.enumerable || d.configurable) { throw 0; }"
            "function F() {} if (!Object.getOwnPropertyDescriptor(F, 'prototype').writable) {"
            " throw 1; }"},
           "ok");
    REPORT("eval_declarations");
}
