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
//
// AND SINCE PHASE 55O THE SAME THREE JOBS FOR THE ESCAPE ORACLE, under
// `--escape`: a second probe with hand-computed made/confined/escaped counts
// per site, a second in-memory checker mirroring tools/check/escape-oracle.py,
// and two stubs - `all-confined` (wrong, must be caught and NAMED) and
// `all-escapes` (sound, zero precision). One recorder, one file: the escape
// half rides in the same recording, header version 2.
#include <ctbrowser.hpp>

#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/type_record.hpp>
#include <ctbrowser/script/vm.hpp>

#include <array>
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
using ctbrowser::script::root_label;
using ctbrowser::script::site_observation;
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
        const ctbrowser::script::program prog = ctbrowser::script::compiler::compile(oracle_probe);
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

// =============================================================================
// THE ESCAPE ORACLE - Phase 55O.
// =============================================================================

// --- THE FRAME-END TABLE HAS THE SHAPE THE ORACLE WAS BUILT AGAINST -----------
//
// FrameEnds.def lists every site that truncates `frames_`. Eight rows, two of
// them hooked - `ret` and `unwind`. A ninth truncation site added to the VM
// without a row here is exactly the hole the table exists to make visible, so
// this is asserted rather than trusted; if the count moves, decide whether the
// new path is a hook or an UNCHECKED reason and add the row.
namespace frame_ends {
struct row {
    const char * name;
    const char * where;
    int hooked;
    const char * note;
};
constexpr row table[] = {
#define CT_FRAME_END(name_, where_, hooked_, note_) row{#name_, where_, hooked_, note_},
#include <ctcompile/JavaScript/FrameEnds.def>
#undef CT_FRAME_END
};
constexpr std::size_t rows = std::size(table);
constexpr std::size_t hooked = [] {
    std::size_t n = 0;
    for (const row & r : table) {
        if (r.hooked != 0) { ++n; }
    }
    return n;
}();
static_assert(rows == 8, "FrameEnds.def must list the eight frames_ truncation sites - a ninth "
                         "needs a row, and a decision: hook it, or name its UNCHECKED reason");
static_assert(hooked == 2, "exactly `ret` and `unwind` are hooked in the MVP");
static_assert(std::string_view{table[0].name} == "ret" &&
                  std::string_view{table[1].name} == "unwind",
              "the two hooked rows lead the table");
} // namespace frame_ends

// --- THE PROBE ---------------------------------------------------------------
//
// EVERY NUMBER BELOW WAS COMPUTED FROM THE JAVASCRIPT BEFORE THE RECORDER RAN,
// and each is derivable without knowing the register allocator: the question
// is only "is the object still reachable from a root when the function that
// made it returns or throws", which is a fact about the program. That is what
// makes a recorder bug VISIBLE - a recorder validated only on a corpus is a
// recorder whose bugs nobody can name, because nobody can say what the right
// answer was.
constexpr std::string_view escape_probe = R"JS(
var G = null;
var H = [];
var T = null;

// CONFINED: made, used, dropped.
function mk() { var o = {x: 1}; return o.x; }
function arr() { var a = [1, 2, 3]; var s = 0; for (var i = 0; i < a.length; i++) { s += a[i]; } return s; }

// ESCAPED, one route each.
function ret() { return {}; }                       // temporaries: the in-flight return value
function glob() { G = {}; }                         // globals
function hold(x) { H.push(x); }
function retain() { var o = {}; hold(o); }          // globals, through a callee that retained it

// THE BLIND SPOT, PINNED. `ident` receives the object and drops it before
// `transit` returns, so the oracle sees it confined - while the analysis says
// escapes:passed, and is right to for a by-value lowering. Retention at frame
// exit is not transit.
function ident(x) { return x; }
function transit() { var o = {}; ident(o); return 1; }

// THROWN out of its frame and caught by the caller, which stores it.
function thrower() { throw {e: 1}; }
function catcher() { try { thrower(); } catch (e) { T = e; } }

