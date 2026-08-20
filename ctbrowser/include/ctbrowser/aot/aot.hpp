#pragma once

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
extern "C" {
struct ct_aot_ctx;
struct ct_aot_frame;
struct ct_aot_site;
struct ct_aot_ic;
struct ct_aot_name;
}

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

} // namespace ctbrowser::aot
