// HostPromiseRejectionTracker (ECMA-262 27.2.1.9) as context::set_rejection_tracker:
// "reject" (handled=false) when a promise is rejected with no reaction ever
// attached, "handle" (handled=true) when one is attached to such a promise
// afterwards - and nothing at all for a rejection that already had a handler,
// or for a fulfilment. The host builds `unhandledrejection` and
// `rejectionhandled` on top; nothing here dispatches an event.
#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include <cstdio>
#include <string>
#include <string_view>

using ctbrowser::script::compiler;
using ctbrowser::script::context;
using ctbrowser::script::program;
using ctbrowser::script::script_kind;
using ctbrowser::script::value;

namespace {

// Runs `source` with a tracker that appends "R" (reject, unhandled) or "H"
// (handled later) per call, and answers the sequence.
std::string track(std::string_view source) {
    const program prog = compiler::compile(source, script_kind::classic);
    context cx;
    ctbrowser::script::install_builtins(cx);
    std::string seen;
    cx.set_rejection_tracker([&](value, bool handled) { seen += handled ? 'H' : 'R'; });
    const auto result = cx.run(prog);
    if (!result.ok) { return "error: " + result.error; }
    return seen;
}

void expect_track(std::string_view source, std::string_view want) {
    const std::string got = track(source);
    if (got != want) {
        std::printf("FAIL rejection tracker: %s -> %s (want %.*s)\n", std::string{source}.c_str(),
                    got.c_str(), static_cast<int>(want.size()), want.data());
        ++ctbrowser_test_failures;
    }
}

void test_rejection_tracker() {
    // Rejected with nobody listening: "reject" once.
    expect_track("Promise.reject(1);", "R");
    expect_track("new Promise((_, j) => j(1));", "R");
    // A handler attached BEFORE the rejection: [[PromiseIsHandled]] was true.
    expect_track("let r; const p = new Promise((_, j) => { r = j; }); p.catch(() => {}); r(1);",
                 "");
    expect_track("let r; const p = new Promise((_, j) => { r = j; }); p.then(0, () => {}); r(1);",
                 "");
    // Attached AFTER: "reject" then "handle", once each - the executor
    // rejects before `.catch` runs.
    expect_track("new Promise((_, j) => j(1)).catch(() => {});", "RH");
    expect_track("const p = Promise.reject(1); p.catch(() => {}); p.catch(() => {});", "RH");
    // ...and the derived promise `then` makes with no rejection handler is a
    // fresh unhandled rejection of its own.
    expect_track("const p = Promise.reject(1); p.then(() => {});", "RHR");
    // `finally`'s thrower rejects the inner `then` promise a job before the
    // thenable job hands it a reaction (27.2.5.3.1 catchFinally), so that one
    // is a reject/handle pair of its own before the outer derived promise.
    expect_track("const p = Promise.reject(1); p.finally(() => {});", "RHRHR");
    // An await is a PerformPromiseThen too.
    expect_track("const p = Promise.reject(1); (async () => { try { await p; } catch (e) {} })();",
                 "RH");
    // A fulfilment is nobody's business, and a derived promise that rejects
    // with nothing after it is its own "reject".
    expect_track("Promise.resolve(1);", "");
    expect_track("Promise.resolve(1).then(() => { throw 2; });", "R");
    // An async function whose body throws is a rejected promise nobody caught.
    expect_track("(async () => { throw 1; })();", "R");
    expect_track("(async () => { throw 1; })().catch(() => {});", "RH");
}

} // namespace

int main() {
    test_rejection_tracker();
    REPORT("script_rejection_tracker");
}
