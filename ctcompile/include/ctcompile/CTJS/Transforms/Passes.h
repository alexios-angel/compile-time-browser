#pragma once
// CTJS passes. None yet; the declarations and the registration function are
// generated from Passes.td.
// THE DIALECTS THE GENERATED HEADER NAMES. -gen-pass-decls turns each pass's
// `dependentDialects` into a `registry.insert<mlir::scf::SCFDialect>()`, which
// needs the class DEFINED and not merely declared - so a pass that depends on a
// dialect obliges this header to include it.
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/Pass/Pass.h"

namespace ctcompile::ctjs {

#define GEN_PASS_DECL
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

#define GEN_PASS_REGISTRATION
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

} // namespace ctcompile::ctjs
