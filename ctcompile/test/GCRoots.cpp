// DOES COMPILED CODE KEEP ITS VALUES ALIVE ACROSS A COLLECTION?
//
// This is the only test in the suite that runs code the backend generated,
// against the real runtime, with the collector deliberately made hostile - and
// it exists because nothing weaker caught the defect it now guards.
//
// WHAT WENT WRONG. The backend compiled `a + b + c` into two calls to
// ct_aot_binary_op with the first result held in a plain C++ local across the
// second. ct_aot_binary_op is is_safepoint, the collector is PRECISE and walks
// exactly the roots in GCRoots.def, and a value living only in a native frame
// is reachable from none of them. Under set_gc_stress the emitted function
// returned a six-character string where the interpreter returned the correct
// sixty-five; ASan called it a heap-use-after-free, the buffer freed and read
// inside the same call. Without stress it was correct every time.
//
// THE FIXTURE NOW MAKES THE SECOND SAFEPOINT A CALL, which covers the other
// kind of rooting the backend does. ct_aot_call takes a CONTIGUOUS `argv`, so
// the arguments are copied into a run of frame slots reserved for that site -
// and the collection happens inside the call, after user JavaScript has run,
// with the intermediate string reachable only from that run.
//
// WHY THE OTHER TESTS COULD NOT SEE IT. Every EmitC test compiles what the
// backend emits and several run it, but a use-after-free that nothing collects
// is invisible: the freed memory is still there and still holds the right
// bytes. The bug needs a collection to happen at exactly the wrong moment, and
// only set_gc_stress makes that reliable. A backend can be read, compiled and
// executed and still be wrong here.
//
// HOW IT IS SET UP, and each part matters:
//
//   `c` IS AN OBJECT WITH A valueOf, so the second `+` runs ToPrimitive, which
//   calls user JavaScript, which reaches context::call - the one place a
//   collection can begin. Two plain numbers would never collect and the test
//   would pass on a broken backend.
//
//   THE STRINGS ARE BUILT AT RUN TIME, never literals, so they are heap objects
//   the collector can actually free.
//
//   EVERY VALUE THE HARNESS HOLDS LIVES IN A JAVASCRIPT GLOBAL, never in a C++
//   local of this file - otherwise the harness would have the very bug it is
//   testing for, and would fail whether or not the backend was fixed.
//
//   AND THE INTERPRETER RUNS THE SAME FIXTURE FIRST. If the expected answer
//   were written down by hand and the fixture drifted, this would be asserting
//   against a stale constant; the interpreter is the definition of correct
//   here, exactly as the dialect's own policy says - "when a CTJS operation and
//   the ctbrowser VM disagree, the VM is correct by definition".
#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/aot/aot_entry.h>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

using ctbrowser::script::context;
using ctbrowser::script::function_proto;
using ctbrowser::script::program;
using ctbrowser::script::value;

// COMPILED BY THE BUILD, from test/gc-roots.js through the real pipeline:
// ctjs-translate, then ctjs-opt, then mlir-translate, then this project's own
// C++ compiler. The symbol is renamed by the build so this declaration does not
// depend on how the importer numbers functions.
extern "C" std::int32_t ctc_held(ctbrowser::aot::ct_aot_ctx *, const ctbrowser::aot::ct_aot_site *,
                                 const std::uint64_t *, std::uint32_t, std::uint64_t, std::uint32_t,
                                 std::uint64_t *);

extern "C" std::int32_t ctc_f(ctbrowser::aot::ct_aot_ctx *, const ctbrowser::aot::ct_aot_site *,
                              const std::uint64_t *, std::uint32_t, std::uint64_t, std::uint32_t,
                              std::uint64_t *);

extern "C" std::int32_t ctc_built(ctbrowser::aot::ct_aot_ctx *, const ctbrowser::aot::ct_aot_site *,
                                  const std::uint64_t *, std::uint32_t, std::uint64_t,
                                  std::uint32_t, std::uint64_t *);

