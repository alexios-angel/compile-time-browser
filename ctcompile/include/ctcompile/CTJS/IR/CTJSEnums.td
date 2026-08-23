//===- CTJSEnums.td - enumerated attributes -------------------------------===//
//
// EMPTY, AND REAL. There is no enum in the IR yet - Phase 7 defines types and
// no operations - but the policy's CMakeLists names this file unconditionally,
// and mlir-tblgen emits valid empty .inc files for a record-less input. Keeping
// the file means the build wiring is the policy's verbatim, rather than the
// policy's minus the lines that had nothing to generate yet.
//
// "Never hand-write an enum that appears in IR" - the policy. Everything that
// becomes one goes here.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTJS_ENUMS
#define CTCOMPILE_CTJS_ENUMS

include "ctcompile/CTJS/IR/CTJSBase.td"

#endif // CTCOMPILE_CTJS_ENUMS
