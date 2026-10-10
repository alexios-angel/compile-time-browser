//===- Passes.h - CTJS pass declarations ------------------------*- C++ -*-===//
//
// CTJS passes (ctjs-lower-to-emitc, ctjs-lift-to-scf, ctjs-resolve-globals,
// ...), declared and registered from Passes.td. The generated registration
// turns each pass's `dependentDialects` into `registry.insert<Dialect>()`,
// which needs the full class definitions, so those dialect headers are
// included here.
//
//===----------------------------------------------------------------------===//

#pragma once
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
