//===- Fields.h - Scalar object field checks --------------------*- C++ -*-===//
//
// Helpers shared by the native object analyses for checking that plain-object
// field accesses cannot run user code.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctnative {
class OwnedGlobalRoots;
namespace object_detail {

/// Whether no operation in `module` can reach a getter, setter or prototype
/// change. Reads of Map snapshots captured by a global that `globals` proved
/// owned are allowed too.
bool scalarFieldEnvironment(mlir::ModuleOp module, const OwnedGlobalRoots * globals = nullptr);
/// Whether `use` is the object operand of a property access with an ordinary
/// constant key.
bool scalarFieldUse(mlir::OpOperand & use);

} // namespace object_detail
} // namespace ctcompile::ctnative
