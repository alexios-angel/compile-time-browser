#pragma once

#include "mlir/IR/Value.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>

namespace ctcompile::ctjs::globals_detail {

// Follow only successor operands into block arguments. Status checks forward
// both their normal vector and their pre-operation handler snapshot; this
// query does not remove either edge or assert that its operation cannot throw.
// The returned operand pointers are valid only until the caller mutates IR.
std::optional<llvm::SmallVector<mlir::OpOperand *>> registerFlowUses(mlir::Value value,
                                                                     unsigned maxSteps = 4096);

// Every incoming definition must be this exact source, including both arms of
// a branch to the same successor. A reachable source is required, so a dead
// self-contained cycle never manufactures a callee identity.
bool registerFlowHasOrigin(mlir::Value value, mlir::Value source, unsigned maxSteps = 4096);

} // namespace ctcompile::ctjs::globals_detail
