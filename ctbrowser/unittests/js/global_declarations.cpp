//===- ctbrowser/unittests/js/global_declarations.cpp - Global scope ------===//
//
// GlobalDeclarationInstantiation (ECMA-262 16.1.7): the scripts of one realm
// share a global environment, and a script whose declarations collide with
// what an earlier one made is a SyntaxError that creates nothing, checked
// before its first statement runs (context::global_declaration_instantiation).
// A top-level `let`, `const` or `class` is a binding of that environment and
// not a property of the global object. The last case checks that compiled
// (AOT) code stores globals the same way the interpreter does.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/script.hpp>

#include "check.hpp"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

namespace {

using namespace ctbrowser::script;

/// Runs each script in turn in one context and answers what the last one did:
/// "ok", the error it failed with, or "COMPILE <error>".
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

/// The message fragment of a redeclaration SyntaxError.
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
    // A refused script creates nothing: its `var` stays unbound.
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
    // that property, attributes and all (9.1.1.4.5).
    expect({"Object.defineProperty(globalThis, 'd',"
            " {value: 'x', writable: true, enumerable: false, configurable: true});",
            "d = 2; var o = Object.getOwnPropertyDescriptor(globalThis, 'd');"
            "if (o.value !== 2 || o.enumerable || d !== 2) { throw 0; }"},
           "ok");
    // An Annex B block function's name is not a VarName (B.3.2.2): a global
    // `let` of it, or a global object that cannot take it, skips it, with no
    // SyntaxError, no TypeError, and no binding.
    expect({"let f = 1;", "if (false) { function f() {} } if (f !== 1) { throw 0; }"}, "ok");
    expect({"Object.preventExtensions(globalThis);", "if (false) { function g() {} }"}, "ok");
    expect({"Object.preventExtensions(globalThis);", "if (false) { function g() {} }", "g;"},
           "ReferenceError");
    // ...but a `var` of the name beside it is a var, and collides as one.
    expect({"let f = 1;", "var f; { function f() {} }"}, refused);
    // A block function a script did declare is a var name a later `let` meets.
    expect({"{ function t() {} }", "let t;"}, refused);
    // A Script (compile_script) refuses a top-level `return` and `new.target`;
    // the embedder's program (compile) keeps its `return`.
    check(!compiler::compile_script("return 1;").ok, "a Script's top-level return is refused");
    check(!compiler::compile_script("new.target;").ok,
          "a Script's top-level new.target is refused");
    check(compiler::compile_script("function f() { return new.target; }").ok,
          "a Script's function may return new.target");
    check(compiler::compile("return 1;").ok, "an embedder's program may return");

    // ct_aot_global_set stores as op::set_global does (context::assign_global):
    // into a data property the global object owns, not over a read-only one,
    // and into the table otherwise.
    {
        context cx;
        install_builtins(cx);
        const program & setup = cx.own_program(compiler::compile(
            "Object.defineProperty(globalThis, 'd', {value: 1, writable: true, configurable: "
            "true});"
            "Object.defineProperty(globalThis, 'r', {value: 1, writable: false, configurable: "
            "true});"));
        check(cx.run(setup).ok, "the global properties are defined");
        alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
        ctbrowser::aot::ct_aot_frame * frame = ctbrowser::aot::ct_aot_enter(
            reinterpret_cast<ctbrowser::aot::ct_aot_ctx *>(&cx),
            reinterpret_cast<const ctbrowser::aot::ct_aot_site *>(&setup.functions[0]), 1u,
            value::undefined().bits(), storage);
        check(frame != nullptr, "a frame is entered");
        if (frame != nullptr) {
            ctbrowser::aot::ct_aot_global_set(frame, "d", 1u, value::number(2).bits());
            ctbrowser::aot::ct_aot_global_set(frame, "r", 1u, value::number(2).bits());
            ctbrowser::aot::ct_aot_global_set(frame, "n", 1u, value::number(3).bits());
            ctbrowser::aot::ct_aot_leave(frame);
        }
        const program & read = cx.own_program(compiler::compile(
            "return Object.getOwnPropertyDescriptor(globalThis, 'd').value + ',' + d + ',' + r +"
            " ',' + n;"));
        const run_result ran = cx.run(read);
        check(ran.ok && cx.to_string(ran.returned) == "2,2,1,3",
              "a compiled global store lands where the interpreter's does");
    }
    REPORT("global_declarations");
}
