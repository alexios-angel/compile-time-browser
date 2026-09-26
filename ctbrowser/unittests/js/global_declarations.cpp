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
    REPORT("global_declarations");
}
