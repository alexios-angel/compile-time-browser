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
CT_ENTRY(counter)
CT_ENTRY(middle)
CT_ENTRY(methodish)
CT_ENTRY(fn)
CT_ENTRY(neg)
CT_ENTRY(bnot)
CT_ENTRY(put)
CT_ENTRY(greet)
CT_ENTRY(pack)
CT_ENTRY(kindOf)
CT_ENTRY(thrower)
CT_ENTRY(build)
CT_ENTRY(Point)
CT_ENTRY(newBad)
CT_ENTRY(coalesce)
CT_ENTRY(chain)
CT_ENTRY(dflt)
CT_ENTRY(total)
CT_ENTRY(chars)
CT_ENTRY(spread)
CT_ENTRY(hasIt)
CT_ENTRY(isA)
CT_ENTRY(drop)
CT_ENTRY(greetChain)
CT_ENTRY(Kid)
CT_ENTRY(spreadCall)
CT_ENTRY(spreadMethod)
CT_ENTRY(spreadNew)
CT_ENTRY(merge)
CT_ENTRY(mergeArray)
CT_ENTRY(firstLoop)
CT_ENTRY(accessors)
CT_ENTRY(guarded)
CT_ENTRY(plusOf)
CT_ENTRY(coerce)
CT_ENTRY(keysOf)
CT_ENTRY(dropNamed)
CT_ENTRY(bigLits)
CT_ENTRY(howMany)
CT_ENTRY(sumAll)
CT_ENTRY(restOf)
CT_ENTRY(bothOf)
CT_ENTRY(wrapped)
CT_ENTRY(passes)
CT_ENTRY(arrayOut)
#undef CT_ENTRY

