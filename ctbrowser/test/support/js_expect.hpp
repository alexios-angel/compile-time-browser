//===- ctbrowser/test/support/js_expect.hpp - Expression checks -----------===//
//
// Evaluate one JavaScript expression and compare what it prints. Every
// differential suite under unittests/js/ asserts against V8 (node) output, so
// they must all agree on what "the answer" is; this one helper is that
// agreement.
//
// `to_string` is the renderer on purpose: it is the engine's own
// Number::toString and String coercion, so what is compared is what a page
// would see, not a debug spelling. That is why these files can be written
// against node's output directly.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include <string>
#include <string_view>

namespace ctbrowser_test {

/// Returns the value of `expression`, rendered the way JavaScript prints it. A
/// parse or runtime fault is "THREW", flattened deliberately: these suites
/// check whether an expression is refused, not which message came back, and
/// pinning the text would make every error-message improvement a failure.
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

/// Fails the test when `expression` does not render as `want`. A function, not
/// a macro, so a failure prints the expression rather than a line number.
inline void js_expect(std::string_view expression, std::string_view want) {
    const std::string got = ctbrowser_test::js_run(expression);
    if (got != want) {
        std::printf("FAIL     %-52s => %s (want %s)\n", std::string{expression}.c_str(),
                    got.c_str(), std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

/// Checks whether `expression` is -0. `-0` and `+0` print identically, so a
/// string comparison cannot separate them; `1/x` can (-Infinity for -0). Kept
/// on `1/x` rather than `Object.is` so the check does not depend on the
/// builtin under test.
inline void js_expect_negative_zero(std::string_view expression, bool want) {
    js_expect("1/(" + std::string{expression} + ") === -Infinity", want ? "true" : "false");
}
