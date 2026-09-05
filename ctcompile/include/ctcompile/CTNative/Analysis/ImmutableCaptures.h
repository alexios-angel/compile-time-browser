#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctnative {

// Construction records code identity without invoking the body. This initial
// boundary accepts local captures and rejects mutable or inherited slots.
ctjs::FuncOp immutableClosureTarget(ctjs::CreateClosureOp made, mlir::ModuleOp module);
bool immutableCaptureCell(ctjs::CreateCellOp cell, mlir::ModuleOp module);
ctjs::CellSetOp captureCellWrite(ctjs::CreateCellOp cell);
bool constructsClosures(ctjs::FuncOp function);

} // namespace ctcompile::ctnative
