// DOES COMPILED CODE COMPUTE WHAT THE INTERPRETER COMPUTES?
//
// This is the test the project's own policy asks for: "when a CTJS operation
// and the ctbrowser VM disagree, the VM is correct by definition." So nothing
// here writes an expected answer down. Each body is run twice against the same
// context - once interpreted, once with its compiled entry installed - and the
// two results are compared.
//
// WHY IT IS DIFFERENT FROM EVERY OTHER TEST HERE. The lit tests read the
// emitted C++ and compile it; ctcompile_linkable links it; ctcompile_gc_roots
// runs one body with the collector hostile. None of them asks whether the
// answer is RIGHT. A backend can emit fluent, linkable, correctly-rooted code
// that computes the wrong thing - lowering `a + b` to op::add instead of
// op::add_generic passes every one of those and makes `{valueOf:()=>3} + 1`
// answer NaN.
//
// THE INPUTS ARE CHOSEN TO SEPARATE THE LOWERINGS, not to cover them. A test
// whose answer is the same whether or not the compiler is right is worse than
// no test, so every case below is one where a plausible mistake changes the
// result:
//
//   AN OBJECT WITH A valueOf separates the two `+` families. op::add uses the
//   static conversions and cannot run user code; op::add_generic runs
//   ToPrimitive. The runtime reaches the static one only from `++`.
//
//   NaN separates the four relational operators from one another.
//   ct_aot_compare answers UNORDERED for it, which makes all four false -
//   including `>=`, so `>=` lowered as `!(<)` answers true here and nowhere
//   else.
//
//   0 AND "0" AND false separate strict from loose equality, which reach
//   different helpers with different effect profiles.
//
// It deliberately does not test the collector: gc_roots owns that, and keeping
// them apart means a rooting bug and a wrong answer never look like each other.
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

// Compiled by the build from differential.js, one per function named there.
#define CT_ENTRY(name_)                                                                            \
    extern "C" std::int32_t ctc_##name_(                                                           \
        ctbrowser::aot::ct_aot_ctx *, const ctbrowser::aot::ct_aot_site *, const std::uint64_t *,  \
        std::uint32_t, std::uint64_t, std::uint32_t, std::uint64_t *);
CT_ENTRY(plus)
CT_ENTRY(ge)
CT_ENTRY(strict)
CT_ENTRY(loose)
CT_ENTRY(pick)
CT_ENTRY(globals)
CT_ENTRY(apply)
CT_ENTRY(step)
#undef CT_ENTRY

namespace {

// MUST STAY TEXTUALLY IDENTICAL TO differential.js. The two tiers have to be
// compiling the same function_proto, and the count check below is a cheap guard
// against them drifting apart silently.
constexpr std::string_view fixture = R"JS(
function plus(a, b) { return a + b; }
function ge(a, b) { return a >= b; }
function strict(a, b) { return a === b; }
function loose(a, b) { return a == b; }
function pick(a, b) { if (a < b) { return a; } return b; }
function globals(a) { DIFF_W = a; return DIFF_R; }
function apply(k, a, b) { return k(a, b); }
function counter(start) { var n = start; return function step() { n = n + 1; return n; }; }
function counters(a, b) { return counter(a)() + counter(b)(); }

var DIFF_R = 0, DIFF_W = 0, OUT = "";

// THE ARGUMENTS ARE BUILT HERE rather than passed from C++, so the harness
// holds no JavaScript value in a C++ local of its own.
var counter2 = { valueOf: function () { return 3; } };
var nan = 0 / 0;

function drive(which) {
  DIFF_R = 41; DIFF_W = 0;
  if (which === 0) { OUT = plus(counter2, 1); }
  if (which === 1) { OUT = ge(nan, nan); }
  if (which === 2) { OUT = strict(0, "0"); }
  if (which === 3) { OUT = loose(0, "0"); }
  if (which === 4) { OUT = pick(2, 7); }
  if (which === 5) { OUT = "" + globals(7) + "/" + DIFF_W; }
  if (which === 6) { OUT = apply(plus, counter2, 1); }
  // CALLED TWICE, because one call cannot tell a captured cell from a copy:
  // both answer 1. The second answer is 2 only if the binding persisted.
  if (which === 7) { var c = counter(10); c(); OUT = c(); }
  // 101 + 201 = 302 if the two closures capture separately; 101 + 102 = 203 if
  // they share, which is what taking upvalues from the proto would do.
  if (which === 8) { OUT = counters(100, 200); }
}
)JS";

