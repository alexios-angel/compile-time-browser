#pragma once
#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include <string>
#include <string_view>

// EVALUATE ONE JAVASCRIPT EXPRESSION AND COMPARE WHAT IT PRINTS.
//
// Every differential suite in this directory wants exactly this and nothing
// more, and there were ten copies of it before it moved here - which is the
// same threshold `core/algorithms.hpp` used. The shape matters more than the
// saving: all of these files assert against V8, so they must agree on what
// "the answer" is, and a private copy that rendered a result differently would
// make two suites disagree for a reason no diff would show.
//
// `to_string` is the renderer on purpose. It is the engine's own
// Number::toString and String coercion, so what is compared is what a page
// would see, not a debug spelling - and it is why these files could be written
// against node's output directly.

namespace ctbrowser_test {

// The value a program RETURNS, rendered the way JavaScript prints it. A parse
// or runtime fault is "THREW", flattened deliberately: what these suites check
// is whether an expression is REFUSED, not which message came back, and pinning
// the text would make every error-message improvement a test failure.
[[nodiscard]] inline std::string js_run(std::string_view expression) {
    using namespace ctbrowser::script;
    const program prog = compiler::compile("return (" + std::string{expression} + ");");
    context cx;
    install_builtins(cx);
    const run_result r = cx.run(prog);
    if (!r.ok) { return "THREW"; }
    return cx.to_string(r.returned);
}

} // namespace ctbrowser_test

// One expression, one expected rendering. Declared as a macro-free function so
// a failure prints the expression that produced it rather than a line number.
inline void js_expect(std::string_view expression, std::string_view want) {
    const std::string got = ctbrowser_test::js_run(expression);
    if (got != want) {
        std::printf("FAIL     %-52s => %s (want %s)\n", std::string{expression}.c_str(),
                    got.c_str(), std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

// `-0` and `+0` print identically, so a string comparison cannot separate them.
// `1/x` can: -Infinity for -0, +Infinity for +0. `Object.is` would be the
// natural spelling and this engine does not have it yet.
inline void js_expect_negative_zero(std::string_view expression, bool want) {
    js_expect("1/(" + std::string{expression} + ") === -Infinity", want ? "true" : "false");
}
