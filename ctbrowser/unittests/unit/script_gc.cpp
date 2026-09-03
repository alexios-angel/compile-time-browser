// The precise collector, made to run at the moment a C++ local is the only
// reference to a heap value.
//
// WHY THIS FILE EXISTS AT ALL. In a shipped page NOTHING COLLECTS WHILE SCRIPT
// IS RUNNING: the only production trigger is `collect_if_due`, once per tick,
// from the browser's frame loop. So every rooting bug inside a synchronous
// builtin call is unreachable from an ordinary page and perfectly invisible to
// test262 and WPT, which measure conformance from outside. It is reachable
// through `$262.gc` and through gc stress - and those are exactly the modes
// ctcompile's differential gate runs this interpreter in, holding the compiler
// to whatever answer comes back. A wrong answer here is not a failure; it is a
// specification.
//
// `gc()` here is `$262.gc` minus test262 - c.collect(), the real mark-sweep -
// so a case in this file is a case a corpus run can reproduce. Every assertion
// checks TWO things: the answer, and that a collection actually happened.
// Asserting an answer alone proves nothing, because the answer is the same when
// the collector silently did not run, which is precisely how a broken forced-GC
// mode looks.
#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

using ctbrowser::script::context;
using ctbrowser::script::program;
using ctbrowser::script::run_result;
using ctbrowser::script::value;

namespace {

struct gc_run {
    std::string answer;
    std::size_t collections = 0;
};

[[nodiscard]] gc_run run_with_gc(const std::string & source) {
    const program prog = ctbrowser::script::compiler::compile(source);
    context cx;
    ctbrowser::script::install_builtins(cx);
    // The same hook ct262 installs, under the same name a test262 case uses.
    cx.define_native("gc", [](context & c, std::span<value>) {
        return value::number(static_cast<double>(c.collect()));
    });
    const run_result r = cx.run(prog);
    gc_run out;
    out.collections = cx.collections();
    out.answer = r.ok ? cx.to_string(r.returned) : std::string{"THREW"};
    return out;
}

void expect_gc(std::string_view what, const std::string & source, std::string_view want) {
    const gc_run got = run_with_gc(source);
    if (got.answer != want) {
        std::printf("FAIL %-46s => %s (want %s)\n", std::string{what}.c_str(), got.answer.c_str(),
                    std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
    // THE GUARD ON THE GUARD. A `gc()` that collected nothing gives the same
    // answer as a correct engine, so without this the whole file could pass
    // while measuring an interpreter no collection ever entered.
    if (got.collections == 0) {
        std::printf("FAIL %-46s ran NO collection - the case measured nothing\n",
                    std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

// --- native lambda captures ------------------------------------------------
//
// A `value` captured by a C++ lambda inside a native is invisible to the
// collector: `mark_object`'s native arm walks `props` and nothing else, and
// `each_root` has no inventory of captures. `new Promise` was fixed by rooting
// its captures in a property; these two were not.

// `Function.prototype.bind` captures THREE values - the target closure, the
// receiver and the bound arguments - and roots none of them. The IIFE is what
// makes the capture the only reference there is: after it returns, `target` and
// `tag` are reachable from the bound native's C++ lambda and from nowhere else.
void test_bind_captures_survive_a_collection() {
    expect_gc("bind's target and bound args survive gc", R"(
        var g = (function () {
            var tag = { name: 'bound' };
            function target(a, b) { return a.name + '/' + b.name; }
            return target.bind(null, tag);
        })();
        gc();
        // Allocate over the freed cells, so a stale pointer reads somebody
        // else's object rather than merely reading memory nobody rewrote.
        var filler = [];
        for (var i = 0; i < 400; i++) { filler.push({ name: 'filler' }); }
        gc();
        return g({ name: 'late' });
    )",
              "bound/late");
}

// `Symbol.for`'s registry is a shared_ptr<vector<pair<string, value>>> captured
// by `for` and by `keyFor`. The KEYS are C++ strings and survive; the SYMBOLS
// are values in a capture, so a collection frees them while the registry still
// lists them - and `Symbol.for` then hands back a pointer into a recycled cell.
void test_symbol_registry_survives_a_collection() {
    expect_gc("Symbol.for's registry survives gc", R"(
        var s = Symbol.for('KEY');
        s = null;
        gc();
        var filler = [];
        for (var i = 0; i < 400; i++) { filler.push(Symbol('recycled')); }
        return Symbol.for('KEY').description;
    )",
              "KEY");
    expect_gc("Symbol.keyFor's registry survives gc", R"(
        var s = Symbol.for('OTHER');
        s = null;
        gc();
        var filler = [];
        for (var i = 0; i < 400; i++) { filler.push(Symbol('recycled')); }
        return Symbol.keyFor(Symbol.for('OTHER'));
    )",
              "OTHER");
    // AND IDENTITY, which is the one guarantee the registry exists to give.
    expect_gc("Symbol.for interns across a gc", R"(
        var a = Symbol.for('SAME');
        a = null;
        gc();
        return String(Symbol.for('SAME') === Symbol.for('SAME'));
    )",
              "true");
}

// --- array builtins across a callback --------------------------------------
//
// Each of these holds a heap value in a C++ local across a call back into the
// VM. `context::rooted` is the primitive for exactly this and none of them used
// it. The callback's `gc()` is what a page cannot do and `$262.gc` can.

void test_map_result_survives_a_collection() {
    // `map`'s `out` is allocated BEFORE the first callback and lives only in a
    // C++ local until it is returned, so the first `gc()` frees the array the
    // remaining iterations then push into.
    expect_gc("map's result array survives gc in the callback", R"(
        var out = [1, 2, 3].map(function (x) { gc(); return { v: x }; });
        return out.length + ':' + out[0].v + ',' + out[1].v + ',' + out[2].v;
    )",
              "3:1,2,3");
}

void test_filter_result_survives_a_collection() {
    // `filter` survived only by accident: the values it keeps are also in the
    // rooted source array. Its `out` is as unrooted as `map`'s.
    expect_gc("filter's result array survives gc in the callback", R"(
        var src = [{ v: 1 }, { v: 2 }, { v: 3 }];
        var out = src.filter(function (o) { gc(); return o.v !== 2; });
        return out.length + ':' + out[0].v + ',' + out[1].v;
    )",
              "2:1,3");
}