// A closure and the cell it captures: all three leave with the closure...
function counter() { var n = {v: 0}; return function () { return n.v++; }; }
// ...and all three stay when the closure is only ever called locally.
function local() { var n = {v: 0}; var f = function () { return n.v++; }; f(); return f(); }

// `arguments` is an array the prologue builds; it dies with the frame too.
function args() { var o = {}; return arguments.length; }

// A cycle that stays, and the same cycle returned.
function cyc() { var a = {}; var b = {}; a.b = b; b.a = a; return 0; }
function leak() { var a = {}; var b = {}; a.b = b; b.a = a; return a; }

// Entered twice: made 2. Never entered: no observation at all.
function twice() { var o = {}; return 0; }
function never_called() { var o = {}; return o; }

// THE BOUNDED-WALK ROW. The caller holds `kept` in its own register window
// across the call; the callee stores its object INTO it. At the callee's
// return the caller's window lies BELOW the callee's base, so the walk must
// still reach `kept` and report the callee's object escaped `via registers`.
// A bound that excluded too much would read it confined.
function fills(target) { var t = {tmp: 1}; target.child = t; return 0; }
function holds_across() { var kept = {k: 1}; fills(kept); return kept.child.tmp; }

// AN UNCLAIMED SITE. Object.keys is a native; the array it makes lands on the
// CALL's pc, which no static inventory names - so it is observed (confined),
// never claimed, and reported as such rather than as anything sound.
function unclaimed_site() { var o = {a: 1}; var k = Object.keys(o); return k.length; }

mk(); arr(); ret(); glob(); retain(); transit(); catcher(); counter(); local();
args(1, 2); cyc(); leak(); twice(); twice(); holds_across(); unclaimed_site();
)JS";

// The kind-summed tally of one function's sites, which is what the rows above
// are stated in terms of.
struct kind_sum {
    std::size_t sites = 0;
    std::uint64_t made = 0;
    std::uint64_t confined = 0;
    std::uint64_t escaped = 0;
    std::uint64_t unresolved = 0;
    std::uint64_t unchecked = 0;
    std::array<std::uint64_t, ctbrowser::script::root_label_count> routes{};
};

[[nodiscard]] kind_sum sum_kind(const std::vector<site_observation> & sites, heap_kind kind) {
    kind_sum out;
    for (const site_observation & s : sites) {
        if (s.kind != kind) { continue; }
        ++out.sites;
        out.made += s.made;
        out.confined += s.confined;
        out.escaped += s.escaped;
        out.unresolved += s.unresolved;
        out.unchecked += s.unchecked;
        for (std::size_t l = 0; l < out.routes.size(); ++l) { out.routes[l] += s.routes[l]; }
    }
    return out;
}

[[nodiscard]] std::size_t index_of(const type_recorder & rec, std::string_view name) {
    const std::vector<function_observation> & fns = rec.functions();
    for (std::size_t i = 0; i < fns.size(); ++i) {
        if (fns[i].name == name) { return i; }
    }
    return fns.size();
}

// One hand-computed row: function, kind, sites, made/confined/escaped, and the
// one route every escape took (or none).
void expect_row(const type_recorder & rec, const std::vector<std::vector<site_observation>> & all,
                const char * fn, heap_kind kind, std::size_t sites, std::uint64_t made,
                std::uint64_t confined, std::uint64_t escaped, const char * route) {
    const std::size_t i = index_of(rec, fn);
    std::string what = std::string{fn} + " " + std::string{ctbrowser::script::site_kind_name(kind)};
    if (i == rec.functions().size()) {
        std::printf("FAIL %s: no such function in the recording\n", what.c_str());
        ++failures;
        return;
    }
    const kind_sum got = sum_kind(all[i], kind);
    check_eq(got.sites, sites, (what + " sites").c_str());
    check_eq(got.made, made, (what + " made").c_str());
    check_eq(got.confined, confined, (what + " confined").c_str());
    check_eq(got.escaped, escaped, (what + " escaped").c_str());
    check_eq(got.unresolved, std::uint64_t{0}, (what + " unresolved").c_str());
    check_eq(got.unchecked, std::uint64_t{0}, (what + " unchecked").c_str());
    for (std::size_t l = 0; l < got.routes.size(); ++l) {
        const bool expected = route != nullptr && ctbrowser::script::root_label_names[l] == route;
        check_eq(got.routes[l], expected ? escaped : std::uint64_t{0},
                 (what + " route " + std::string{ctbrowser::script::root_label_names[l]}).c_str());
    }
}