namespace {

constexpr std::string_view fixture =
#include "gc-roots.js.inc"
    ;

int failures = 0;

void report(const char * what, bool ok, const std::string & got, const std::string & want) {
    std::printf("%-34s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        std::printf("    expected %zu chars: %s\n    got      %zu chars: %s\n", want.size(),
                    want.c_str(), got.size(), got.c_str());
        ++failures;
    }
}

} // namespace

int main() {
    program compiled = ctbrowser::script::compiler::compile(std::string(fixture));
    if (!compiled.ok) {
        std::printf("the fixture did not compile\n");
        return 1;
    }

    context cx;
    ctbrowser::script::install_builtins(cx);
    if (!cx.run(compiled).ok) {
        std::printf("the fixture did not run\n");
        return 1;
    }

    function_proto * f = nullptr;
    for (function_proto & each : compiled.functions) {
        if (each.name == "f") { f = &each; }
    }
    if (f == nullptr) {
        std::printf("no function_proto named f\n");
        return 1;
    }

    const auto attempt = [&](function_proto * body, const char * driver,
                             ctbrowser::aot::ct_aot_entry_fn entry, bool stress) {
        // THE FIXTURE IS REBUILT WITHOUT STRESS EACH TIME, so the strings are
        // fresh heap objects and one run cannot leave the next a survivor.
        body->aot_entry = nullptr;
        cx.set_gc_stress(false);
        cx.call(cx.global("setup"), std::span<const value>{}, value::undefined());
        body->aot_entry = entry;
        cx.set_gc_stress(stress);
        cx.call(cx.global(driver), std::span<const value>{}, value::undefined());
        const std::string answer = cx.to_string(cx.global("R"));
        cx.set_gc_stress(false);
        return answer;
    };

    // THE INTERPRETER DEFINES THE ANSWER. Nothing here is written down twice.
    const std::string expected = attempt(f, "run", nullptr, true);
    report("the interpreter under stress", expected.size() == 65, expected, "65 characters");

    // AND THE COMPILED BODY MUST AGREE WITH IT - under stress, which is the
    // whole point, and without, which catches a backend that is wrong always
    // rather than only when the collector runs.
    report("compiled, collector hostile", attempt(f, "run", &ctc_f, true) == expected,
           attempt(f, "run", &ctc_f, true), expected);
    report("compiled, collector idle", attempt(f, "run", &ctc_f, false) == expected,
           attempt(f, "run", &ctc_f, false), expected);

    // AND A CLOSURE BUILT IN COMPILED CODE, HELD ACROSS THE SAME COLLECTION.
    //
    // ct_aot_make_closure allocates and is a safepoint, and so is the call
    // after it. THREE things have to survive: the string `a + b` builds, the
    // CELL holding it, and the closure_object itself - and while `k` runs user
    // JavaScript, none of them is reachable from anything but this frame's
    // slots.
    function_proto * kept = nullptr;
    for (function_proto & candidate : compiled.functions) {
        if (candidate.name == "held") { kept = &candidate; }
    }
    if (kept == nullptr) {
        std::printf("no function_proto named held\n");
        return 1;
    }
    const std::string want_held = attempt(kept, "runHeld", nullptr, true);
    report("a closure, collector hostile", attempt(kept, "runHeld", &ctc_held, true) == want_held,
           attempt(kept, "runHeld", &ctc_held, true), want_held);

    // AND THE STRING HANDED TO A CONSTRUCTOR, which lives in a window of its
    // own. ct_aot_construct allocates the instance before the body runs, so
    // under stress the collection happens with `a + b` reachable from nothing
    // but ct_aot_construct's parked arguments.
    function_proto * builder = nullptr;
    for (function_proto & candidate : compiled.functions) {
        if (candidate.name == "built") { builder = &candidate; }
    }
    if (builder == nullptr) {
        std::printf("no function_proto named built\n");
        return 1;
    }
    const std::string want_built = attempt(builder, "runBuilt", nullptr, true);
    report("a constructor, collector hostile",
           attempt(builder, "runBuilt", &ctc_built, true) == want_built,
           attempt(builder, "runBuilt", &ctc_built, true), want_built);

    if (failures == 0) { std::printf("\nall %d checks passed\n", 5); }
    return failures == 0 ? 0 : 1;
}
