//===- ctbrowser/unittests/js/vm/expect.hpp - Assertions --------*- C++ -*-===//
//
// Assertions shared by the js/vm/*.cpp tests: a whole program run as written,
// and what a global holds after the turn's microtasks drain. These tests do
// not use js_expect.hpp; the comment above `expect_result` says why. The
// file-scope using-directive below is deliberate: every including test
// relies on it.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include <cstdio>
#include <string>
#include <string_view>

using namespace ctbrowser::script;

/// Runs `source` on the engine VM and renders the result the way JS would
/// print it. When `ok` is non-null it receives whether the program ran; a
/// failed run renders as "<error: ...>".
[[nodiscard]] inline std::string run_vm(std::string_view source, bool * ok = nullptr) {
    const program prog = compiler::compile(source);
    context cx;
    install_builtins(cx);
    const run_result r = cx.run(prog);
    if (ok != nullptr) { *ok = r.ok; }
    if (!r.ok) { return "<error: " + r.error + ">"; }
    return cx.to_string(r.returned);
}

/// Fails the test unless the whole program `source`, run as written, returns
/// `want`. These tests are about statements (loops, labels, try blocks), so
/// they are written as programs ending in an explicit `return`, not as
/// expressions to be wrapped.
///
/// This does not use js_expect (ctbrowser/test/support/js_expect.hpp): that
/// helper wraps its argument as `return (expr);`, which cannot express a
/// function declaration, class body or labelled loop, and most assertions
/// here are whole statements. run_vm also serves diff_vs_v1 in
/// vm/operators.cpp, which needs to know whether each side ran as well as
/// what it returned.
inline void expect_result(std::string_view source, std::string_view want) {
    const std::string got = run_vm(source);
    if (got != want) {
        std::printf("FAIL     %.70s => %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
                    std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

/// Fails the test unless `globalThis.result` holds `want` after the turn ends,
/// which is when promise handlers have run.
///
/// `expect_result` reads what the top level returned, and a `return` is
/// evaluated before the microtask queue drains, so it cannot see anything a
/// `then` did. That is the ordering being asserted: together the two helpers
/// show that `then` has not run at the return and has by the end of the turn.
inline void expect_after_turn(std::string_view source, std::string_view want) {
    const program prog = compiler::compile(std::string{source});
    context cx;
    install_builtins(cx);
    const run_result r = cx.run(prog);
    const std::string got = r.ok ? cx.to_string(cx.global("result")) : "<error: " + r.error + ">";
    if (got != want) {
        std::printf("FAIL     %.70s => %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
                    std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}