namespace {

// THE SAME FILE THE PIPELINE COMPILES, not a transcription of it.
//
// It WAS a transcription, with a comment saying the two must stay identical -
// and they drifted in ORDER: a function appended in a different position in
// each. That is not cosmetic. A compiled body bakes the function INDEX of every
// closure it builds, and ct_aot_make_closure's row says outright that "a
// function index means nothing outside the program it was compiled in" - so a
// reordered fixture makes a compiled body build a closure over a DIFFERENT
// function. It presented as a case reporting the wrong answer and passing,
// because both tiers agreed on a stale OUT.
constexpr std::string_view fixture =
#include "differential.js.inc"
    ;

// One proto to install a compiled entry on.
//
// BY NAME AND ORDINAL, because an arrow has no name: the importer calls every
// anonymous function `fn`, and picking the first match silently would be the
// wrong closure the moment a fixture grows a second one. The lookup below
// refuses an ambiguous name rather than guessing.
struct installed {
    const char * name;
    unsigned ordinal;
    ctbrowser::aot::ct_aot_entry_fn entry;
};

struct subject {
    // WHICH ARM OF drive() THIS IS, written down rather than taken from the
    // row's position. It was positional, and inserting a case in the middle
    // silently misattributed every row after it - the answers stayed right and
    // the NAMES moved, so a failure would have named the wrong lowering. A
    // test that lies about which case failed is worse than one that fails.
    unsigned which;
    const char * name;
    // WHICH function_protos GET A COMPILED ENTRY, which is not always the
    // function the arm calls, and is not always ONE.
    //
    // Installing a single entry is what the harness did, and a case that drives
    // `counters` while installing `counters` runs `step` INTERPRETED and
    // exercises no closure at all - which is what the first two-closure case
    // did. It passed with the closure handoff removed, and that is how it was
    // found.
    //
    // MORE THAN ONE IS NOT A CONVENIENCE EITHER. Whether a closure BUILT by
    // compiled code enters compiled code when called is a question no
    // single-entry case can ask, and the ABI row claims it does not.
    installed patches[2];
    // WHAT THE ANSWER MUST BE, and it is written down on purpose.
    //
    // THE INTERPRETER DEFINES CORRECT, which is this file's whole premise - but
    // that premise has a bound, and it is worth stating rather than
    // discovering. Where the two tiers share an implementation, a bug in the
    // shared part breaks BOTH and they agree. context::make_closure is exactly
    // that: it was factored out so run_loop and ct_aot_make_closure could not
    // drift, and the price is that the differential comparison goes blind to
    // it. Swapping its two descriptor arms makes every closure case answer
    // `undefined` and every one of them still "agree".
    //
    // So the cases that reach shared code carry an anchor. It is not a second
    // source of truth for the language - it is a tripwire on the one path the
    // comparison cannot see.
    // WHY THIS CASE WOULD DIFFER, so a failure says what broke rather than only
    // that something did.
    const char * separates;
    const char * expected;
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
        {0u,
         "plus",
         {{"plus", 0u, &ctc_plus}, {}},
         "op::add_generic against op::add - a valueOf that is or is not run",
         "4"},
        {1u,
         "ge",
         {{"ge", 0u, &ctc_ge}, {}},
         "UNORDERED - `>=` lowered as !(<) answers true for NaN",
         "false"},
        {2u,
         "strict",
         {{"strict", 0u, &ctc_strict}, {}},
         "ct_aot_strict_equals, which cannot throw and takes no frame",
         "false"},
        {3u,
         "loose",
         {{"loose", 0u, &ctc_loose}, {}},
         "ct_aot_loose_equals, which converts and has an exception edge",
         "true"},
        {4u,
         "pick",
         {{"pick", 0u, &ctc_pick}, {}},
         "block arguments, which the C++ emitter miscompiles as edges",
         "2"},
        {5u,
         "globals",
         {{"globals", 0u, &ctc_globals}, {}},
         "a global read and a global write",
         "41/7"},
        {6u,
         "apply",
         {{"apply", 0u, &ctc_apply}, {}},
         "the contiguous argument window a call needs",
         "4"},
        {7u,
         "step",
         {{"step", 0u, &ctc_step}, {}},
         "a captured cell against a copied value - a copy answers 1 twice, and a body that "
         "cannot see its closure answers undefined, because the write lands on a non-cell and "
         "is dropped",
         "12"},
        // PATCHES step, NOT counters. The instance-versus-proto question is
        // about the two `step` closures, so `step` is what has to be running
        // compiled; `counters` is the interpreted driver that makes two of them.
        // PATCHES counter, WHICH BUILDS THE CLOSURE. Everything above reads
        // one; this is the first case where ct_aot_make_closure runs in
        // compiled code.
        {9u,
         "make closure",
         {{"counter", 0u, &ctc_counter}, {}},
         "building a closure at all - a wrong upvalue array captures the wrong binding and "
         "says nothing",
         "52"},
        {10u,
         "two levels",
         {{"middle", 0u, &ctc_middle}, {}},
         "the from_parent_local arm against the enclosing-closure arm - `y` is middle's own "
         "register and `x` is not",
         "3"},
        // AN ARROW'S `this`, WITH BOTH HALVES COMPILED. `methodish` builds the
        // arrow, so it exercises ct_aot_make_closure's enclosing_this; the
        // arrow itself reads it, which exercises ct_aot_this.
        //
        // ITS PROTO NAME IS EMPTY. `fn` is what the IMPORTER calls an anonymous
        // function when it builds a SYMBOL - `fn$2` - and the proto's own name
        // is "". The two are not the same string, and the ordinal is there
        // because a second anonymous function would make "" ambiguous.
        {19u,
         "throw",
         {{"thrower", 0u, &ctc_thrower}, {}},
         "a real throw against a frame that merely unwound - a lost thrown_ catches undefined",
         "caught 7"},
        // ONLY `build`, SO Point STAYS INTERPRETED - and that is the whole
        // difference between this case and the next, which was found by a
        // mutant that passed.
        //
        // THE PRIMITIVE-RETURN RULE IS WRITTEN TWICE and only one copy runs at
        // a time. ct_aot_construct_result applies it INSIDE a compiled body
        // before returning; context::construct_new applies it to what invoke()
        // handed back. Patch both halves and the compiled Point has already
        // done it, so construct_new's copy sees an object and can be deleted
        // with every case still green. Leaving Point interpreted is what makes
        // that copy load-bearing - and it is the only thing that does, because
        // an interpreted frame entered through invoke() has `constructing`
        // FALSE (the aggregate initialiser fills eight members and constructing
        // is the twelfth), so run_loop's own copy of the rule does not fire
        // either.
        {20u,
         "construct",
         {{"build", 0u, &ctc_build}, {}},
         "the fresh instance against the body's return value - `new` throws away the 7 Point "
         "returns, and an ordinary call would answer it",
         "9"},
        // AND NOW BOTH HALVES, which is a different path rather than a stronger
        // one: `Point` is entered with constructing set, so the rule is applied
        // by the compiled body and construct_new never sees a primitive.
        {20u,
         "construct compiled",
         {{"build", 0u, &ctc_build}, {"Point", 0u, &ctc_Point}},
         "entering a COMPILED constructor - ct_aot_construct_result is where the rule lives on "
         "that side, and `constructing` is what selects it",
         "9"},
        // THE ONE CASE THAT READS ct_aot_construct's `site`. See newBad.
        {21u,
         "new on a number",
         {{"newBad", 0u, &ctc_newBad}, {}},
         "the entry's own site against the memo marker - the site is only ever read to name "
         "the enclosing function in this message",
         "TypeError: `new` on the value is number (5), not a function - in `newBad`"},
        // THE TWO CONDITIONAL JUMPS THE IMPORTER'S CFG CLASSIFIER DID NOT
        // KNOW. Their emission was written and correct; is_conditional_jump
        // named two of the four, so neither one's target was ever marked a
        // block leader and the emitter refused every function containing one.
        //
        // ALL THREE BODIES ARE PATCHED, because each compiles to a different
        // one of the three shapes and a single entry would leave two
        // interpreted.
        {22u,
         "nullish",
         {{"coalesce", 0u, &ctc_coalesce}, {"chain", 0u, &ctc_chain}},
         "definedness against truthiness - `0 ?? 9` is 0, and a jump lowered through ctjs.truthy "
         "answers 9",
         "0//9/undefined/4/0"},
        // for-of, WHICH IS ONE OPCODE AND THREE ARMS OF ONE HELPER.
        {23u,
         "for-of",
         {{"total", 0u, &ctc_total}, {"chars", 0u, &ctc_chars}},
         "the ORDER the values drain in and the non-object arm - `chars` prepends, so a "
         "reversed drain answers abc, and a sum could not tell",
         "6/cba/0"},
        {24u,
         "spread",
         {{"spread", 0u, &ctc_spread}, {}},
         "the same helper reached from a spread rather than a loop - the string arm yields one "
         "element per character",
         "3/2"},
        {25u,
         "in",
         {{"hasIt", 0u, &ctc_hasIt}, {}},
         "an INDEX against a name - `1x` is not index 1, and 2 is past the end - and the "
         "non-object arm, which is false rather than a throw",
         "true/false/false/true/false"},
        {26u,
         "instanceof",
         {{"isA", 0u, &ctc_isA}, {}},
         "the explicit prototype chain against the implicit tables, and the object-like guard - "
         "`5 instanceof Number` is false however many methods a primitive resolves",
         "true/true/false"},
        {27u,
         "delete",
         {{"drop", 0u, &ctc_drop}, {}},
         "the property actually going away - without the erase it reads back as 1, giving 12",
         "undefined2"},
        {28u,
         "super method",
         {{"greetChain", 0u, &ctc_greetChain}, {}},
         "the HOME object against `this` - Sub shadows hello, so a lookup starting at the "
         "receiver's own prototype answers BSUB",
         "BS"},
        {29u,
         "super constructor",
         {{"Kid", 0u, &ctc_Kid}, {}},
         "pass_new_target AND the base call, which are different halves: `v` is written by the "
         "base constructor either way, so new.target is the only thing the handoff changes",
         "20/true"},
        {30u,
         "spread call",
         {{"spreadCall", 0u, &ctc_spreadCall}, {}},
         "an argument ARRAY against a contiguous window - passing the array as one argument "
         "answers 1,2,3/undefined/undefined, and a non-iterable spread yields NO arguments "
         "rather than one",
         "1/2/3|undefined/undefined/undefined"},
        {31u,
         "spread method",
         {{"spreadMethod", 0u, &ctc_spreadMethod}, {}},
         "the receiver, which ct_aot_call_spread takes as its own operand - dropping it makes "
         "`this.tag` read undefined",
         "R7"},
        {32u,
         "spread new",
         {{"spreadNew", 0u, &ctc_spreadNew}, {}},
         "op::construct_apply, a different opcode and helper from both ctjs.construct and "
         "ctjs.call_spread",
         "12"},
        {33u,
         "object spread",
         {{"merge", 0u, &ctc_merge}, {"mergeArray", 0u, &ctc_mergeArray}},
         "which key wins - the spread overwrites the literal before it and loses to the one "
         "after, so a copy in the wrong order changes both digits - and an array source, which "
         "spreads by index",
         "29/78"},
        // THE SHAPE THAT JUMPS BACK TO INSTRUCTION ZERO. It is a differential
        // case rather than an import test because importing it was only half
        // the question: the entry now falls through into a header block
        // carrying the whole register file, and getting that edge wrong loses
        // the loop variable rather than failing to compile.
        {34u,
         "loop at zero",
         {{"firstLoop", 0u, &ctc_firstLoop}, {}},
         "a back edge to instruction 0 - the importer marked it a leader and built no block for "
         "it, and the second call checks the loop is skipped rather than entered once",
         "3/1"},
        // THE COMPILED BODY IS `accessors`, WHICH INSTALLS THEM - not
        // useAccessor, which merely reads and writes. define_getter and
        // define_setter are what is under test, and they run where the object
        // literal is built.
        {35u,
         "accessors",
         {{"accessors", 0u, &ctc_accessors}, {}},
         "the getter half against the setter half, which no operand encodes - swapping them "
         "makes the read answer undefined AND the write record nothing, so both halves of the "
         "answer move at once",
         "41/7/s"},
        // ONLY `guarded` IS PATCHED, so `thrower` stays interpreted and the
        // caught status reaches the compiled frame from ct_aot_call across a
        // real throw. The AOT pass below runs the same arm with `thrower`
        // compiled too, which is a different path through ct_aot_check: a
        // callee's UNWOUND reclassified as the caller's CAUGHT.
        {36u,
         "try/catch",
         {{"guarded", 0u, &ctc_guarded}, {}},
         "the register file AS OF THE THROW against as of the `try` - n is written to 1 inside "
         "the protected region, so a handler edge taken from push_handler answers 0:7",
         "1:7/2"},
        {37u,
         "unary plus",
         {{"plusOf", 0u, &ctc_plusOf}, {"coerce", 0u, &ctc_coerce}},
         "ToNumber against truthiness - the empty string is 0 and undefined is NaN, and NaN is "
         "the only value that is not equal to itself",
         "42/0/true"},
        {38u,
         "for-in",
         {{"keysOf", 0u, &ctc_keysOf}, {}},
         "definition ORDER, which a key count would not see - and an array enumerates its "
         "indices as STRINGS, while a non-object yields nothing rather than throwing",
         "xy/01/"},
        {39u,
         "delete named",
         {{"dropNamed", 0u, &ctc_dropNamed}, {"keysOf", 0u, &ctc_keysOf}},
         "the NAMED delete against the computed one - a different opcode and a different "
         "helper, and the key must actually leave the enumeration as well as read undefined",
         "ac/undefined"},
        {40u,
         "bigint literals",
         {{"bigLits", 0u, &ctc_bigLits}, {}},
         "the per-slot memo, which the string version once got wrong the same way - two "
         "different literals in one body, so a shared key returns the other one - and the hex "
         "form, which only bigint_from_literal parses correctly",
         "900000000000000000009/16/900000000000000000010"},
        {41u,
         "arguments",
         {{"howMany", 0u, &ctc_howMany}, {"sumAll", 0u, &ctc_sumAll}},
         "arguments PAST the declared parameters, which a compiled frame could not see at all - "
         "its own registers sit above the caller's window and the prologue reads only the "
         "declared ones, so a body limited to them answers 2 and 0",
         "4/1/18"},
        {42u,
         "rest parameter",
         {{"restOf", 0u, &ctc_restOf}, {"bothOf", 0u, &ctc_bothOf}},
         "the rest array, and the path that only exists when `arguments` was built too - that "
         "claims a register an extra argument may be in, so gather_rest must read the frame's "
         "copy rather than the window",
         "2:2,3/0:/3/2,3"},
        // ASYNC WITHOUT await. op::wrap_promise is the only opcode async
        // adds to a body with no suspension point in it, and it is the only
        // one of Phase 14's three that a compiled C++ stack frame can execute
        // at all.
        {50u,
         "wrap_promise",
         {{"wrapped", 0u, &ctc_wrapped}, {}},
         "the wrap itself - a body that returned its value raw answers "
         "\"number/2/undefined\" and the second field still looks nearly right",
         "object/2/true/false"},
        // BOTH SHAPE TESTS, and they are the two a plausible simplification
        // gets wrong in opposite directions.
        {51u,
         "promise shapes",
         {{"passes", 0u, &ctc_passes}, {"arrayOut", 0u, &ctc_arrayOut}},
         "the already-a-promise test (dropping it nests, so === is false) against the "
         "EXACTNESS of is_object() (widening it to is_object_like passes an array through "
         "unwrapped, so __value is undefined)",
         "true/7/object"},
        {17u,
         "literals",
         {{"pack", 0u, &ctc_pack}, {}},
         "an array built by new_array plus appends, and an object literal written through "
         "set_index",
         "112"},
        {18u,
         "typeof",
         {{"kindOf", 0u, &ctc_kindOf}, {}},
         "a LENGTH and a static pointer turned into a string, with no memo",
         "number/number/bigint"},
        {16u,
         "string literal",
         {{"greet", 0u, &ctc_greet}, {}},
         "ct_aot_new_string and its (site, slot) memo - a truncating escape or a wrong length "
         "shows up in the text",
         "ababab!"},
        {15u,
         "property write",
         {{"put", 0u, &ctc_put}, {}},
         "the numeric fast path against the named one - a STRING key on an array never reaches "
         "items[0], and store_property's array arm DROPS it - so it reads back as nothing "
         "at all",
         "900/100"},
        {12u,
         "negate",
         {{"neg", 0u, &ctc_neg}, {}},
         "ToNumber-plus-negation against a real unary minus - and -0, whose 1/x is -Infinity",
         "-5/-Infinity"},
        {13u,
         "negate bigint",
         {{"neg", 0u, &ctc_neg}, {}},
         "the BigInt arm, which allocates and which ToNumber would throw on",
         "-9007199254740993/0"},
        {14u,
         "bitwise not",
         {{"bnot", 0u, &ctc_bnot}, {}},
         "the ToInt32 arm against the unbounded one - a BigInt has no width to truncate to",
         "-6/-9007199254740994"},
        {11u,
         "arrow this",
         {{"methodish", 0u, &ctc_methodish}, {"", 0u, &ctc_fn}},
         "the EFFECTIVE receiver against the entry's raw one - `seen()` is called with no "
         "receiver, so the raw one is undefined",
         "captured"},
        // A CLOSURE BUILT BY COMPILED CODE, CALLED FROM COMPILED CODE. No
        // single-entry case can ask this, and the ABI row claims it does not
        // work - a claim that predates function_proto::aot_entry.
        {9u,
         "nested compiled",
         {{"counter", 0u, &ctc_counter}, {"step", 0u, &ctc_step}},
         "whether a closure built by compiled code enters compiled code when called",
         "52"},
        {8u,
         "two closures",
         {{"step", 0u, &ctc_step}, {}},
         "the closure INSTANCE against the shared function_proto - sharing makes the second "
         "counter continue the first's count, giving 203 rather than 302",
         "302"},
    };

