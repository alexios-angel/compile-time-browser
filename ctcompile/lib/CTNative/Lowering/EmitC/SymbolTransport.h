#pragma once

#include "mlir/Dialect/EmitC/IR/EmitC.h"

namespace ctcompile::ctnative::lowering_detail {

// SCF-to-EmitC declares state before assigning it. Keep that storage disengaged
// until a proved incoming Symbol initializes it, without creating a JS identity.
void transportSymbols(mlir::emitc::FuncOp function);

} // namespace ctcompile::ctnative::lowering_detail