// --- THE CHECKER, AGAIN - the escape verdict table, in memory -----------------
//
// The second implementation of tools/check/escape-oracle.py, over the
// recorder's memory rather than the file. Same rule as the type half: short,
// and deliberately not shared.
struct escape_tally {
    std::uint64_t claimed = 0;
    std::uint64_t observed = 0;
    std::uint64_t unobserved = 0;
    std::uint64_t violations = 0;
    std::uint64_t sound = 0;
    std::uint64_t partial = 0;
    std::uint64_t pending = 0;
    std::uint64_t imprecise = 0;
    std::uint64_t exact = 0;
    std::uint64_t unclaimed = 0;
    std::uint64_t inconclusive = 0;
    std::uint64_t mismatch = 0;
};

[[nodiscard]] escape_tally check_escape_stub(const type_recorder & rec,
                                             const std::vector<std::vector<site_observation>> & all,
                                             bool claim_confined) {
    escape_tally out;
    const std::vector<function_observation> & fns = rec.functions();
    for (std::size_t i = 0; i < fns.size(); ++i) {
        const function_observation & fn = fns[i];
        const std::vector<site_observation> & sites = all[i];
        out.observed += sites.size();
        std::vector<bool> claimed(sites.size(), false);
        for (const ctbrowser::script::static_site & a : fn.allocs) {
            ++out.claimed;
            const site_observation * site = nullptr;
            bool same_pc = false;
            for (std::size_t s = 0; s < sites.size(); ++s) {
                if (sites[s].pc != a.pc) { continue; }
                if (sites[s].kind == a.kind) {
                    site = &sites[s];
                    claimed[s] = true;
                } else {
                    same_pc = true;
                }
            }
            if (site == nullptr) {
                if (same_pc) {
                    ++out.mismatch;
                } else {
                    ++out.unobserved;
                }
                continue;
            }
            const bool clean = site->unresolved == 0 && site->unchecked == 0;
            if (claim_confined) {
                if (site->escaped > 0) {
                    ++out.violations;
                    if (out.violations == 1) {
                        std::printf(
                            "  VIOLATION program %016llx function %u (%s) pc %u kind %s: claimed "
                            "confined, observed escaped %llu/made %llu\n",
                            static_cast<unsigned long long>(rec.programs()[fn.program].source_hash),
                            fn.index, fn.name.c_str(), site->pc,
                            std::string{ctbrowser::script::site_kind_name(site->kind)}.c_str(),
                            static_cast<unsigned long long>(site->escaped),
                            static_cast<unsigned long long>(site->made));
                    }
                } else if (site->confined > 0) {
                    if (clean) {
                        ++out.sound;
                    } else {
                        ++out.partial;
                    }
                } else {
                    ++out.pending;
                }
            } else {
                if (site->escaped > 0) {
                    ++out.exact;
                } else if (site->confined > 0 && clean) {
                    ++out.imprecise;
                } else {
                    ++out.inconclusive;
                }
            }
        }
        for (std::size_t s = 0; s < sites.size(); ++s) {
            if (!claimed[s]) { ++out.unclaimed; }
        }
    }
    return out;
}