    // ---- AND THE THIRD MODE ------------------------------------------------
    //
    // The plan asks for differential runs under VM, HYBRID and AOT. Everything
    // above is HYBRID: one or two protos carry a compiled entry and the rest of
    // the program is interpreted, which is what makes each case separate one
    // lowering from its neighbours.
    //
    // IT IS ALSO THE MODE THAT CANNOT SEE A CROSS-FUNCTION DEFECT. A compiled
    // caller reaching a compiled callee goes through paths a single patch never
    // exercises - the argument window survives a callee that collects, a
    // new.target handoff crosses a frame boundary that is compiled on BOTH
    // sides - and exactly one case above (`nested compiled`) asks that question,
    // about exactly two functions.
    //
    // So this installs EVERY entry the build produced and runs every arm again.
    // It separates nothing on its own; it is the check that the cases above,
    // which each hold one variable still, did not agree only because everything
    // around them was interpreted.
    const installed all[] = {
        {"plus", 0u, &ctc_plus},
        {"ge", 0u, &ctc_ge},
        {"strict", 0u, &ctc_strict},
        {"loose", 0u, &ctc_loose},
        {"pick", 0u, &ctc_pick},
        {"globals", 0u, &ctc_globals},
        {"apply", 0u, &ctc_apply},
        {"step", 0u, &ctc_step},
        {"counter", 0u, &ctc_counter},
        {"middle", 0u, &ctc_middle},
        {"methodish", 0u, &ctc_methodish},
        {"", 0u, &ctc_fn},
        {"neg", 0u, &ctc_neg},
        {"bnot", 0u, &ctc_bnot},
        {"put", 0u, &ctc_put},
        {"greet", 0u, &ctc_greet},
        {"pack", 0u, &ctc_pack},
        {"kindOf", 0u, &ctc_kindOf},
        {"thrower", 0u, &ctc_thrower},
        {"build", 0u, &ctc_build},
        {"Point", 0u, &ctc_Point},
        {"newBad", 0u, &ctc_newBad},
        {"coalesce", 0u, &ctc_coalesce},
        {"chain", 0u, &ctc_chain},
        {"dflt", 0u, &ctc_dflt},
        {"total", 0u, &ctc_total},
        {"chars", 0u, &ctc_chars},
        {"spread", 0u, &ctc_spread},
        {"hasIt", 0u, &ctc_hasIt},
        {"isA", 0u, &ctc_isA},
        {"drop", 0u, &ctc_drop},
        {"greetChain", 0u, &ctc_greetChain},
        {"Kid", 0u, &ctc_Kid},
        {"spreadCall", 0u, &ctc_spreadCall},
        {"spreadMethod", 0u, &ctc_spreadMethod},
        {"spreadNew", 0u, &ctc_spreadNew},
        {"merge", 0u, &ctc_merge},
        {"mergeArray", 0u, &ctc_mergeArray},
        {"firstLoop", 0u, &ctc_firstLoop},
        {"accessors", 0u, &ctc_accessors},
        {"guarded", 0u, &ctc_guarded},
        {"plusOf", 0u, &ctc_plusOf},
        {"coerce", 0u, &ctc_coerce},
        {"keysOf", 0u, &ctc_keysOf},
        {"dropNamed", 0u, &ctc_dropNamed},
        {"bigLits", 0u, &ctc_bigLits},
        {"howMany", 0u, &ctc_howMany},
        {"sumAll", 0u, &ctc_sumAll},
        {"restOf", 0u, &ctc_restOf},
        {"bothOf", 0u, &ctc_bothOf},
        {"wrapped", 0u, &ctc_wrapped},
        {"passes", 0u, &ctc_passes},
        {"arrayOut", 0u, &ctc_arrayOut},
    };

