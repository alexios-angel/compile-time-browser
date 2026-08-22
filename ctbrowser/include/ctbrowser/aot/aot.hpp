#pragma once

#include <ctbrowser/aot/aot_entry.h>

#include <cstdint>

// THE AOT RUNTIME ABI, as the runtime declares it.
//
// The table is aot_helpers.def and it is the source of truth; this header is
// one of its two expansions. The other is in ctcompile, which includes THE SAME
// FILE - that is the whole mechanism, and it is why a renamed or misspelled
// helper is a compile error in the compiler rather than a link error, a crash,
// or a silently wrong call (Principle 11).
//
// NO DEFINITIONS YET. Phases 4 and 6 write the bodies; Phase 2 is the contract,
// and a contract is useful before its implementation exists precisely because
// everything downstream is written against it. Declaring them costs nothing: a
// prototype nobody calls links fine.
//
// A RUNTIME-ONLY BUILD PAYS NOTHING FOR THIS. There is no LLVM here, no
// TableGen, no generator - an X-macro and <cstdint>, which is what keeps the
// engine buildable on a machine with no compiler toolchain installed at all
// (Principles 2 and 8).
namespace ctbrowser::aot {

// The five opaque types the ABI passes by pointer. Opaque ON PURPOSE: their
// layouts belong to Phase 4 and must stay changeable, while their existence and
// their names are part of the contract now.
//
//   ct_aot_ctx    the script context a helper acts on
//   ct_aot_frame  one compiled function's frame: its register span, its depth,
//                 its handler base. What makes its values reachable by the
//                 precise collector, which walks only the roots in GCRoots.def
//   ct_aot_site   a call or property site's identity, stable across executions.
//                 Where Phase 26 attaches an inline cache WITHOUT an ABI break
//   ct_aot_ic     one inline cache's storage, caller-allocated
//   ct_aot_name   an interned property name, prehashed once
// Declared in aot_entry.h, beside the entry signature that needs them, so there
// is exactly one declaration of each rather than two that can drift.

// EVERY HELPER, AS AN ENUMERATOR. An ODS operation names its helper by
// identifier and that identifier is concatenated into a reference to this
// enum inside generated C++, so a helper that is renamed or removed stops the
// compiler's build at the point of use.
enum class helper_id : std::uint16_t {
#define CT_AOT_HELPER(name, ret, params, may_throw, may_reenter, is_safepoint) name,
#include <ctbrowser/aot/aot_helpers.def>
    count
};

// How many there are, for anyone sizing a table by it.
inline constexpr std::size_t helper_count = static_cast<std::size_t>(helper_id::count);

// AND THE PROTOTYPES. extern "C" because the LLVM dialect backend emits
// LLVMFuncOp declarations against exactly these symbols; the EmitC backend
// calls the same semantics as ordinary C++ so the host compiler can inline the
// fast paths, which is the reason EmitC is the primary backend at all.
extern "C" {
#define CT_AOT_HELPER(name, ret, params, may_throw, may_reenter, is_safepoint) ret name params;
#include <ctbrowser/aot/aot_helpers.def>
}

// WHAT A HELPER ANSWERS WITH, and it did not exist until now: the four names
// below are cited thirty-five times in aot_helpers.def and were defined
// nowhere, so twenty-four prototypes returned a bare `int32_t` whose meaning
// lived only in prose - and the table already assumes ctcompile will `switch`
// on it, which needs a type.
//
// THE UNDERLYING TYPE IS TAKEN FROM THE TABLE rather than written down twice.
// `ct_aot_check` is the classifier and its `ret` column IS this vocabulary's
// type; deriving it means a change to that column moves this enum instead of
// silently disagreeing with it.
//
// THE PRECEDENCE IS THE CONTRACT; THE NUMBERS ARE NOT. aot_helpers.def:157-163
// fixes the ORDER a classifier must test in - unwound first, because the
// call_frame is destroyed and every later test dereferences it; then failed,
// because the run loop's own head tests it in the same disjunction and leaves
// regardless of a handler that just fired; then caught; then ok. It fixes no
// numeric value, and none is invented here: nothing may depend on `ok` being
// zero until something measures a reason for it.
//
// NOT DECLARED, AND DELIBERATELY: `CT_AOT_PAD_BIT` and `CT_AOT_FRAME_BYTES`,
// the other two names the table cites. Both are Phase 4 LAYOUT decisions - one
// picks a spare bit in `call_frame::ip`, the other sizes a caller-allocated
// block - and inventing either here would freeze a choice with no measurement
// behind it into a header two backends will read. They are named here so the
// gap is written down rather than discovered.
enum class ct_aot_status : decltype(ct_aot_check(static_cast<ct_aot_frame *>(nullptr))) {
    unwound, // the frame is GONE; test this first, the rest dereference it
    failed,  // the uncatchable tier: no try/catch can see it
    caught,  // a handler in THIS frame won; resume at its pad
    ok,      // nothing happened
};

// AND THE ORDERING VOCABULARY, which was in the same position the status enum
// was in before it existed: ct_aot_compare's row names CT_AOT_ORD_LESS,
// EQUIVALENT, GREATER and UNORDERED, spells out that "is_lteq is
// (ord == -1 || ord == 0), false for UNORDERED, which is what makes
// `NaN <= NaN` false" - and nothing defined any of them.
//
// THE NUMBERS ARE PART OF THE CONTRACT HERE, unlike the status enum's. The row
// writes them down as -1, 0, 1 and 2 and derives the four relational opcodes
// from constant comparisons against them, so a backend that picked its own
// would produce code that reads correctly and compares wrongly.
enum class ct_aot_ordering : decltype(ct_aot_compare(static_cast<ct_aot_frame *>(nullptr), 0, 0,
                                                     static_cast<int32_t *>(nullptr))) {
    less = -1,
    equivalent = 0,
    greater = 1,
    // NOT "incomparable with itself" - this is what a NaN on either side, and
    // only that, produces. Every one of the four relational operators is FALSE
    // against it, including `>=`, which is why they cannot be lowered as the
    // negation of one another.
    unordered = 2,
};

} // namespace ctbrowser::aot