void report_escape(const char * which, const escape_tally & t) {
    std::printf(
        "%s claimed %llu observed %llu unobserved %llu violations %llu sound %llu partial "
        "%llu pending %llu imprecise %llu exact %llu unclaimed %llu inconclusive %llu "
        "mismatch %llu\n",
        which, static_cast<unsigned long long>(t.claimed),
        static_cast<unsigned long long>(t.observed), static_cast<unsigned long long>(t.unobserved),
        static_cast<unsigned long long>(t.violations), static_cast<unsigned long long>(t.sound),
        static_cast<unsigned long long>(t.partial), static_cast<unsigned long long>(t.pending),
        static_cast<unsigned long long>(t.imprecise), static_cast<unsigned long long>(t.exact),
        static_cast<unsigned long long>(t.unclaimed),
        static_cast<unsigned long long>(t.inconclusive),
        static_cast<unsigned long long>(t.mismatch));
}

// One run of the probe, with or without a recorder. What the CONTEXT saw is
// returned too, because "the oracle never collects and never sweeps" is a
// claim about the context, not about the recording.
struct probe_run {
    bool ok = false;
    std::size_t collections = 0;
    std::size_t live_objects = 0;
};

[[nodiscard]] probe_run run_probe(type_recorder * rec, std::uint64_t budget, bool unbounded) {
    if (rec != nullptr) {
        rec->set_escape_budget(budget);
        rec->set_unbounded(unbounded);
    }
    ctbrowser::script::set_active_type_recorder(rec);
    probe_run out;
    {
        ctbrowser::script::context cx;
        ctbrowser::script::install_builtins(cx);
        const ctbrowser::script::program prog = ctbrowser::script::compiler::compile(escape_probe);
        check(prog.ok, "the escape probe compiles");
        if (!prog.ok) {
            std::printf("       %s\n", prog.error.c_str());
            return out;
        }
        const ctbrowser::script::run_result result = cx.run(prog);
        check(result.ok, "the escape probe runs");
        if (!result.ok) { std::printf("       %s\n", result.error.c_str()); }
        out.ok = result.ok;
        out.collections = cx.collections();
        out.live_objects = cx.live_objects();
    }
    ctbrowser::script::set_active_type_recorder(nullptr);
    return out;
}

[[nodiscard]] std::uint64_t total_escaped(const std::vector<std::vector<site_observation>> & all) {
    std::uint64_t n = 0;
    for (const std::vector<site_observation> & sites : all) {
        for (const site_observation & s : sites) { n += s.escaped; }
    }
    return n;
}

