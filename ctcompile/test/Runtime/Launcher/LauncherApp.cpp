// A GENERATED APPLICATION, AND THE SAME APPLICATION INTERPRETED.
//
// Everything else in this directory tests a piece of the compiler. This is the
// first thing that is an APPLICATION: one JavaScript file, compiled to C++ by
// the real pipeline, linked into a native executable beside `ctbrowser::script`,
// with its compiled bodies installed onto the right `function_proto`s by the
// program itself at startup. Nothing here names a function, and nothing assigns
// `aot_entry` by hand - `Differential.cpp` does both, which is right for a
// harness driving forty-six bodies one case at a time and is not a mechanism
// anything could ship.
//
// ONE SOURCE, TWO EXECUTABLES. This file is compiled twice: once alone, and
// once with the generated bodies and the generated entry table linked in and
// `CTCOMPILE_LAUNCHER_AOT` defined. The two are then run and their stdout is
// compared byte for byte by `check-launcher.cmake`. That comparison is the
// correctness half.
//
// AND IT IS ONLY HALF, WHICH IS THE WHOLE DIFFICULTY OF THIS TEST. An
// application that silently ran everything interpreted would print exactly the
// same bytes and exit 0 - it is the same program either way. So both arms also
// report the dispatch counters, and the arm that matters must show that the
// interpreter NEVER RAN: `cxx->vm 0`, `vm->aot 0`, `aot->vm 0`. That is what
// strict AOT-only means operationally, and it is the assertion this test is
// for. The transcript is the control.
//
// THE LAUNCHER IS NOT GENERATED, which is Phase 18's own instruction: "Do not
// generate C++ source for the launcher and shell out to a host compiler."
// Everything application-specific is in the two generated objects - the bodies
// and the table naming them. This file varies in nothing but a preprocessor
// symbol, which is why it can be the same file twice.
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/dispatch.hpp>
#include <ctbrowser/script/vm.hpp>

#ifdef CTCOMPILE_LAUNCHER_AOT
#include <ctcompile/AOT/EntryTable.hpp>

// THE ONE SYMBOL A GENERATED APPLICATION EXPORTS. `aot-entry-table.cmake`
// writes its definition; the name is a build parameter so that a build with
// more than one compiled script has one table each.
extern const ctcompile::aot::entry_table CTCOMPILE_LAUNCHER_TABLE;
#endif

#include <cstdio>
#include <string>
#include <string_view>

using ctbrowser::script::context;
using ctbrowser::script::program;
using ctbrowser::script::transition;

namespace {

// THE SAME FILE THE PIPELINE COMPILED, not a transcription of it - the
// discipline `embed-js.cmake` exists for, and the one thing standing between
// this test and a table generated from a different program. See the note on
// what the bijection cannot see in EntryTable.hpp.
constexpr std::string_view fixture =
#include "launcher.js.inc"
    ;

// EVERY TRANSITION, PRINTED THE SAME WAY BY BOTH ARMS, so that the check script
// compares two lists rather than parsing prose. They go to stderr because
// stdout is the APPLICATION'S output and is compared byte for byte; a counter
// on it would make the two arms differ by construction.
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
    // INSTALL BEFORE ANYTHING RUNS, AND REFUSE RATHER THAN FALL BACK.
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

    // AFTER install_builtins, WHICH RUNS NO JAVASCRIPT TODAY AND MIGHT.
    // Resetting here means the counters describe the application and nothing
    // else, whatever the engine's own bootstrap grows into.
    ctbrowser::script::reset_transitions();

    const ctbrowser::script::run_result ran = cx.run(compiled);
    if (!ran.ok) {
        std::fprintf(stderr, "launcher: the application threw: %s\n", ran.error.c_str());
        report_transitions();
        return 1;
    }

    // THE APPLICATION'S OUTPUT, and only it, on stdout. `OUT` is a global the
    // program builds; reading it here rather than taking `run`'s returned value
    // is deliberate - a top level's value is the last expression statement's,
    // which would make the transcript depend on where `main()` sits in the
    // file.
    const std::string transcript = cx.to_string(cx.global("OUT"));
    std::fwrite(transcript.data(), 1, transcript.size(), stdout);

    // AND IT HAS TO HAVE RUN. An application whose top level threw before
    // building anything leaves `OUT` an empty string, both arms print nothing,
    // and the byte comparison in check-launcher.cmake passes on two empty
    // files. That is the vacuous pass this project keeps finding.
    if (transcript.empty()) {
        std::fprintf(stderr, "launcher: the application produced no transcript\n");
        report_transitions();
        return 1;
    }
    report_transitions();
    return 0;
}
