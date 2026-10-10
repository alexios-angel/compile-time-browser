//===- RegisterFlow.h - Follow values through block arguments ---*- C++ -*-===//
//
// The importer models the bytecode register file as block arguments, so one
// JavaScript value may travel through many of them. These queries follow a
// value forward to its real uses, or backward to its origin, across branch
// edges.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "mlir/IR/Value.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>

namespace ctcompile::ctjs::globals_detail {

/// Returns every use of \p value that is not a successor operand, following
/// successor operands into the block arguments they feed. Returns nullopt if
/// \p maxSteps runs out.
///
/// Status checks forward both their normal register vector and their
/// pre-operation handler snapshot; this query follows both edges and does not
/// assume the operation cannot throw. The returned operand pointers are valid
/// only until the caller mutates IR. When supplied, \p steps is reset and
/// receives the work consumed on every exit, including failure, so a caller
/// can share one budget across several queries.
std::optional<llvm::SmallVector<mlir::OpOperand *>> registerFlowUses(mlir::Value value,
                                                                     unsigned maxSteps = 4096,
                                                                     unsigned * steps = nullptr);

/// Whether \p value, followed backward through block arguments, always comes
/// from \p source. Every incoming definition must be this exact source,
/// including both arms of a branch to the same successor. The source must be
/// reached at least once, so a dead self-contained cycle never manufactures a
/// callee identity. Returns false if \p maxSteps runs out; \p steps works as
/// in registerFlowUses().
bool registerFlowHasOrigin(mlir::Value value, mlir::Value source, unsigned maxSteps = 4096,
                           unsigned * steps = nullptr);

/// The one value \p value always comes from, followed backward through block
/// arguments, or null when two incoming definitions differ, an entry-block or
/// unreachable argument is reached, or \p maxSteps runs out. A value that is
/// not a non-entry block argument is its own origin.
mlir::Value registerFlowOrigin(mlir::Value value, unsigned maxSteps = 4096);

} // namespace ctcompile::ctjs::globals_detail
