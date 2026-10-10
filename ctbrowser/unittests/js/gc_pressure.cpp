// A synchronous script that allocates far more than it keeps, and finishes.
//
// Until 2026-09-12 the only production trigger was `collect_if_due`, once per
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

#include "vm/expect.hpp"

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

    // A NATIVE'S OWN ALLOCATIONS SURVIVE ITS CALL BACK INTO SCRIPT. `build`
    // makes an object, runs a script callback that collects (every safepoint,
    // under stress), then writes to the object - the shape of `new Blob`
    // stringifying its parts, and of every binding that was safe only because
    // nothing collected inside a turn. See context::native_scope.
    {
        context stress;
        install_builtins(stress);
        stress.set_gc_stress(true);
        stress.define_native("build", [](context & c, std::span<value> args) {
            value made = c.make_object();
            auto * obj = static_cast<object_object *>(made.as_heap());
            obj->set("before", value::number(1));
            (void)c.call(args[0], {}, value::undefined());
            obj->set("after", value::number(2));
            return made;
        });
        const program pinned = compiler::compile(R"(
            var o = build(function () { var junk = []; for (var i = 0; i < 100; i++) { junk.push({ i: i }); } });
            return o.before + o.after;
        )");
        const run_result got = stress.run(pinned);
        CHECK(got.ok);
        CHECK_EQ(stress.to_string(got.returned), std::string{"3"});
        CHECK(stress.collections() > 0);
    }

    // A MICROTASK'S ARGUMENTS SURVIVE THE HANDLER THEY ARE DELIVERED TO. The
    // promise reaction job holds its record only in the popped job's argument
    // vector; a collection inside the handler freed it and the settle that
    // followed wrote into freed memory. 200 rounds of an async loop awaiting
    // settled, later-resolved and rejected promises crossed the threshold and
    // crashed in allocate().
    {
        context cx2;
        install_builtins(cx2);
        const program chain = compiler::compile(R"(
            var out = ''; var probes = [];
            for (var i = 0; i < 400; i++) { (function (i) { probes.push(function () {
                if (i % 4 === 0) { return 'sync'; }
                if (i % 4 === 1) { return Promise.resolve('res'); }
                if (i % 4 === 2) { return new Promise(function (r) { Promise.resolve().then(function () { r('late'); }); }); }
                return new Promise(function (r, j) { j('bad'); });
            }); })(i); }
            async function run() {
                var passed = 0, failed = 0;
                for (const p of probes) { try { await p(); passed++; } catch (e) { failed++; } }
                return passed + '/' + failed;
            }
            run().then(function (r) { out = r; }, function (e) { out = 'REJECTED ' + e; });
        )");
        const run_result r2 = cx2.run(chain);
        CHECK(r2.ok);
        CHECK_EQ(cx2.to_string(cx2.global("out")), std::string{"300/100"});
        CHECK(cx2.collections() > 0);
    }

    // A NATIVE'S SCRIPT CALLEE COLLECTS ITS OWN GARBAGE. testharness.js runs
    // every test body through `Function.prototype.apply`, and pinning all the
    // callee allocated held a NodeList read loop's garbage to the 4 GB cap
    // (dom/nodes/NodeList-static-length-getter-tampered-*).
    {
        context cx3;
        install_builtins(cx3);
        cx3.define_native("live", [](context & c, std::span<value>) {
            return value::number(static_cast<double>(c.live_objects()));
        });
        const program churn = compiler::compile(R"(
            function body() {
                var peak = 0;
                for (var i = 0; i < 200000; i++) {
                    var junk = { i: i };
                    if (i % 1000 === 0) { var now = live(); if (now > peak) { peak = now; } }
                }
                return { peak: peak };
            }
            return body.apply(null, []).peak;
        )");
        const run_result r3 = cx3.run(churn);
        CHECK(r3.ok);
        CHECK(r3.returned.is_number() && r3.returned.as_number() < 50000);
        std::printf("  gc: peak %s live under apply\n", cx3.to_string(r3.returned).c_str());
    }

    // ... AND WHAT THE CALLEE RETURNED SURVIVES, because the native holds it in
    // a C++ local while later calls collect (every safepoint, under stress).
    {
        context stress;
        install_builtins(stress);
        stress.set_gc_stress(true);
        stress.define_native("hold", [](context & c, std::span<value> args) {
            const value got = c.call(args[0], {}, value::undefined());
            for (int k = 0; k < 4; ++k) { (void)c.call(args[1], {}, value::undefined()); }
            return got;
        });
        const program held = compiler::compile(R"(
            var r = hold(function () { return { tag: 7 }; },
                         function () { var junk = []; for (var i = 0; i < 100; i++) { junk.push({ i: i }); } });
            return r.tag;
        )");
        const run_result got = stress.run(held);
        CHECK(got.ok);
        CHECK_EQ(stress.to_string(got.returned), std::string{"7"});
    }

    // WHAT THE INTERPRETER ITSELF HOLDS ACROSS A CALL is rooted where it is
    // made, not by a native's pin: a generator whose parameters run a callback
    // before its object is anywhere else, and a spread through a proxy's `get`
    // trap. Both were freed once the pin narrowed (ASan, 2026-09-26), and both
    // were already exposed in a script no native was running.
    {
        context stress;
        install_builtins(stress);
        stress.set_gc_stress(true);
        const program held = compiler::compile(R"(
            function* g(a = [1, 2].map(function (x) { return x * 2; })) { yield a.length; }
            var spread = [...new Proxy([{ n: 1 }, { n: 2 }], { get: function (t, k) { return t[k]; } })];
            return g().next().value + spread[0].n + spread[1].n;
        )");
        const run_result got = stress.run(held);
        CHECK(got.ok);
        CHECK_EQ(stress.to_string(got.returned), std::string{"5"});
    }

    // A NATIVE CALLEE'S RESULT SURVIVES TOO. A bound Reflect.get hands `hold`
    // a value read off a data property; the next callback clears the property
    // and collects, and nothing but the pin holds the object any more.
    {
        context stress;
        install_builtins(stress);
        stress.set_gc_stress(true);
        stress.define_native("hold", [](context & c, std::span<value> args) {
            const value got = c.call(args[0], {}, value::undefined());
            for (int k = 0; k < 4; ++k) { (void)c.call(args[1], {}, value::undefined()); }
            return got;
        });
        const program held = compiler::compile(R"(
            var box = { v: { tag: 7 } };
            var r = hold(Reflect.get.bind(null, box, 'v'),
                         function () { box.v = null; var junk = []; for (var i = 0; i < 100; i++) { junk.push({ i: i }); } });
            return r.tag;
        )");
        const run_result got = stress.run(held);
        CHECK(got.ok);
        CHECK_EQ(stress.to_string(got.returned), std::string{"7"});
    }

    // A RE-ENTRY REFUSED WITHOUT A THROW is still a throw to call_fenced. The
    // first refusal of an episode throws the RangeError; the next one, made
    // from the same depth, answered undefined with threw=false, and an
    // async-from-sync step cast that undefined to its result array.
    {
        context cx4;
        install_builtins(cx4);
        std::string seen;
        cx4.define_native("dive", [&seen](context & c, std::span<value> args) {
            bool threw = false;
            value thrown = value::undefined();
            (void)c.call_fenced(args[0], args, value::undefined(), threw, thrown);
            if (threw && seen.empty()) {
                bool again = false;
                (void)c.call_fenced(args[0], args, value::undefined(), again, thrown);
                seen = again ? "threw" : "silent";
            }
            return value::undefined();
        });
        const run_result got = cx4.run(cx4.own_program(compiler::compile("dive(dive); return 1;")));
        CHECK(got.ok);
        CHECK_EQ(seen, std::string{"threw"});
    }
    REPORT("gc_pressure");
}
