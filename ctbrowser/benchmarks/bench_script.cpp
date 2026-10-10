//===- ctbrowser/benchmarks/bench_script.cpp - Script VM benchmark --------===//
//
// Measures what the script VM costs to run, separately from what compiling
// JavaScript costs.
//
// The other benchmarks measure page loads, where running JavaScript is only
// about 1.4% of the time (docs/performance.md): a page load spends an order of
// magnitude more parsing script than running it. Game loops and per-frame
// rendering (Phaser Space Invaders, Babylon, p5's draw()) keep running the VM
// long after load, so this benchmark measures it on its own.
//
// Two numbers per workload:
//
//   compile   source -> bytecode, the cost that dominates a page load, so the
//             two can be compared.
//   run       bytecode -> result, on a program compiled once: dispatch plus the
//             opcode handlers (docs/history/computed-goto.md uses it).
//
// The workloads avoid allocation on purpose. A loop that builds strings or
// objects measures the garbage collector instead; these use arithmetic, locals,
// property reads on one fixed shape, calls and branches, the opcodes a
// frame-driven page executes most.
//
// Not a ctest test, because the numbers vary with the machine. `--csv` prints
// one row per workload for scripting.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/script/script.hpp>

#include "options.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace ctbrowser;
using clock_type = std::chrono::steady_clock;

namespace {

struct workload {
    const char * name;
    const char * what; // Which opcodes this workload is meant to exercise.
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

// Report the minimum of `repeats` runs, not the mean. Run-to-run noise was
// measured at +-10%, enough to fake a 5% improvement; the minimum is the run
// least disturbed by the rest of the machine, and is what the other benchmarks
// report too.
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

        // A fresh context per run, so no run inherits another's heap and later
        // runs do not measure a fuller collector.
        //
        // The standard library is installed before the timer starts: it is
        // setup, not dispatch, but it is required (without it `[].push` does
        // not exist and the `index` workload throws a TypeError).
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
    namespace po = boost::program_options;
    bool csv = false;
    po::options_description options{"usage: ctbrowser-test-bench_script [--csv]"};
    options.add_options()("csv", po::bool_switch(&csv), "one row per workload, for scripting");
    if (!ctbrowser_cli::parse_options(argc, argv, options)) { return 2; }

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
