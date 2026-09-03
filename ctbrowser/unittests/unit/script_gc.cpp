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
    run("chain", &test_a_deep_chain_does_not_exhaust_the_stack);
    REPORT("script_gc");
}