    for (const subject & each : subjects) {
        // EVERY PROTO THIS CASE INSTALLS ON, resolved before anything runs so a
        // missing one is reported rather than silently skipped.
        function_proto * bodies[std::size(each.patches)] = {};
        bool resolved = true;
        for (std::size_t slot = 0; slot < std::size(each.patches); ++slot) {
            const installed & want = each.patches[slot];
            if (want.name == nullptr) { continue; }
            unsigned seen = 0;
            for (function_proto & candidate : compiled.functions) {
                if (candidate.name != want.name) { continue; }
                if (seen++ == want.ordinal) { bodies[slot] = &candidate; }
            }
            if (bodies[slot] == nullptr) {
                std::printf("%-12s FAILED - no function_proto named %s#%u\n", each.name, want.name,
                            want.ordinal);
                resolved = false;
            }
        }
        if (!resolved) {
            ++failures;
            continue;
        }

        const auto answer = [&](bool compiled_tier) {
            for (std::size_t slot = 0; slot < std::size(each.patches); ++slot) {
                if (bodies[slot] != nullptr) {
                    bodies[slot]->aot_entry = compiled_tier ? each.patches[slot].entry : nullptr;
                }
            }
            const value which = value::number(static_cast<double>(each.which));
            const value arguments[] = {which};
            cx.call(cx.global("drive"), std::span<const value>{arguments}, value::undefined());
            for (function_proto * each_body : bodies) {
                if (each_body != nullptr) { each_body->aot_entry = nullptr; }
            }
            return cx.to_string(cx.global("OUT"));
        };

        const std::string interpreted = answer(false);
        const std::string generated = answer(true);

        // THE ARM HAS TO HAVE RUN. Without this a case whose arm throws leaves
        // OUT holding the PREVIOUS case's answer, both tiers read the same
        // stale value, and the case reports success while testing nothing.
        if (interpreted == "<the arm did not run>") {
            std::printf("%-12s FAILED - drive(%u) set nothing, so the arm threw or does not "
                        "exist\n",
                        each.name, each.which);
            ++failures;
            continue;
        }
        // AND THE ANCHOR, for the paths the comparison cannot see. See the
        // note on subject::expected: where the two tiers share an
        // implementation, a bug in it breaks both and they agree.
        if (interpreted != each.expected) {
            std::printf("%-12s FAILED - the INTERPRETER answered %s where %s is correct, so "
                        "something shared by both tiers is wrong\n",
                        each.name, interpreted.c_str(), each.expected);
            ++failures;
            continue;
        }
        if (interpreted == generated) {
            std::printf("%-10s ok    %s\n", each.name, interpreted.c_str());
        } else {
            std::printf("%-10s FAILED\n    interpreted %s\n    compiled    %s\n    separates:  "
                        "%s\n",
                        each.name, interpreted.c_str(), generated.c_str(), each.separates);
            ++failures;
        }
    }