int self_test_escape(const char * out_path) {
    if (!ctbrowser::script::type_recording_enabled()) {
        std::printf("ok escape_oracle (SKIPPED - built with CTBROWSER_SCRIPT_RECORD_TYPES=0, "
                    "so the interpreter has no recording hook to check)\n");
        return 0;
    }
    static_assert(ctbrowser::script::root_label_count == 19,
                  "GCRoots.def has 19 rows; the route vocabulary must match it row for row");

    // --- the baseline: the same program with no recorder at all ------------
    const probe_run baseline = run_probe(nullptr, 0, false);
    if (!baseline.ok) { return 1; }

    // --- the bounded run: the oracle as shipped ------------------------------
    type_recorder bounded;
    const probe_run b = run_probe(&bounded, 0, false);
    if (!b.ok) { return 1; }

    // THE ORACLE NEVER COLLECTS AND NEVER SWEEPS. Same collection count and
    // the same live-object count as the run that had no recorder: it marked,
    // it looked, it unmarked, and it freed nothing.
    check_eq(b.collections, baseline.collections, "collections() unchanged by the oracle");
    check_eq(b.live_objects, baseline.live_objects, "live_objects() unchanged - nothing was swept");

    // A RECORDER THAT RECORDS NOTHING LOOKS LIKE A PROGRAM THAT ALLOCATES
    // NOTHING, so every counter that proves a hook fired is asserted.
    check(bounded.pops() > 0, "ret's hook fired (pops)");
    check(bounded.unwinds() > 0, "unwind_to_handler's hook fired (unwinds) - `thrower` throws");
    check(bounded.checks() > 0, "frames were adjudicated (checks)");
    check(bounded.unframed() > 0, "install_builtins allocated with no frame (unframed)");
    check_eq(bounded.unresolved(), std::uint64_t{0},
             "nothing collects under --script, so nothing is unresolved");
    check_eq(bounded.pending_records(), std::uint64_t{0},
             "every frame of the probe ended through a hooked path - nothing is pending");

    const std::vector<std::vector<site_observation>> all = bounded.all_sites();

    // --- THE HAND-COMPUTED TABLE ----------------------------------------------
    //                        fn              kind                  sites made confined escaped
    //                        route
    expect_row(bounded, all, "mk", heap_kind::object, 1, 1, 1, 0, nullptr);
    expect_row(bounded, all, "arr", heap_kind::array, 1, 1, 1, 0, nullptr);
    expect_row(bounded, all, "ret", heap_kind::object, 1, 1, 0, 1, "temporaries");
    expect_row(bounded, all, "glob", heap_kind::object, 1, 1, 0, 1, "globals");
    expect_row(bounded, all, "retain", heap_kind::object, 1, 1, 0, 1, "globals");
    expect_row(bounded, all, "transit", heap_kind::object, 1, 1, 1, 0, nullptr);
    expect_row(bounded, all, "thrower", heap_kind::object, 1, 1, 0, 1, "thrown");
    expect_row(bounded, all, "counter", heap_kind::object, 1, 1, 0, 1, "temporaries");
    expect_row(bounded, all, "counter", heap_kind::cell, 1, 1, 0, 1, "temporaries");
    expect_row(bounded, all, "counter", heap_kind::function, 1, 1, 0, 1, "temporaries");
    expect_row(bounded, all, "local", heap_kind::object, 1, 1, 1, 0, nullptr);
    expect_row(bounded, all, "local", heap_kind::cell, 1, 1, 1, 0, nullptr);
    expect_row(bounded, all, "local", heap_kind::function, 1, 1, 1, 0, nullptr);
    expect_row(bounded, all, "args", heap_kind::object, 1, 1, 1, 0, nullptr);
    expect_row(bounded, all, "args", heap_kind::array, 1, 1, 1, 0, nullptr);
    expect_row(bounded, all, "cyc", heap_kind::object, 2, 2, 2, 0, nullptr);
    expect_row(bounded, all, "leak", heap_kind::object, 2, 2, 0, 2, "temporaries");
    expect_row(bounded, all, "twice", heap_kind::object, 1, 2, 2, 0, nullptr);
    expect_row(bounded, all, "fills", heap_kind::object, 1, 1, 0, 1, "registers");
    expect_row(bounded, all, "holds_across", heap_kind::object, 1, 1, 1, 0, nullptr);
    expect_row(bounded, all, "unclaimed_site", heap_kind::object, 1, 1, 1, 0, nullptr);
    expect_row(bounded, all, "unclaimed_site", heap_kind::array, 1, 1, 1, 0, nullptr);
    {
        // The array's pc is the call's, and the inventory does not name it.
        const std::size_t i = index_of(bounded, "unclaimed_site");
        check_eq(bounded.functions()[i].allocs.size(), std::size_t{1},
                 "unclaimed_site's inventory has only the object literal");
    }
    // Functions that allocate nothing tracked have no site of any kind.
    for (const char * quiet : {"hold", "ident", "catcher"}) {
        const std::size_t i = index_of(bounded, quiet);
        check(i < all.size() && all[i].empty(), (std::string{quiet} + " has no sites").c_str());
    }
    // NEVER ENTERED IS NOT "CONFINED". `never_called` has a static site and no
    // observation, and the two must be told apart.
    {
        const std::size_t i = index_of(bounded, "never_called");
        check(i < all.size(), "the recording has `never_called`");
        if (i < all.size()) {
            check(all[i].empty(), "never_called has no site line");
            check_eq(bounded.functions()[i].allocs.size(), std::size_t{1},
                     "never_called has exactly one static site (its object literal)");
        }
    }

    // --- THE DEAD-WINDOW EXCLUSION IS LOAD-BEARING: the --unbounded A/B ------
    //
    // The unbounded walk includes the ending frame's own register window, so
    // it must report a SUPERSET of escapes. And it changes exactly the rows the
    // exclusion exists for: `transit` (its object is still in a dead register)
    // and `holds_across` (its `kept` is, too). That both flip is the proof the
    // bound is what makes "confined" sayable at all.
    type_recorder unbounded;
    const probe_run u = run_probe(&unbounded, 0, true);
    if (!u.ok) { return 1; }
    const std::vector<std::vector<site_observation>> all_u = unbounded.all_sites();
    const std::uint64_t escaped_bounded = total_escaped(all);
    const std::uint64_t escaped_unbounded = total_escaped(all_u);
    std::printf("escaped bounded %llu unbounded %llu\n",
                static_cast<unsigned long long>(escaped_bounded),
                static_cast<unsigned long long>(escaped_unbounded));
    check(escaped_unbounded >= escaped_bounded, "--unbounded reports a superset of escapes");
    check(escaped_unbounded > escaped_bounded,
          "--unbounded reports MORE escapes - the dead window held something");
    expect_row(unbounded, all_u, "transit", heap_kind::object, 1, 1, 0, 1, "registers");
    expect_row(unbounded, all_u, "holds_across", heap_kind::object, 1, 1, 0, 1, "registers");
    // And the rows that escape through a real root are unchanged by the bound.
    expect_row(unbounded, all_u, "glob", heap_kind::object, 1, 1, 0, 1, "globals");
    expect_row(unbounded, all_u, "fills", heap_kind::object, 1, 1, 0, 1, "registers");

    // --- THE BUDGET: over budget is UNCHECKED, never confined ---------------
    type_recorder budgeted;
    const probe_run g = run_probe(&budgeted, 1, false);
    if (!g.ok) { return 1; }
    const std::vector<std::vector<site_observation>> all_g = budgeted.all_sites();
    {
        const std::size_t i = index_of(budgeted, "twice");
        check(i < all_g.size(), "the budgeted recording has `twice`");
        if (i < all_g.size()) {
            const kind_sum s = sum_kind(all_g[i], heap_kind::object);
            check_eq(s.made, std::uint64_t{2}, "twice under budget 1: made 2");
            check_eq(s.confined, std::uint64_t{1}, "twice under budget 1: the first entry checked");
            check_eq(s.unchecked, std::uint64_t{1},
                     "twice under budget 1: the second is UNCHECKED");
        }
        check_eq(budgeted.checks(), bounded.checks() - 1, "budget 1 skipped exactly one check");
        check_eq(budgeted.pops(), bounded.pops(),
                 "the budget changes what is checked, not what pops");
    }

    // --- THE TWO STUBS --------------------------------------------------------
    const escape_tally wrong = check_escape_stub(bounded, all, true);
    const escape_tally trivial = check_escape_stub(bounded, all, false);
    report_escape("all-confined", wrong);
    report_escape("all-escapes", trivial);
    check(wrong.violations > 0, "all-confined is caught");
    check_eq(wrong.mismatch, std::uint64_t{0},
             "no kind mismatch - the recorder's coordinates are right");
    check_eq(trivial.violations, std::uint64_t{0}, "all-escapes is sound");
    check_eq(trivial.sound, std::uint64_t{0}, "all-escapes has zero precision");
    check_eq(trivial.imprecise + trivial.exact + trivial.inconclusive,
             wrong.violations + wrong.sound + wrong.partial + wrong.pending,
             "both stubs judged the same observed claims");
    check_eq(wrong.unobserved, trivial.unobserved, "both stubs see the same unobserved sites");
    check(wrong.unobserved > 0, "something claimed was never observed (never_called)");
    check(wrong.observed > 0, "something was observed");
    check_eq(
        wrong.unclaimed, std::uint64_t{1},
        "exactly one observed site is unclaimed - the array Object.keys made, on the call's pc");
    check_eq(trivial.unclaimed, wrong.unclaimed, "unclaimed does not depend on the stub");

    std::printf(
        "recorder escape pops %llu unwinds %llu checks %llu unframed %llu unresolved %llu\n",
        static_cast<unsigned long long>(bounded.pops()),
        static_cast<unsigned long long>(bounded.unwinds()),
        static_cast<unsigned long long>(bounded.checks()),
        static_cast<unsigned long long>(bounded.unframed()),
        static_cast<unsigned long long>(bounded.unresolved()));

    if (out_path != nullptr) {
        if (!bounded.write(out_path)) {
            std::printf("FAIL cannot write %s\n", out_path);
            ++failures;
        }
    }

    if (failures == 0) { std::printf("ok escape_oracle\n"); }
    return failures == 0 ? 0 : 1;
}