void test_flat_map_result_survives_a_collection() {
    expect_gc("flatMap's result array survives gc in the callback", R"(
        var out = [1, 2].flatMap(function (x) { gc(); return [{ v: x }, { v: x * 10 }]; });
        return out.length + ':' + out[0].v + ',' + out[1].v + ',' + out[2].v + ',' + out[3].v;
    )",
              "4:1,10,2,20");
}

void test_sort_snapshot_survives_a_collection() {
    // `sort` copies the array so a comparator cannot make it index out of
    // bounds - and that snapshot is a bare std::vector<value>. A comparator
    // that empties the array leaves the snapshot holding the ONLY reference to
    // every element not currently an argument, and `gc()` then frees them
    // under the merge that is still reading them.
    expect_gc("sort's snapshot survives gc in the comparator", R"(
        var a = [{ k: 3 }, { k: 1 }, { k: 4 }, { k: 2 }];
        a.sort(function (x, y) { a.length = 0; gc(); return x.k - y.k; });
        var out = [];
        for (var i = 0; i < a.length; i++) { out.push(a[i].k); }
        return out.join(',');
    )",
              "1,2,3,4");
    // The comparator itself is a C++ local too, and the only reference to it
    // once the expression that produced it has been overwritten.
    expect_gc("sort's comparator survives gc in itself", R"(
        var a = [{ k: 2 }, { k: 3 }, { k: 1 }];
        var by = function (x, y) { hold = null; gc(); return x.k - y.k; };
        var hold = by;
        by = null;
        a.sort(hold);
        var out = [];
        for (var i = 0; i < a.length; i++) { out.push(a[i].k); }
        return out.join(',');
    )",
              "1,2,3");
    // THE DEFAULT SORT RUNS USER CODE TOO. It has no comparator, but it calls
    // `to_string` on every element to build its keys, and `toString` is a page
    // method - so the same window is open on the path nobody passes a function
    // to. This one also walks `self->items` with a range-for while that code
    // can push to it.
    expect_gc("the default sort survives a mutating toString", R"(
        var a = [];
        a.push({ toString: function () { a.push({ toString: function () { return 'z'; } });
                                         gc(); return 'b'; } });
        a.push({ toString: function () { gc(); return 'a'; } });
        a.sort();
        return String(a.length >= 2);
    )",
              "true");
}

// --- the mark phase --------------------------------------------------------

// `mark_object` recursed once per EDGE, with no depth bound and no worklist, so
// the C++ stack had to be as deep as the object graph is long. A linked list is
// the ordinary shape that produces one: `head = { next: head }` in a loop is
// something a page writes on purpose.
//
// 200,000 is chosen because ~130,000 was measured to exhaust the default 8 MiB
// stack. The failure was not a bad answer - it was exit 139, with `mark_object`
// all the way up the backtrace.
void test_a_deep_chain_does_not_exhaust_the_stack() {
    expect_gc("a 200k-deep chain can be marked", R"(
        var head = null;
        for (var i = 0; i < 200000; i++) { head = { next: head }; }
        gc();
        var n = 0;
        var at = head;
        while (at !== null && n < 200000) { n++; at = at.next; }
        return String(n);
    )",
              "200000");
    // ...and the same for the other two shapes that make a long chain: an
    // array's elements and a closure's captured cell.
    expect_gc("a 200k-deep array chain can be marked", R"(
        var head = null;
        for (var i = 0; i < 200000; i++) { head = [head]; }
        gc();
        var n = 0;
        while (head !== null && n < 200000) { n++; head = head[0]; }
        return String(n);
    )",
              "200000");
}

} // namespace

// ONE CASE AT A TIME, from the command line - `ctbrowser-test-script_gc bind`.
//
// Not a convenience. ASan does not recover: the first heap-use-after-free ends
// the process, so a file of use-after-free cases run in one go reports the
// first and hides the rest. Under the asan preset each case has to be run on
// its own to be seen at all, and the filter is what makes that one command
// rather than an edit-and-rebuild. ctest runs the whole file with no argument.
int main(int argc, char ** argv) {
    const std::string_view only = argc > 1 ? std::string_view{argv[1]} : std::string_view{};
    const auto run = [&](std::string_view name, void (*body)()) {
        if (only.empty() || name.find(only) != std::string_view::npos) { body(); }
    };
    run("bind", &test_bind_captures_survive_a_collection);
    run("symbol", &test_symbol_registry_survives_a_collection);
    run("map", &test_map_result_survives_a_collection);
    run("filter", &test_filter_result_survives_a_collection);
    run("flatmap", &test_flat_map_result_survives_a_collection);
    run("sort", &test_sort_snapshot_survives_a_collection);
    run("chain", &test_a_deep_chain_does_not_exhaust_the_stack);
    REPORT("script_gc");
}
