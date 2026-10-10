//===- aot.hpp - AOT runtime ABI --------------------------------*- C++ -*-===//
//
// The AOT runtime ABI as the runtime declares it: the helpers compiled code may
// call, the status they return, and the ordering vocabulary of ct_aot_compare.
//
// aot_helpers.def (with the tables in helpers/*.def) is the single source of
// truth. This header is one expansion of it; ctcompile includes the same file
// for the other, so a renamed or misspelled helper is a compile error in the
// compiler, not a link error or a silently wrong call.
//
// The definitions live in lib/Script/aot_bridge/. This header only declares
// them, so code compiled against the ABI does not depend on how the bodies are
// written.
//
// It uses only an X-macro and <cstdint> (no LLVM, no TableGen), so the engine
// still builds on a machine with no compiler toolchain.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ctbrowser/aot/aot_entry.h>

#include <cstdint>

namespace ctbrowser::aot {

// The five opaque types the ABI passes by pointer. They are opaque so their
// layouts can change without changing the ABI; only their names are fixed.
//
//   ct_aot_ctx    the script context a helper acts on
//   ct_aot_frame  one compiled function's frame: its register span, its depth,
//                 its handler base. It is what makes the frame's values
//                 reachable by the precise collector, which walks only the
//                 roots of context::each_root.
//   ct_aot_site   a call or property site's identity, stable across runs, so
//                 an inline cache can be attached later without an ABI break.
//   ct_aot_ic     one inline cache's storage, caller-allocated.
//   ct_aot_name   an interned property name, prehashed once.
// They are declared in aot_entry.h, beside the entry signature that needs
// them, so each has exactly one declaration.

/// Every helper, as an enumerator. An ODS operation names its helper by
/// identifier and that identifier is concatenated into a reference to this
/// enum inside generated C++, so a helper that is renamed or removed stops the
/// compiler's build at the point of use.
enum class helper_id : std::uint16_t {
#define CT_AOT_HELPER(name, ret, params, may_throw, may_reenter, is_safepoint) name,
#include <ctbrowser/aot/aot_helpers.def>
    count
};

/// The number of helpers, for sizing a table indexed by helper_id.
inline constexpr std::size_t helper_count = static_cast<std::size_t>(helper_id::count);

// The prototypes. extern "C" because the LLVM-dialect backend emits
// LLVMFuncOp declarations against exactly these symbols; the EmitC backend
// calls them as ordinary C++ so the host compiler can inline the fast paths,
// which is why EmitC is the primary backend.
extern "C" {
#define CT_AOT_HELPER(name, ret, params, may_throw, may_reenter, is_safepoint) ret name params;
#include <ctbrowser/aot/aot_helpers.def>
}

/// The status a helper returns. The helper table refers to these four names
/// and ctcompile switches on them, so they need one shared type.
///
/// The underlying type is taken from the `ret` column of the `ct_aot_check`
/// row rather than written down twice, so a change to that column moves this
/// enum instead of silently disagreeing with it.
///
/// The test order is the contract; the numeric values are not. The
/// `ct_aot_check` row in helpers/frames-and-bindings.def fixes the order a
/// classifier must test in: unwound first, because the call_frame is destroyed
/// and every later test dereferences it; then failed, because the run loop
/// leaves on it even if a handler just fired; then caught; then ok. No numeric
/// value is fixed, so nothing may rely on `ok` being zero.
///
/// `CT_AOT_PAD_BIT` (the spare bit in `call_frame::ip` that marks a caught
/// exception) and `CT_AOT_FRAME_BYTES` (the size of the caller-allocated frame
/// block), which the table also cites, are defined in aot_entry.h.
enum class ct_aot_status : decltype(ct_aot_check(static_cast<ct_aot_frame *>(nullptr))) {
    unwound, ///< The frame is gone; test this first, the rest dereference it.
    failed,  ///< An uncatchable failure: no try/catch can see it.
    caught,  ///< A handler in this frame caught it; resume at its pad.
    ok,      ///< Nothing happened.
};

/// The result of ct_aot_compare. The row in helpers/values-and-properties.def
/// names CT_AOT_ORD_LESS, EQUIVALENT, GREATER and UNORDERED and defines
/// `is_lteq` as `(ord == -1 || ord == 0)`, false for UNORDERED, which is what
/// makes `NaN <= NaN` false.
///
/// Unlike ct_aot_status, the numbers are part of the contract: the row fixes
/// them as -1, 0, 1 and 2 and derives the four relational opcodes from
/// constant comparisons against them, so a backend that picked its own values
/// would compare wrongly.
enum class ct_aot_ordering : decltype(ct_aot_compare(static_cast<ct_aot_frame *>(nullptr), 0, 0,
                                                     static_cast<int32_t *>(nullptr))) {
    less = -1,
    equivalent = 0,
    greater = 1,
    /// Produced only when either side is NaN. All four relational operators
    /// are false against it, including `>=`, which is why they cannot be
    /// lowered as the negation of one another.
    unordered = 2,
};

} // namespace ctbrowser::aot
