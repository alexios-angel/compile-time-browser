// THE TYPE ORACLE - ctcompile Phase 54B.
//
// The native backend (plan part 24) compiles a function to statically typed
// C++ only where it can PROVE the function is in the static subset, and every
// later phase consumes that proof. So the question that has to be answerable
// before any of them is written is: is the inference RIGHT, or merely
// plausible?
//
// The interpreter already knows. It has every value's real type at run time and
// it has been running the three corpora for months. So record what it sees, per
// (function, register), and any claim an analysis makes can be checked against
// reality instead of argued about.
//
// THIS FILE IS THE DRIVER AND THE PROOF THE INSTRUMENT WORKS. Three jobs:
//
//   1. A FIXTURE WITH HAND-COMPUTED COUNTERS. `oracle_probe`'s numbers below
//      were worked out on paper before the recorder was run, and the test
//      asserts them exactly. A recorder validated only on a corpus is a
//      recorder whose bugs are invisible, because nobody can say what the
//      right answer was.
//
//   2. A SECOND IMPLEMENTATION OF THE CHECKER. `tools/check/type-oracle.py`
//      reads the recording FILE; the code here walks the recorder IN MEMORY.
//      check-type-oracle.cmake runs both and compares every number. Two
//      independent implementations agreeing is a much stronger statement than
//      one implementation agreeing with itself, and it is the only thing that
//      can catch a bug in the file format - which sits between them.
//
//   3. RECORDING A CORPUS. `--page` runs a real HTML page through the whole
//      engine with a recorder installed; `--script` runs a bare bundle against
//      a context with builtins and no DOM.
//
// WHY THE STUB INFERENCES. Phase 54A does not exist, so there is nothing real
// to check. Two fakes stand in, and running them is the experiment:
//
//   all-i32     deliberately WRONG. It must produce violations and name them.
//               A checker that has never caught anything is not known to work.
//   all-boxed   deliberately trivial and sound by construction. It must
//               produce ZERO violations and ZERO precision.
//
// Those two runs are what makes the numbers on a real inference mean anything.
#include <ctbrowser.hpp>

#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/type_record.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::script::function_observation;
using ctbrowser::script::heap_kind;
using ctbrowser::script::obs_heap;
using ctbrowser::script::register_observation;
using ctbrowser::script::type_recorder;

