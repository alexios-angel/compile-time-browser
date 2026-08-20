// What the script VM costs, separated from what READING JavaScript costs.
//
// WHY THIS DID NOT EXIST UNTIL NOW, and why that was a hole. Six benchmarks
// cover core, style, layout, raster, interaction and the GPU; none covered the
// VM. So the only number anyone had for `context::run_loop` came from
// docs/performance.md's whole-page profile, where it was 1.4% - and 1.4% of a
// PAGE LOAD says almost nothing about a game loop, because showing a page costs
// an order of magnitude more in parsing JavaScript than in running it.
//
// The corpora in this tree no longer stop at load. Phaser Space Invaders runs a
// game loop, Babylon renders per frame, p5's draw() runs for ever. This
// measures the half that keeps running.
//
// WHAT IS MEASURED, and the split is the point:
//
//   compile   source -> bytecode. The cost docs/performance.md found dominating
//             a page load, here so the two can be compared honestly rather than
//             argued about.
//   run       bytecode -> answer, on a program compiled ONCE. This is dispatch
//             plus the handlers, and it is the number docs/history/computed-goto.md
//             needs.
//
// THE WORKLOADS AVOID THE ALLOCATOR ON PURPOSE. A loop that builds strings or
// objects measures the garbage collector, which is a different question with a
// different answer; these are arithmetic, locals, property reads on one fixed
// shape, calls, and branches - the opcodes a frame-driven page actually
// retires in bulk.
//
// Not a ctest gate: the numbers move with the machine. Run it, read it, decide.
// `--csv` prints one row per workload for scripting.

#include <ctbrowser/script/script.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ctbrowser;
using clock_type = std::chrono::steady_clock;

namespace {

struct workload {
    const char * name;
    const char * what; // which opcodes this is meant to lean on
    const char * source;
};

// Each returns a number, so nothing can be optimised away as dead, and each
// runs long enough that timer resolution is not the measurement.
const workload workloads[] = {
    {"arith", "add/sub/mul, jump_if_false, load_const",
     "let a = 0;"
     "for (let i = 0; i < 300000; i++) { a = a + i * 2 - (i / 3); }"
     "return a;"},
    {"locals", "move, get/set of registers, no property lookup",
     "let a = 1, b = 2, c = 3, d = 4;"
     "for (let i = 0; i < 300000; i++) { const t = a; a = b; b = c; c = d; d = t + 1; }"
     "return d;"},
    {"property", "get_prop/set_prop on ONE fixed shape - the inline-cache site",
     "const o = {x: 1, y: 2, z: 3};"
     "let a = 0;"
     "for (let i = 0; i < 300000; i++) { o.x = o.y + o.z; a += o.x; }"
     "return a;"},
    {"index", "get_index/set_index over a dense array, no growth",
     "const xs = [];"
     "for (let i = 0; i < 256; i++) { xs.push(i); }"
     "let a = 0;"
     "for (let i = 0; i < 300000; i++) { const k = i & 255; xs[k] = xs[k] + 1; a += xs[k]; }"
     "return a;"},
    {"calls", "call/ret and the frame push-pop the dispatch loop reloads around",
     "function add(a, b) { return a + b; }"
     "let a = 0;"
     "for (let i = 0; i < 300000; i++) { a = add(a, 1); }"
     "return a;"},
    {"method", "call_method - the receiver path, through a prototype",
     "class Counter { constructor() { this.n = 0; } bump(by) { this.n += by; return this.n; } }"
     "const c = new Counter();"
     "for (let i = 0; i < 200000; i++) { c.bump(1); }"
     "return c.n;"},
    {"branches", "jump_if_true/false and comparisons, deliberately UNPREDICTABLE",
     // A branch the predictor cannot learn, which is exactly the case a
     // computed-goto interpreter is supposed to help with: the dispatch after
     // an unpredictable branch is itself unpredictable in a switch.
     "let s = 12345, a = 0;"
     "for (let i = 0; i < 300000; i++) {"
     "  s = (s * 1103515245 + 12345) & 2147483647;"
     "  if ((s & 1) === 0) { a += 1; } else if ((s & 2) === 0) { a += 2; } else { a -= 1; }"
     "}"
     "return a;"},
};

[[nodiscard]] double seconds_of(clock_type::duration d) {
    return std::chrono::duration<double>(d).count();
}

// MIN OF N, not mean. This machine was measured at +-10% run to run, which is
// how a 5% "improvement" gets believed; the minimum is the run least disturbed
// by everything else on the box and is what the rest of this tree reports.
constexpr int repeats = 7;

struct timing {
    double compile_seconds = 0;
    double run_seconds = 0;
    std::string answer;
};

[[nodiscard]] timing measure(const workload & w) {
    timing best;
    best.compile_seconds = 1e30;
    best.run_seconds = 1e30;
    for (int i = 0; i < repeats; ++i) {
        const auto compile_start = clock_type::now();
        script::program program = script::compiler::compile(w.source);
        const auto compile_end = clock_type::now();
        if (!program.ok) {
            best.answer = "COMPILE ERROR: " + program.error;
            return best;
        }

        // A FRESH CONTEXT PER RUN, so no run inherits another's heap and the
        // later ones do not measure a fuller collector.
        //
        // THE STANDARD LIBRARY IS INSTALLED BEFORE THE TIMER STARTS. It is
        // setup, not dispatch - and leaving it out entirely is not an option:
        // without it `[].push` does not exist, which is how the `index`
        // workload first reported a TypeError instead of a time.
        script::context cx;
        script::install_builtins(cx);
        const auto run_start = clock_type::now();
        const script::run_result out = cx.run(program);
        const auto run_end = clock_type::now();

        best.compile_seconds =
            std::min(best.compile_seconds, seconds_of(compile_end - compile_start));
        best.run_seconds = std::min(best.run_seconds, seconds_of(run_end - run_start));
        best.answer = out.ok ? cx.to_string(out.returned) : "RUN ERROR: " + out.error;
    }
    return best;
}

} // namespace

