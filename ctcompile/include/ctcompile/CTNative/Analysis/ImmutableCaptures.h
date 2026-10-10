//===- ImmutableCaptures.h - Immutable closure captures ---------*- C++ -*-===//
//
// Helpers that prove a closure's captures never change after it is created,
// so creating the closure only records which function it runs and never
// executes the body. Local captures are accepted; mutable or inherited
// (upvalue-of-upvalue) slots are refused.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctnative {

/// The function `made` runs, when `made` is the only creation of it and every
/// capture is local and immutable; null otherwise.
ctjs::FuncOp immutableClosureTarget(ctjs::CreateClosureOp made, mlir::ModuleOp module);
/// True when `cell` is written at most once and is otherwise only read or
/// captured by closures that immutableClosureTarget() accepts.
bool immutableCaptureCell(ctjs::CreateCellOp cell, mlir::ModuleOp module);
/// The first `ctjs.cell_set` that writes `cell`, or null.
ctjs::CellSetOp captureCellWrite(ctjs::CreateCellOp cell);
/// True when `function`'s body creates any closure.
bool constructsClosures(ctjs::FuncOp function);

} // namespace ctcompile::ctnative