namespace {

int failures = 0;

void check(bool ok, const char * what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

template <typename T> void check_eq(T got, T want, const char * what) {
    if (got != want) {
        std::printf("FAIL %s: got %llu, want %llu\n", what, static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
        ++failures;
    }
}

[[nodiscard]] std::string read_file(const std::filesystem::path & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// --- THE FIXTURE -------------------------------------------------------------
//
// Every number this test asserts is derivable from these fourteen lines without
// knowing anything about the register allocator, which is the point: the
// assertions are about PARAMETER registers and about entry counts, and
// parameters land in r0.. by the calling convention (vm/call.cpp fills
// registers[base + i] from the argument span before the first instruction
// runs). Nothing here depends on where the compiler happened to put a
// temporary, so a change to the allocator cannot silently rewrite the expected
// answer.
constexpr std::string_view oracle_probe = R"JS(
function ident(x) { return x; }
function two(a, b) { return a + b; }
function never_called(q) { return q * 2; }

// AN INSTRUCTION THAT THROWS BEFORE IT WRITES. `o.missing` is undefined and
// CALLING it is a TypeError, so `op::call_method`'s destination register is
// never written - it still holds whatever the frame started with. A recorder
// that flushes a def without checking that the def actually happened observes
// that stale slot and reports a type the program never put there.
//
// IT IS THE CALL AND NOT THE PROPERTY READ, which the first version of this
// fixture got wrong: `o.missing.deeper` on this engine answers undefined rather
// than raising, so the whole probe ran without ever throwing and the guard it
// was written to exercise was never reached. Both spellings are a TypeError in
// V8; only one of them is here.
function throws_before_writing(o) {
    try { return o.missing(); } catch (e) { return 0; }
}

// AND THE SAME THING ACROSS A CALL, which is the case that actually reaches the
// guard. `op::call` writes its result into a register of the CALLER, so the
// caller's pending def is armed while the callee runs; when the callee throws,
// the caller resumes at its handler instead of at the instruction after the
// call, and the register the call would have written was never written.
function throwing_callee(o) { return o.missing(); }
function catching_caller(o) {
    try { return throwing_callee(o); } catch (e) { return 0; }
}

ident(1);
ident("s");
ident(null);
ident(undefined);
ident(true);
ident(-0);
ident(0/0);
ident(1/0);
ident(3000000000);
ident(0.5);

two(1, 2);
two(1.5, 2);

throws_before_writing({});
catching_caller({});
)JS";

// The hand-computed expectations, written down before the recorder ran.
//
// `ident` is entered TEN times and its r0 holds, in order: 1, a string, null,
// undefined, true, -0, NaN, Infinity, 3000000000, 0.5. So r0's kind set is
// number|string|null|undefined|boolean - five bits - and its number shape has
// every one of the five ways a JS number fails to be an i32:
//
//   0.5           fractional
//   3000000000    integral and outside int32          wide
//   -0            integral, inside int32, still not an i32 because 1/-0 is
//                 -Infinity and 1/0 is +Infinity      negative-zero
//   0/0           NaN
//   1/0           infinite
//
// `two` is entered TWICE. Its r0 holds 1 then 1.5 - a number, fractional and
// nothing else. Its r1 holds 2 then 2 - a number with NO not-i32 flag at all,
// which is the only register in the fixture an honest inference could call an
// i32, and therefore the one that separates a checker that measures from a
// checker that always says no.
//
// `never_called` is never called. Its registers have NO observation, and that
// is not the same as having been observed to hold anything.
constexpr std::uint64_t ident_entries = 10;
constexpr std::uint64_t two_entries = 2;

[[nodiscard]] const function_observation * find(const type_recorder & rec, std::string_view name) {
    for (const function_observation & fn : rec.functions()) {
        if (fn.name == name) { return &fn; }
    }
    return nullptr;
}

// --- THE CHECKER, AGAIN ------------------------------------------------------
//
// A second implementation of tools/check/type-oracle.py, over the recorder's
// own memory rather than over the file it writes. check-type-oracle.cmake runs
// both and compares. Kept deliberately short and deliberately not shared: two
// implementations that share their arithmetic are one implementation.
struct tally {
    std::uint64_t observed = 0;
    std::uint64_t unobserved = 0;
    std::uint64_t violations = 0;
    std::uint64_t beat_boxed = 0;
};

// Is a claim of "this register is an i32" consistent with what was seen?
// Only if the register held numbers and NOTHING else, and every number in it
// was an i32-shaped one.
[[nodiscard]] bool i32_covers(const register_observation & obs) {
    return obs.kinds == ctbrowser::script::obs_number &&
           (obs.numbers & ctbrowser::script::num_not_i32) == 0;
}

[[nodiscard]] tally check_all_i32(const type_recorder & rec) {
    tally out;
    for (const function_observation & fn : rec.functions()) {
        for (const register_observation & obs : fn.regs) {
            if (!obs.observed()) {
                ++out.unobserved;
                continue;
            }
            ++out.observed;
            ++out.beat_boxed; // `i32` is narrower than boxed everywhere
            if (!i32_covers(obs)) { ++out.violations; }
        }
    }
    return out;
}

[[nodiscard]] tally check_all_boxed(const type_recorder & rec) {
    tally out;
    for (const function_observation & fn : rec.functions()) {
        for (const register_observation & obs : fn.regs) {
            if (obs.observed()) {
                ++out.observed;
            } else {
                ++out.unobserved;
            }
        }
    }
    // Boxed admits everything, so there is nothing to violate and nothing beaten.
    return out;
}

void report(const char * which, const tally & t) {
    std::printf("%s observed %llu unobserved %llu violations %llu beat-boxed %llu\n", which,
                static_cast<unsigned long long>(t.observed),
                static_cast<unsigned long long>(t.unobserved),
                static_cast<unsigned long long>(t.violations),
                static_cast<unsigned long long>(t.beat_boxed));
}

// --- the self-test -----------------------------------------------------------

int self_test(const char * out_path) {
    // THE HOOK MUST BE COMPILED IN, and this asks rather than guessing.
    //
    // Without it the recording below is empty and every count is a truthful
    // zero about a build that measured nothing - which is exactly the vacuous
    // pass this project keeps finding. So it SKIPS, loudly, on the same terms
    // as unittests/unit/script_debug does for CTBROWSER_SCRIPT_DEBUG_NAMES: the
    // option exists for a build that will never record, and turning that build
    // into a red suite would make the option unusable. A skip that says which
    // build it is in is not the same thing as a pass.
    if (!ctbrowser::script::type_recording_enabled()) {
        std::printf("ok type_oracle (SKIPPED - built with CTBROWSER_SCRIPT_RECORD_TYPES=0, "
                    "so the interpreter has no recording hook to check)\n");
        return 0;
    }

    // 68 OF 93, counted from bytecode_opcodes.def's own writes_a column rather
    // than written down. If a new opcode arrives this number changes and the
    // change should be deliberate: a definer the recorder does not know about
    // is a register whose type it silently never sees.
    static_assert(ctbrowser::script::opcode_count == 93,
                  "the opcode count moved - re-derive the writer count below");
    static_assert(ctbrowser::script::opcode_writer_count == 68,
                  "the set of register-defining opcodes changed; confirm the recorder still "
                  "sees every def and update this number deliberately");

    type_recorder rec;
    ctbrowser::script::set_active_type_recorder(&rec);
    {
        ctbrowser::script::context cx;
        ctbrowser::script::install_builtins(cx);
        const ctbrowser::script::program prog =
            ctbrowser::script::compiler::compile(oracle_probe);
        check(prog.ok, "the fixture compiles");
        if (!prog.ok) {
            std::printf("       %s\n", prog.error.c_str());
            return 1;
        }
        const ctbrowser::script::run_result result = cx.run(prog);
        check(result.ok, "the fixture runs");
        if (!result.ok) { std::printf("       %s\n", result.error.c_str()); }
    }
    ctbrowser::script::set_active_type_recorder(nullptr);

    // --- the hand-computed numbers ------------------------------------------
    const function_observation * ident = find(rec, "ident");
    const function_observation * two = find(rec, "two");
    const function_observation * never = find(rec, "never_called");
    check(ident != nullptr, "the recording has `ident`");
    check(two != nullptr, "the recording has `two`");
    check(never != nullptr, "the recording has `never_called`");
    if (ident == nullptr || two == nullptr || never == nullptr) { return 1; }

    check_eq(ident->entries, ident_entries, "ident entered ten times");
    check_eq(two->entries, two_entries, "two entered twice");
    check_eq(never->entries, std::uint64_t{0}, "never_called was never entered");

    check(!ident->regs.empty(), "ident has registers");
    const register_observation & x = ident->regs[0];
    check_eq(x.defs, ident_entries, "ident r0 defined once per call");
    check_eq(x.kinds,
             ctbrowser::script::obs_number | ctbrowser::script::obs_null |
                 ctbrowser::script::obs_undefined | ctbrowser::script::obs_boolean |
                 obs_heap(heap_kind::string),
             "ident r0 held a number, a string, null, undefined and a boolean");
    check_eq(x.numbers,
             ctbrowser::script::num_seen | ctbrowser::script::num_fractional |
                 ctbrowser::script::num_wide | ctbrowser::script::num_negative_zero |
                 ctbrowser::script::num_nan | ctbrowser::script::num_infinite,
             "ident r0's numbers were every shape an i32 cannot hold");

    check(two->regs.size() >= 2, "two has two parameter registers");
    if (two->regs.size() >= 2) {
        check_eq(two->regs[0].defs, two_entries, "two r0 defined once per call");
        check_eq(two->regs[0].kinds, ctbrowser::script::obs_number, "two r0 is only ever a number");
        check_eq(two->regs[0].numbers,
                 ctbrowser::script::num_seen | ctbrowser::script::num_fractional,
                 "two r0 saw 1 and 1.5, so fractional and nothing else");
        check_eq(two->regs[1].defs, two_entries, "two r1 defined once per call");
        check_eq(two->regs[1].kinds, ctbrowser::script::obs_number, "two r1 is only ever a number");
        // THE REGISTER THAT MAKES THE PRECISION NUMBER MEAN SOMETHING. If the
        // checker cannot say `i32` about this one it cannot say it about
        // anything, and "zero soundness violations" would be the trivial
        // consequence of never claiming anything.
        check_eq(two->regs[1].numbers, ctbrowser::script::num_seen,
                 "two r1 saw 2 twice - an i32, with no disqualifying shape");
        check(i32_covers(two->regs[1]), "two r1 is a register `i32` honestly covers");
    }

    // NEVER EXECUTED IS NOT `ANY TYPE`. Every register of a function nothing
    // called must be absent from the observations, not present and empty.
    for (const register_observation & obs : never->regs) {
        check(!obs.observed(), "never_called's registers have no observation");
    }
    check(!never->regs.empty(), "never_called has registers to be unobserved");

    // --- THE `expect_pc` GUARD, WHICH THIS IS THE ONLY TEST OF ---------------
    //
    // An instruction that threw did not write its destination, so the deferred
    // flush must not observe it. The recorder enforces that by requiring that
    // the next instruction executed in the frame is the one AFTER the def, and
    // `catching_caller` above is written to violate it: the callee throws, the
    // caller resumes at its handler rather than at the instruction after the
    // call, and the register the call would have written was never written.
    //
    // WHAT THIS ACTUALLY CATCHES, said honestly, because it is less than it
    // looks. Deleting the guard changes the `dropped` counter to zero and the
    // affected registers' `defs` from one to two - and changes NO observed type
    // set, here or on the bootstrap corpus (41 drops, identical types either
    // way). The reason is structural: a stale register holds a value that
    // register legitimately held EARLIER, so re-observing it re-adds a type the
    // set already has. The guard is still right - a def that did not happen is
    // not an observation - but the case where it would change an answer is one
    // no fixture in this file reaches, and pretending otherwise would be the
    // kind of claim this project keeps finding to be false.
    check(rec.dropped_defs() > 0, "the throwing fixture reached the expect_pc guard");
    if (const function_observation * caller = find(rec, "catching_caller"); caller != nullptr) {
        for (const register_observation & obs : caller->regs) {
            check(obs.defs <= 1, "no register of catching_caller was defined twice");
        }
    }

    // --- the two stub inferences --------------------------------------------
    const tally wrong = check_all_i32(rec);
    const tally trivial = check_all_boxed(rec);
    report("all-i32", wrong);
    report("all-boxed", trivial);

    // THE DELIBERATELY WRONG ONE MUST BE CAUGHT. `ident` r0 alone held five
    // different kinds; a claim of `i32` over it is a defect and the tool has to
    // say so. If this is ever zero the checker has stopped checking.
    check(wrong.violations > 0, "all-i32 is caught");
    check_eq(trivial.violations, std::uint64_t{0}, "all-boxed is sound");
    check_eq(trivial.beat_boxed, std::uint64_t{0}, "all-boxed has zero precision");
    check_eq(wrong.observed, trivial.observed, "both stubs see the same observed registers");
    check_eq(wrong.unobserved, trivial.unobserved, "both stubs see the same unobserved registers");
    check(wrong.observed > 0, "something was observed");
    check(wrong.unobserved > 0, "something was not");

    std::printf("recorder defs recorded %llu dropped %llu orphan-frames %llu\n",
                static_cast<unsigned long long>(rec.recorded_defs()),
                static_cast<unsigned long long>(rec.dropped_defs()),
                static_cast<unsigned long long>(rec.orphan_frames()));

    if (out_path != nullptr) {
        if (!rec.write(out_path)) {
            std::printf("FAIL cannot write %s\n", out_path);
            ++failures;
        }
    }

    if (failures == 0) { std::printf("ok type_oracle\n"); }
    return failures == 0 ? 0 : 1;
}

// --- corpus recording --------------------------------------------------------

int record_page(const std::string & html_path, const std::string & assets, int frames,
                const std::vector<std::string> & extra_js, const std::string & out_path) {
    const std::string html = read_file(html_path);
    if (html.empty()) {
        std::fprintf(stderr, "type-oracle: cannot read %s\n", html_path.c_str());
        return 1;
    }
    type_recorder rec;
    // BEFORE THE BROWSER IS BUILT. `shell::browser` makes its own context and
    // never hands it out, and a context picks the active recorder up when it is
    // constructed - so the order of these two lines is the whole mechanism.
    ctbrowser::script::set_active_type_recorder(&rec);
    {
        ctbrowser::browser page{ctbrowser::browser_options{800, 600}};
        if (!assets.empty()) { page.assets().set_base_path(assets); }
        page.load_html(html);
        if (!page.script_error().empty()) {
            std::fprintf(stderr, "type-oracle: %s reported %s\n", html_path.c_str(),
                         page.script_error().c_str());
        }
        // SCRIPTS THE PAGE DOES NOT LOAD ITSELF. bootstrap.bundle.js is
        // vendored and no example page runs it - bootstrap-components.html says
        // so in its own comment, because a component that needs JS to be
        // visible cannot be compared against Chrome. That makes the markup a
        // perfect fixture and leaves the bundle unexecuted, so the recording
        // driver supplies it here rather than a near-duplicate page being
        // checked in to run it.
        for (const std::string & js : extra_js) {
            const std::string source = read_file(js);
            if (source.empty()) {
                std::fprintf(stderr, "type-oracle: cannot read %s\n", js.c_str());
                return 1;
            }
            (void)page.run_script(source);
            if (!page.script_error().empty()) {
                std::fprintf(stderr, "type-oracle: %s reported %s\n", js.c_str(),
                             page.script_error().c_str());
            }
        }
        // A FIXED CLOCK, for the same reason every other harness here uses one:
        // `Math.random` is seeded and deterministic, so a recording is
        // reproducible only as long as nothing else in the run is not.
        for (int i = 0; i < frames; ++i) {
            page.tick(16.0);
            (void)page.frame();
        }
    }
    ctbrowser::script::set_active_type_recorder(nullptr);
    if (!rec.write(out_path)) {
        std::fprintf(stderr, "type-oracle: cannot write %s\n", out_path.c_str());
        return 1;
    }
    std::fprintf(stderr, "recorded %llu defs (%llu dropped, %llu orphan frames) to %s\n",
                 static_cast<unsigned long long>(rec.recorded_defs()),
                 static_cast<unsigned long long>(rec.dropped_defs()),
                 static_cast<unsigned long long>(rec.orphan_frames()), out_path.c_str());
    return 0;
}

int record_script(const std::string & js_path, const std::string & out_path) {
    const std::string source = read_file(js_path);
    if (source.empty()) {
        std::fprintf(stderr, "type-oracle: cannot read %s\n", js_path.c_str());
        return 1;
    }
    type_recorder rec;
    ctbrowser::script::set_active_type_recorder(&rec);
    {
        ctbrowser::script::context cx;
        ctbrowser::script::install_builtins(cx);
        const ctbrowser::script::program prog = ctbrowser::script::compiler::compile(source);
        if (!prog.ok) {
            std::fprintf(stderr, "type-oracle: %s does not compile: %s\n", js_path.c_str(),
                         prog.error.c_str());
            return 1;
        }
        const ctbrowser::script::run_result result = cx.run(prog);
        if (!result.ok) {
            std::fprintf(stderr, "type-oracle: %s: %s\n", js_path.c_str(), result.error.c_str());
        }
    }
    ctbrowser::script::set_active_type_recorder(nullptr);
    if (!rec.write(out_path)) {
        std::fprintf(stderr, "type-oracle: cannot write %s\n", out_path.c_str());
        return 1;
    }
    std::fprintf(stderr, "recorded %llu defs (%llu dropped, %llu orphan frames) to %s\n",
                 static_cast<unsigned long long>(rec.recorded_defs()),
                 static_cast<unsigned long long>(rec.dropped_defs()),
                 static_cast<unsigned long long>(rec.orphan_frames()), out_path.c_str());
    return 0;
}

} // namespace

int main(int argc, char ** argv) {
    std::string page;
    std::string script;
    std::string assets;
    std::string out;
    std::vector<std::string> extra_js;
    int frames = 20;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag{argv[i]};
        const auto next = [&]() -> std::string {
            return i + 1 < argc ? std::string{argv[++i]} : std::string{};
        };
        if (flag == "--page") {
            page = next();
        } else if (flag == "--script") {
            script = next();
        } else if (flag == "--assets") {
            assets = next();
        } else if (flag == "--js") {
            extra_js.push_back(next());
        } else if (flag == "--out") {
            out = next();
        } else if (flag == "--frames") {
            frames = std::atoi(next().c_str());
        } else {
            std::fprintf(stderr,
                         "usage: type-oracle [--out FILE]\n"
                         "       type-oracle --page PAGE.html [--assets DIR] [--frames N] "
                         "[--js EXTRA.js ...] --out FILE\n"
                         "       type-oracle --script BUNDLE.js --out FILE\n");
            return 2;
        }
    }
    if (!page.empty() || !script.empty()) {
        if (out.empty()) {
            std::fprintf(stderr, "type-oracle: --page and --script need --out\n");
            return 2;
        }
        return page.empty() ? record_script(script, out)
                            : record_page(page, assets, frames, extra_js, out);
    }
    return self_test(out.empty() ? nullptr : out.c_str());
}