int main(int argc, char ** argv) {
    const bool csv = argc > 1 && std::strcmp(argv[1], "--csv") == 0;

    if (csv) {
        std::printf("workload,compile_ms,run_ms,run_share\n");
    } else {
        std::printf("script VM benchmark - min of %d, times in ms\n\n", repeats);
        std::printf("%-10s %10s %10s %8s  %s\n", "workload", "compile", "run", "run%", "answer");
        std::printf("%-10s %10s %10s %8s  %s\n", "--------", "-------", "---", "----", "------");
    }

    double total_compile = 0, total_run = 0;
    for (const workload & w : workloads) {
        const timing t = measure(w);
        const double both = t.compile_seconds + t.run_seconds;
        const double share = both > 0 ? t.run_seconds / both * 100.0 : 0.0;
        total_compile += t.compile_seconds;
        total_run += t.run_seconds;
        if (csv) {
            std::printf("%s,%.3f,%.3f,%.1f\n", w.name, t.compile_seconds * 1e3, t.run_seconds * 1e3,
                        share);
        } else {
            std::printf("%-10s %10.3f %10.3f %7.1f%%  %s\n", w.name, t.compile_seconds * 1e3,
                        t.run_seconds * 1e3, share, t.answer.c_str());
        }
    }

    if (!csv) {
        const double both = total_compile + total_run;
        std::printf("\n%-10s %10.3f %10.3f %7.1f%%\n", "TOTAL", total_compile * 1e3,
                    total_run * 1e3, both > 0 ? total_run / both * 100.0 : 0.0);
        std::printf("\nwhat each leans on:\n");
        for (const workload & w : workloads) { std::printf("  %-10s %s\n", w.name, w.what); }
        std::printf("\nRUN%% IS NOT THE DISPATCH SHARE. It is running against compiling for\n"
                    "this source, and these sources are deliberately tiny and loop hard -\n"
                    "the opposite balance from a page load, where docs/performance.md\n"
                    "measured the interpreter at 1.4%%. For the dispatch share inside the\n"
                    "run column, profile it:\n"
                    "  valgrind --tool=callgrind --callgrind-out-file=cg.out ./bench_script\n"
                    "  callgrind_annotate cg.out | head -30\n");
    }
    return 0;
}
