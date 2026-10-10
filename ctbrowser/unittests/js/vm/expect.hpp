#pragma once
// The assertions the vm_*.cpp files share: a whole program run as written, and
// what a global holds after the turn's microtasks have drained. They were
// js/vm_basics.cpp's file-scope helpers until that file was split on 2026-09-08;
// the using-directive below is the one that file had at file scope, so every
// file here sees exactly what that one saw.
//
// NOT js_expect.hpp, and the comment above `expect_result` says why.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include <cstdio>
#include <string>
#include <string_view>

using namespace ctbrowser::script;

// Run through the the engine VM and render the result the way JS would print it.
[[nodiscard]] inline std::string run_vm(std::string_view source, bool * ok = nullptr) {
    const program prog = compiler::compile(source);
    context cx;
    install_builtins(cx);
    const run_result r = cx.run(prog);
    if (ok != nullptr) { *ok = r.ok; }
    if (!r.ok) { return "<error: " + r.error + ">"; }
    return cx.to_string(r.returned);
}

// A whole program, run as written. The stage-2 tests are about STATEMENTS -
// loops, labels, try blocks - so they are written as programs ending in an
// explicit `return`, not as expressions to be wrapped.
//
// WHICH IS WHY THIS FILE DOES NOT USE `js_expect` from test/support. That
// helper wraps its argument as `return (expr);`, which is right for the ten
// differential suites next door and cannot express a function declaration, a
// class body or a labelled loop - and 765 of the 768 assertions here are whole
// statements. `run_vm` also has a second caller this file cannot do without:
// `diff_vs_v1` needs the same source through ctjs, and needs to know whether
// each side merely RAN as well as what it returned.
//
// There was a second, identical `expect()` beside this until 2026-08-09 - same
// body, same output, 386 call sites against this one's 385. Two spellings of
// one assertion in one file is how two halves of a suite drift apart.
inline void expect_result(std::string_view source, std::string_view want) {
    const std::string got = run_vm(source);
    if (got != want) {
        std::printf("FAIL     %.70s => %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
                    std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

// What `globalThis.result` holds AFTER THE TURN ENDS - which is where a promise
// handler runs.
//
// `expect_result` reads what the top level RETURNED, and a `return` is evaluated
// before the microtask queue drains, so it cannot see anything a `then` did.
// That is not a limitation of the test: it is the ordering being asserted, and
// these two functions together say both halves - `then` has not run yet at the
// return, and it has by the end of the turn.
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