    // ---- the AOT pass ------------------------------------------------------
    //
    // RESOLVED FIRST AND ASSERTED, because a name that matches nothing would
    // silently install nothing, and a pass that compiles nothing agrees with
    // the interpreter perfectly. That is the shape of every vacuous test this
    // project has already found.
    function_proto * every[std::size(all)] = {};
    for (std::size_t slot = 0; slot < std::size(all); ++slot) {
        unsigned seen = 0;
        for (function_proto & candidate : compiled.functions) {
            if (candidate.name != all[slot].name) { continue; }
            if (seen++ == all[slot].ordinal) { every[slot] = &candidate; }
        }
        if (every[slot] == nullptr) {
            std::printf("AOT        FAILED - no function_proto named %s#%u\n", all[slot].name,
                        all[slot].ordinal);
            ++failures;
        }
    }

    unsigned arms = 0;
    unsigned disagreed = 0;
    for (const subject & each : subjects) {
        for (std::size_t slot = 0; slot < std::size(all); ++slot) {
            if (every[slot] != nullptr) { every[slot]->aot_entry = nullptr; }
        }
        const value which = value::number(static_cast<double>(each.which));
        const value arguments[] = {which};
        cx.call(cx.global("drive"), std::span<const value>{arguments}, value::undefined());
        const std::string interpreted = cx.to_string(cx.global("OUT"));

        for (std::size_t slot = 0; slot < std::size(all); ++slot) {
            if (every[slot] != nullptr) { every[slot]->aot_entry = all[slot].entry; }
        }
        cx.call(cx.global("drive"), std::span<const value>{arguments}, value::undefined());
        const std::string whole = cx.to_string(cx.global("OUT"));
        for (std::size_t slot = 0; slot < std::size(all); ++slot) {
            if (every[slot] != nullptr) { every[slot]->aot_entry = nullptr; }
        }

        ++arms;
        if (interpreted != whole) {
            std::printf("AOT %-10s FAILED - every entry installed\n    interpreted %s\n"
                        "    compiled    %s\n",
                        each.name, interpreted.c_str(), whole.c_str());
            ++disagreed;
            ++failures;
        }
    }
    std::printf("\nAOT: %u arms with all %zu entries installed, %u disagreed\n", arms,
                std::size(all), disagreed);

    if (failures == 0) {
        std::printf("\nall %zu bodies agree with the interpreter\n", std::size(subjects));
    }
    return failures == 0 ? 0 : 1;
}
