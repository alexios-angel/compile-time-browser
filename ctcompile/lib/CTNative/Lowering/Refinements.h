//===- Refinements.h - Narrowing values a branch tested ---------*- C++ -*-===//
//
// Inserts and drops `ctnative.refine`, the value a dominating `typeof` test or
// truthiness branch proved to be of one kind.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/STLFunctionalExtras.h"

namespace ctcompile::ctnative {

/// Gives each value an `scf.if` tests a `ctnative.refine` at the start of the
/// region the test proves something in, and points that region's uses of the
/// value at it. The tests are `typeof v == "string"` or `"number"` (also with
/// `===`, and in the else-region under `!`, which is how `!=` arrives), and
/// `v` itself for the String methods a truthy value is read through. When `v`
/// is a read of a cell `constantCell` accepts (one dominating write, no
/// closure writes it: closureLifter::isConstantCell), the region's other reads
/// of that cell are the same value and are refined too.
void insertRefinements(mlir::ModuleOp module,
                       llvm::function_ref<bool(ctjs::CreateCellOp)> constantCell);

/// Replaces every `ctnative.refine` left under `root` by its operand. A
/// refinement only names a fact, so dropping one changes no behaviour.
void dropRefinements(mlir::Operation * root);

} // namespace ctcompile::ctnative
