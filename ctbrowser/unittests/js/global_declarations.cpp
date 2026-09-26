// GLOBAL DECLARATION INSTANTIATION (16.1.7): the scripts of one realm share
// a global environment, and a script whose declarations collide with what an
// earlier one made is a SyntaxError that creates nothing - checked before
// its first statement runs (context::global_declaration_instantiation). A
// top-level `let`, `const` or `class` is a binding of that environment and
// not a property of the global object.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"

#include <initializer_list>
#include <string>
#include <string_view>

namespace {

using namespace ctbrowser::script;

// Runs each script in turn in one context; answers what the LAST one did:
// "ok", or the error it failed with.
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

constexpr std::string_view refused = "has already been declared";

} // namespace

int main() {
    // A lexical name meets a lexical, a var, a function, a restricted global.
    expect({"let a;", "let a;"}, refused);
    expect({"const a = 1;", "class a {}"}, refused);
    expect({"var a;", "let a;"}, refused);
    expect({"function a() {}", "let a;"}, refused);
    expect({"let undefined;"}, refused);
    expect({"let a;", "var a;"}, refused);
    expect({"class a {}", "function a() {}"}, refused);
    // ...and the near misses that must still run.
    expect({"var a;", "var a;"}, "ok");
    expect({"function a() {}", "var a;"}, "ok");
    expect({"let a;", "let b; { let a; }"}, "ok");
    expect({"let a;", "(function () { var a; })();"}, "ok");
    // A REFUSED SCRIPT CREATES NOTHING: its `var` stays unbound.
    expect({"let a;", "var b; let a;", "b;"}, "ReferenceError");
    // A binding everywhere, and not a property of the global object.
    expect({"let a = 1;", "if (this.hasOwnProperty('a') || a !== 1) { throw 0; }"}, "ok");
    // A script's function is { writable, enumerable, !configurable } (9.1.1.4.18
    // CreateGlobalFunctionBinding with D false), so `delete` refuses it.
    expect({"function f() {}", "var d = Object.getOwnPropertyDescriptor(globalThis, 'f');"
                               "if (d.configurable || !d.enumerable || delete globalThis.f) {"
                               " throw 0; }"},
           "ok");
    // CanDeclareGlobalVar / CanDeclareGlobalFunction: a TypeError, and nothing
    // is created.
    expect({"Object.preventExtensions(globalThis);", "var nope;"}, "TypeError");
    expect({"Object.preventExtensions(globalThis);", "function nope() {}"}, "TypeError");
    expect({"Object.defineProperty(globalThis, 'd',"
            " {value: 0, writable: true, enumerable: false, configurable: false});",
            "var x; function d() {}", "x;"},
           "ReferenceError");
    expect({"Object.defineProperty(globalThis, 'd',"
            " {value: 0, writable: true, enumerable: true, configurable: false});",
            "function d() {}", "if (typeof d !== 'function') { throw 0; }"},
           "ok");
    // for-in over the global object names the bindings the table holds: an
    // eval's `var`, a plain assignment's global.
    expect({"eval('var ev = 1;'); implicit = 2; var seen = '';"
            "for (var k in globalThis) { if (k === 'ev' || k === 'implicit') { seen += k; } }"
            "if (seen !== 'evimplicit' && seen !== 'implicitev') { throw seen; }"},
           "ok");
    // A global assignment to a data property the global object owns writes
    // THAT property, attributes and all (9.1.1.4.5).
    expect({"Object.defineProperty(globalThis, 'd',"
            " {value: 'x', writable: true, enumerable: false, configurable: true});",
            "d = 2; var o = Object.getOwnPropertyDescriptor(globalThis, 'd');"
            "if (o.value !== 2 || o.enumerable || d !== 2) { throw 0; }"},
           "ok");
    // A Script (compile_script) refuses a top-level `return` and `new.target`;
    // the embedder's program (compile) keeps its `return`.
    check(!compiler::compile_script("return 1;").ok, "a Script's top-level return is refused");
    check(!compiler::compile_script("new.target;").ok,
          "a Script's top-level new.target is refused");
    check(compiler::compile_script("function f() { return new.target; }").ok,
          "a Script's function may return new.target");
    check(compiler::compile("return 1;").ok, "an embedder's program may return");
    REPORT("global_declarations");
}
