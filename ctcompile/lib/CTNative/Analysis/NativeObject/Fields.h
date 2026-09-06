#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctnative::object_detail {

bool scalarFieldEnvironment(mlir::ModuleOp module);
bool scalarFieldUse(mlir::OpOperand & use);

} // namespace ctcompile::ctnative::object_detail
