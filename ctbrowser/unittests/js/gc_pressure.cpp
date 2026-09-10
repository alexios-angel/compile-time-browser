// A synchronous script that allocates far more than it keeps, and finishes.
//
// Until 2026-09-10 the only production trigger was `collect_if_due`, once per
// tick, BETWEEN turns - so one long top-level script accumulated every object
// it ever made. Seven WPT html/dom/reflection-*.html files, thousands of
// subtests each in a single script, died at 1.6-2.0 s under the driver's 4 GB
// address-space cap; run-wpt.py saw the socket close and called it TIMEOUT.
// `safepoint()` now runs the same threshold check inside a turn, at every C++
// entry into JavaScript - `Function.prototype.call` here, `.apply` in
// testharness.js's Test.prototype.step.
//
// ponytail: an interpreted JS-to-JS call is not a safepoint (pinned by
// ctcompile/test/EscapeCycle.cpp), so a loop that only makes those still
// collects at the tick; add op::call to the trigger when a page shows it.
//
// THE COUNTER, NOT ONLY THE ANSWER. The sum is the same whether or not a
// collection ran, so the case also asserts that collections happened and that
// the heap was bounded when the script returned - the two facts an unbounded
// collector gets wrong.

#include "vm_expect.hpp"

int main() {
    const program prog = compiler::compile(R"(
        function make(i) { return { n: i }; }
        var total = 0;
        for (var i = 0; i < 2000000; i++) { total = total + make.call(null, i).n; }
        return total;
    )");
    context cx;
    install_builtins(cx);
    const run_result r = cx.run(prog);
    CHECK(r.ok);
    CHECK_EQ(cx.to_string(r.returned), std::string{"1999999000000"});
    CHECK(cx.collections() > 0);
    // Two million made, a few hundred kept: the survivors are the builtins and
    // one number, so the threshold never leaves its floor.
    CHECK(cx.live_objects() < 20000);
    std::printf("  gc: %zu collections, %zu live at the end\n", cx.collections(),
                cx.live_objects());
    REPORT("gc_pressure");
}
