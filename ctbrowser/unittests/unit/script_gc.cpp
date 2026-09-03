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
    run("chain", &test_a_deep_chain_does_not_exhaust_the_stack);
    REPORT("script_gc");
}
