#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctnative {
class OwnedGlobalRoots;
namespace object_detail {

bool scalarFieldEnvironment(mlir::ModuleOp module, const OwnedGlobalRoots * globals = nullptr);
bool scalarFieldUse(mlir::OpOperand & use);

} // namespace object_detail
} // namespace ctcompile::ctnative