// --- corpus recording --------------------------------------------------------

// The escape half's knobs for a corpus recording - see type_record.hpp.
std::uint64_t g_escape_budget = 0;
bool g_unbounded = false;

void configure(type_recorder & rec) {
    rec.set_escape_budget(g_escape_budget);
    rec.set_unbounded(g_unbounded);
}

void report_escape_counters(const type_recorder & rec) {
    std::fprintf(stderr,
                 "escape pops %llu unwinds %llu checks %llu unframed %llu unresolved %llu "
                 "pending %llu\n",
                 static_cast<unsigned long long>(rec.pops()),
                 static_cast<unsigned long long>(rec.unwinds()),
                 static_cast<unsigned long long>(rec.checks()),
                 static_cast<unsigned long long>(rec.unframed()),
                 static_cast<unsigned long long>(rec.unresolved()),
                 static_cast<unsigned long long>(rec.pending_records()));
}

int record_page(const std::string & html_path, const std::string & assets, int frames,
                const std::vector<std::string> & extra_js, const std::string & out_path) {
    const std::string html = read_file(html_path);
    if (html.empty()) {
        std::fprintf(stderr, "type-oracle: cannot read %s\n", html_path.c_str());
        return 1;
    }
    type_recorder rec;
    configure(rec);
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
    report_escape_counters(rec);
    return 0;
}

