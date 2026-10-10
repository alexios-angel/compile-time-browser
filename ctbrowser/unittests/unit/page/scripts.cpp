//===- ctbrowser/unittests/unit/page/scripts.cpp - Page scripts compile ---===//
//
// Checks that the scripts of the real example pages compile. Unit tests check
// features one at a time; this checks the language against pages nobody wrote
// for this engine. `pong.html` is the MDN breakout tutorial verbatim, and
// `fetchboard.html` uses fetch, await, try/catch, template literals and for..of
// in one page.
//
// A construct the compiler refuses is asserted to be refused by name, which is
// the difference between a deferred feature and a mis-compile.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

/// Returns every <script> body in the page at \p path, concatenated, since all
/// of a page's scripts share one global scope. Empty if the file cannot be read.
[[nodiscard]] std::string page_script(const std::string & path) {
    std::ifstream in{path};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string html = buffer.str();

    std::string js;
    std::size_t at = 0;
    while ((at = html.find("<script", at)) != std::string::npos) {
        const std::size_t open = html.find('>', at);
        const std::size_t close = html.find("</script", open);
        if (open == std::string::npos || close == std::string::npos) { break; }
        js += html.substr(open + 1, close - open - 1);
        js += '\n';
        at = close;
    }
    return js;
}

/// Fails the test unless the scripts of the page at \p path compile.
void must_compile(const std::string & path) {
    const std::string js = page_script(path);
    if (js.empty()) {
        std::printf("FAIL %s: no script found\n", path.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    const auto program = ctbrowser::script::compiler::compile(js);
    if (!program.ok) {
        std::printf("FAIL %s (%zu bytes of JS): %s\n", path.c_str(), js.size(),
                    program.error.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    std::printf("     %s: %zu bytes of JS, compiles\n", path.c_str(), js.size());
}

/// Fails the test unless compiling \p source fails with an error containing
/// \p blocker. The blocker is asserted, not just printed: if it changes, either
/// something regressed or a deferral was lifted, and both are worth knowing.
void must_stop_at_source(const std::string & source, std::string_view blocker) {
    const ctbrowser::script::program compiled = ctbrowser::script::compiler::compile(source);
    if (compiled.ok) {
        std::printf("FAIL `%s` COMPILES - promote it\n", source.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    if (compiled.error.find(blocker) == std::string::npos) {
        std::printf("FAIL `%s` stops at \"%s\", not at \"%s\"\n", source.c_str(),
                    compiled.error.c_str(), std::string{blocker}.c_str());
        ++ctbrowser_test_failures;
    }
}

/// The other side of must_stop_at_source: fails the test unless \p source
/// compiles. A lifted deferral moves its case from that function to this one.
void must_compile_source(const std::string & source) {
    const ctbrowser::script::program compiled = ctbrowser::script::compiler::compile(source);
    if (!compiled.ok) {
        std::printf("FAIL `%s`: %s\n", source.c_str(), compiled.error.c_str());
        ++ctbrowser_test_failures;
    }
}

/// Returns head, then `before<i>after` for i in [0, times), then tail. Builds a
/// program large enough to cross a compiler limit without writing it out.
[[nodiscard]] std::string repeated(std::string_view head, std::string_view before,
                                   std::string_view after, int times, std::string_view tail) {
    std::string out{head};
    for (int i = 0; i < times; ++i) {
        out += std::string{before} + std::to_string(i) + std::string{after};
    }
    return out + std::string{tail};
}

} // namespace

int main() {
    // The MDN breakout tutorial, verbatim: nothing in it was written for this
    // engine.
    must_compile("examples/pages/pong.html");

    // async/await, template literals, for..of and try/catch, in a page written
    // against the real web platform.
    must_compile("examples/pages/fetchboard.html");

    // The pages the engine actually ships.
    must_compile("examples/pages/widgets.html");
    must_compile("examples/pages/invaders.html");

    // Regular expression literals, including named groups and lookahead.
    must_compile_source("var mobile = /iphone|android/i.test(navigator.userAgent);");
    must_compile_source("var named = /(?<k>\\d+)/.exec('42');");
    must_compile_source("var ahead = /\\B(?=(\\d{3})+(?!\\d))/g;");

    // Spread arguments and rest parameters.
    must_compile_source("Math.max(...[1, 2, 3]);");
    must_compile_source("function f(a, ...rest) { return f(a, ...rest); }");

    // Getters and setters, in object literals, classes and defineProperty.
    must_compile_source("var o = { get v() { return 1; }, set v(x) { this.n = x; } };");
    must_compile_source("class C { get v() { return 1; } static get w() { return 2; } }");
    must_compile_source("Object.defineProperty({}, 'x', { get() { return 1; } });");
    // A labelled block is not a loop, and `break lbl` out of one is legal.
    must_compile_source("outer: { if (1) { break outer; } }");
    // `continue` to a label that is not on a loop is an early error. The
    // early-error pass runs before the compiler, so its wording is what a page
    // sees and what this asserts.
    must_stop_at_source("outer: { continue outer; }", "names a label that is not on a loop");

    // Programs past the old instruction-encoding limits: more than 256 property
    // names, more than 256 locals, and jumps past 65,535 instructions. Each was
    // once a silent miscompile. Real libraries need these (p5.js has 1,452
    // top-level declarations).
    must_compile_source(repeated("function f(o){ return 0", " + o.p", "", 300, "; }"));
    must_compile_source(repeated("function g(){ ", "let v", " = 1; ", 300, "return v0; }"));
    must_compile_source(repeated("function h(o){ if (o) { ", "o.m(", ");", 40000, "} return 1; }"));

    REPORT("page_scripts");
}