struct subject {
    // WHICH ARM OF drive() THIS IS, written down rather than taken from the
    // row's position. It was positional, and inserting a case in the middle
    // silently misattributed every row after it - the answers stayed right and
    // the NAMES moved, so a failure would have named the wrong lowering. A
    // test that lies about which case failed is worse than one that fails.
    unsigned which;
    const char * name;
    // WHOSE function_proto GETS THE COMPILED ENTRY, which is not always the
    // function the arm calls. The harness installs ONE entry at a time, so a
    // case that drives `counters` while installing `counters` runs `step`
    // INTERPRETED and exercises no closure at all - which is what the first
    // version of the two-closure case did. It passed with the closure handoff
    // removed, and that is how it was found.
    const char * patched;
    ctbrowser::aot::ct_aot_entry_fn entry;
    // WHY THIS CASE WOULD DIFFER, so a failure says what broke rather than only
    // that something did.
    const char * separates;
};

int failures = 0;

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

    const subject subjects[] = {
        {0u, "plus", "plus", &ctc_plus,
         "op::add_generic against op::add - a valueOf that is or is not run"},
        {1u, "ge", "ge", &ctc_ge, "UNORDERED - `>=` lowered as !(<) answers true for NaN"},
        {2u, "strict", "strict", &ctc_strict,
         "ct_aot_strict_equals, which cannot throw and takes no frame"},
        {3u, "loose", "loose", &ctc_loose,
         "ct_aot_loose_equals, which converts and has an exception edge"},
        {4u, "pick", "pick", &ctc_pick,
         "block arguments, which the C++ emitter miscompiles as edges"},
        {5u, "globals", "globals", &ctc_globals, "a global read and a global write"},
        {6u, "apply", "apply", &ctc_apply, "the contiguous argument window a call needs"},
        {7u, "step", "step", &ctc_step,
         "a captured cell against a copied value - a copy answers 1 twice, and a body that "
         "cannot see its closure answers undefined, because the write lands on a non-cell and "
         "is dropped"},
        // PATCHES step, NOT counters. The instance-versus-proto question is
        // about the two `step` closures, so `step` is what has to be running
        // compiled; `counters` is the interpreted driver that makes two of them.
        {8u, "two closures", "step", &ctc_step,
         "the closure INSTANCE against the shared function_proto - sharing makes the second "
         "counter continue the first's count, giving 203 rather than 302"},
    };

    for (const subject & each : subjects) {
        function_proto * body = nullptr;
        for (function_proto & candidate : compiled.functions) {
            if (candidate.name == each.patched) { body = &candidate; }
        }
        if (body == nullptr) {
            std::printf("%-10s FAILED - no function_proto, so differential.js and this file "
                        "have drifted\n",
                        each.name);
            ++failures;
            continue;
        }

        const auto answer = [&](ctbrowser::aot::ct_aot_entry_fn entry) {
            body->aot_entry = entry;
            const value which = value::number(static_cast<double>(each.which));
            const value arguments[] = {which};
            cx.call(cx.global("drive"), std::span<const value>{arguments}, value::undefined());
            body->aot_entry = nullptr;
            return cx.to_string(cx.global("OUT"));
        };

        const std::string interpreted = answer(nullptr);
        const std::string generated = answer(each.entry);
        if (interpreted == generated) {
            std::printf("%-10s ok    %s\n", each.name, interpreted.c_str());
        } else {
            std::printf("%-10s FAILED\n    interpreted %s\n    compiled    %s\n    separates:  "
                        "%s\n",
                        each.name, interpreted.c_str(), generated.c_str(), each.separates);
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("\nall %zu bodies agree with the interpreter\n", std::size(subjects));
    }
    return failures == 0 ? 0 : 1;
}