int record_script(const std::string & js_path, const std::string & out_path) {
    const std::string source = read_file(js_path);
    if (source.empty()) {
        std::fprintf(stderr, "type-oracle: cannot read %s\n", js_path.c_str());
        return 1;
    }
    type_recorder rec;
    configure(rec);
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
    report_escape_counters(rec);
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
    bool escape = false;
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
        } else if (flag == "--escape") {
            escape = true;
        } else if (flag == "--escape-budget") {
            g_escape_budget = static_cast<std::uint64_t>(std::atoll(next().c_str()));
        } else if (flag == "--unbounded") {
            g_unbounded = true;
        } else {
            std::fprintf(stderr,
                         "usage: type-oracle [--out FILE]                the type self-test\n"
                         "       type-oracle --escape [--out FILE]       the escape self-test\n"
                         "       type-oracle --page PAGE.html [--assets DIR] [--frames N] "
                         "[--js EXTRA.js ...] --out FILE\n"
                         "       type-oracle --script BUNDLE.js --out FILE\n"
                         "  recording options: --escape-budget K  (frame ends adjudicated per "
                         "function, 0 = unlimited)\n"
                         "                     --unbounded         (walk the dead register window "
                         "too; a superset of escapes)\n");
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
    if (escape) { return self_test_escape(out.empty() ? nullptr : out.c_str()); }
    return self_test(out.empty() ? nullptr : out.c_str());
}
