//===- ctcompile/test/Runtime/Launcher/Application.cpp - AOT app launcher -===//
//
// A generated application, and the same application interpreted.
//
// Most ctcompile tests check one piece of the compiler. This one is a whole
// application: one JavaScript file (application.js), compiled to C++ by the
// real pipeline, linked into a native executable beside `ctbrowser::script`,
// with its compiled bodies installed onto the right `function_proto`s by the
// program itself at startup. Nothing here names a function, and nothing
// assigns `aot_entry` by hand - the differential harness (Runtime/Differential/
// Differential.cpp) does both, which suits a harness driving many bodies one
// case at a time but is not a mechanism an application could ship.
//
// One source, two executables. This file is compiled twice: once alone, and
// once with the generated bodies and the generated entry table linked in and
// `CTCOMPILE_LAUNCHER_AOT` defined. application.test (a lit test beside this
// file) runs both and compares their stdout byte for byte. That comparison is
// the correctness half.
//
// It is only half, and the harder half is the other one. An application that
// silently ran everything interpreted would print exactly the same bytes and
// exit 0 - it is the same program either way. So both executables also report
// the dispatch counters, and the compiled one must show that the interpreter
// never ran: "C++ -> VM", "VM -> AOT" and "AOT -> VM" are all 0. That is what
// strict AOT-only means operationally, and it is the assertion this test is
// for. The transcript is the control.
//
// The launcher is hand-written, not generated: generating C++ for it and
// shelling out to a host compiler at run time is deliberately avoided.
// Everything application-specific is in the two generated objects - the
// bodies and the table naming them - so this file differs between the two
// builds only by a preprocessor symbol.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/dispatch.hpp>
#include <ctbrowser/script/vm.hpp>

#ifdef CTCOMPILE_LAUNCHER_AOT
#include <ctcompile/AOT/EntryTable.hpp>

/// The one symbol a generated application exports. aot-entry-table.cmake
/// writes its definition; the name is a build parameter so that a build with
/// more than one compiled script has one table each.
extern const ctcompile::aot::entry_table CTCOMPILE_LAUNCHER_TABLE;
#endif

#include <cstdio>
#include <string>
#include <string_view>

using ctbrowser::script::context;
using ctbrowser::script::program;
using ctbrowser::script::transition;

namespace {

/// The same file the pipeline compiled, not a transcription of it (see
/// embed-js.cmake). This is what keeps the entry table from describing a
/// different program; see the note on what the bijection cannot see in
/// EntryTable.hpp.
constexpr std::string_view fixture =
#include "launcher.js.inc"
    ;

/// Prints every dispatch transition counter, the same way in both builds, so
/// application.test can FileCheck a fixed list. They go to stderr because
/// stdout is the application's output and is compared byte for byte; a counter
/// there would make the two builds differ by construction.
void report_transitions() {
    for (unsigned which = 0; which < static_cast<unsigned>(transition::count); ++which) {
        const auto one = static_cast<transition>(which);
        std::fprintf(stderr, "transition \"%s\" = %llu\n", ctbrowser::script::transition_name(one),
                     static_cast<unsigned long long>(ctbrowser::script::transitions(one)));
    }
}

} // namespace

int main() {
    program compiled = ctbrowser::script::compiler::compile(std::string(fixture));
    if (!compiled.ok) {
        std::fprintf(stderr, "launcher: the application did not compile: %s\n",
                     compiled.error.c_str());
        return 1;
    }

#ifdef CTCOMPILE_LAUNCHER_AOT
    // Install before anything runs, and refuse rather than fall back.
    //
    // `install_strict` is where "AOT-only" is enforced: a program still holding
    // a function with no compiled body is not one, and it does not start. The
    // alternative - installing what there is and running - produces an
    // application that works, is slower, and says nothing.
    const ctcompile::aot::install_report installed =
        ctcompile::aot::install_strict(compiled, CTCOMPILE_LAUNCHER_TABLE);
    if (!installed.ok()) {
        std::fprintf(stderr, "launcher: %s\n", installed.error.c_str());
        return 1;
    }
    std::fprintf(stderr, "installed %zu of %zu, interpreted %zu\n", installed.installed,
                 installed.functions, installed.interpreted);
#else
    std::fprintf(stderr, "installed 0 of %zu, interpreted %zu\n", compiled.functions.size(),
                 compiled.functions.size());
#endif

    context cx;
    ctbrowser::script::install_builtins(cx);

    // Reset after install_builtins, which runs no JavaScript today but might
    // in future. Resetting here means the counters describe the application
    // and nothing else, whatever the engine's own bootstrap grows into.
    ctbrowser::script::reset_transitions();

    const ctbrowser::script::run_result ran = cx.run(compiled);
    if (!ran.ok) {
        std::fprintf(stderr, "launcher: the application threw: %s\n", ran.error.c_str());
        report_transitions();
        return 1;
    }

    // The application's output, and only it, on stdout. `OUT` is a global the
    // program builds; reading it here rather than taking `run`'s returned value
    // is deliberate - a top level's value is the last expression statement's,
    // which would make the transcript depend on where `main()` sits in the
    // file.
    const std::string transcript = cx.to_string(cx.global("OUT"));
    std::fwrite(transcript.data(), 1, transcript.size(), stdout);

    // And it has to have run. If the top level threw before building anything,
    // `OUT` is empty, both builds print nothing, and the byte comparison in
    // application.test would pass on two empty files.
    if (transcript.empty()) {
        std::fprintf(stderr, "launcher: the application produced no transcript\n");
        report_transitions();
        return 1;
    }
    report_transitions();
    return 0;
}
