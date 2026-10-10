//===- ctcompile/test/Runtime/GC/Roots.cpp - GC rooting of compiled code --===//
//
// Does compiled code keep its values alive across a collection?
//
// This is the only test that runs backend-generated code with the collector
// forced to collect at every safepoint (set_gc_stress). Without stress, the
// rooting bug it guards against produces correct output.
//
// The bug: compiling `a + b + c` into two calls to ct_aot_binary_op with the
// first result held in a plain C++ local across the second.
// ct_aot_binary_op is a safepoint, the collector is precise and walks exactly
// the roots context::mark_roots (lib/Script/vm/objects/gc.cpp) visits, and a
// value living only in a native frame is reachable from none of them. Under
// stress such a function returns a six-character string where the
// interpreter returns sixty-five, and ASan reports a heap-use-after-free.
//
// The fixture makes the second safepoint a call, which covers the other kind
// of rooting the backend does. ct_aot_call takes a contiguous `argv`, so the
// arguments are copied into a run of frame slots reserved for that site, and
// the collection happens inside the call, after user JavaScript has run, with
// the intermediate string reachable only from that run.
//
// Other EmitC tests cannot see this: a use-after-free that nothing collects
// is invisible, because the freed memory still holds the right bytes. The bug
// needs a collection at exactly the wrong moment, and only set_gc_stress makes
// that reliable.
//
// How it is set up, and why each part matters:
//
//   `c` is an object with a valueOf, so the second `+` runs ToPrimitive, which
//   calls user JavaScript, which reaches context::call - the one place a
//   collection can begin. Two plain numbers would never collect and the test
//   would pass on a broken backend.
//
//   The strings are built at run time, never literals, so they are heap
//   objects the collector can actually free.
//
//   Every value the harness holds lives in a JavaScript global, never in a C++
//   local of this file; otherwise the harness would have the very bug it is
//   testing for, and would fail whether or not the backend was fixed.
//
//   The interpreter runs the same fixture first and defines the expected
//   answer, so no answer is written down by hand. The CTJS dialect's policy
//   says the same: "when a CTJS operation and the ctbrowser VM disagree, the
//   VM is correct by definition".
//
//===----------------------------------------------------------------------===//
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

#include "check.hpp"

using ctbrowser::script::context;
using ctbrowser::script::function_proto;
using ctbrowser::script::program;
using ctbrowser::script::value;

// Compiled at build time from test/Runtime/GC/roots.js by ctjs-translate,
// ctjs-opt, mlir-translate and the C++ compiler (see
// test/cmake/CoreAndBoxed.cmake). The build renames each symbol so these
// declarations do not depend on how the importer numbers functions.
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

void report(const char * what, bool ok, const std::string & got, const std::string & want) {
    std::printf("%-34s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        std::printf("    expected %zu chars: %s\n    got      %zu chars: %s\n", want.size(),
                    want.c_str(), got.size(), got.c_str());
        ++ctbrowser_test_failures;
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
        // The fixture is rebuilt without stress each time, so the strings are
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

    // The interpreter defines the answer; nothing here is written down twice.
    const std::string expected = attempt(f, "run", nullptr, true);
    report("the interpreter under stress", expected.size() == 65, expected, "65 characters");

    // The compiled body must agree with it, both under stress, which is the
    // point, and without, which catches a backend that is always wrong rather
    // than only wrong when the collector runs.
    report("compiled, collector hostile", attempt(f, "run", &ctc_f, true) == expected,
           attempt(f, "run", &ctc_f, true), expected);
    report("compiled, collector idle", attempt(f, "run", &ctc_f, false) == expected,
           attempt(f, "run", &ctc_f, false), expected);

    // A closure built in compiled code, held across the same collection.
    //
    // ct_aot_make_closure allocates and is a safepoint, and so is the call
    // after it. Three things have to survive: the string `a + b` builds, the
    // cell holding it, and the closure_object itself - and while `k` runs user
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

    // The string handed to a constructor, which lives in an argument window of
    // its own. ct_aot_construct allocates the instance before the body runs, so
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

    if (ctbrowser_test_failures == 0) { std::printf("\nall %d checks passed\n", 5); }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
