//===- Analysis.h - Function reachability analysis --------------*- C++ -*-===//
//
// Finds the private ctjs functions that no root can reach, for
// --ctnative-prune-unreachable to delete.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <string>

namespace ctcompile::ctnative::reachability {

/// The outcome of analyze().
struct result {
    /// Functions proved unreachable. Empty whenever `reason` is set.
    llvm::SmallVector<ctjs::FuncOp> dead;
    /// Why the proof was refused, or empty when it succeeded.
    std::string reason;
    /// Work units spent, bounded by the `maxSteps` given to analyze().
    uint64_t steps = 0;
    /// Number of functions kept.
    unsigned retained = 0;
};

/// Analyzes one flat ctjs module without changing it. Any unsupported
/// reference or exhausted budget empties the deletion set. Existing
/// annotations on the IR are never trusted as evidence.
result analyze(mlir::ModuleOp module, unsigned maxSteps);

} // namespace ctcompile::ctnative::reachability
