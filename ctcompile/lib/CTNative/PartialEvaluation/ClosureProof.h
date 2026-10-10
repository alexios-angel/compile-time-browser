//===- ClosureProof.h - Closure facts for the evaluator ---------*- C++ -*-===//
//
// Lets the partial evaluator reuse the native closure lifter and Map
// admission: they run on a private copy of the module, and the facts they
// prove are copied back to the original operations.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <optional>

namespace ctcompile::ctnative::partial_eval {

/// Every indexed source function by its numeric identity; nullopt when two
/// functions share one.
std::optional<llvm::DenseMap<unsigned, ctjs::FuncOp>> uniqueFunctionIndex(mlir::ModuleOp module);

/// What prepareClosureHeapFacts proved on its private copy.
struct closureHeapProof {
    /// Original ctjs.call operations that the closure lift turned into a
    /// ctjs.call_direct with a visible body, so their callee is known.
    llvm::SmallPtrSet<mlir::Operation *, 16> closedCalls;
    /// Whether the lifted copy passed the caller's environment check;
    /// closedCalls may be trusted only when it did.
    bool checkedEnvironment = false;
};
/// Runs native closure lifting and Map admission on a private copy of `module`
/// and transfers the results back. The original's signatures and operations
/// stay intact; only Map attributes whose origin operation survived are
/// copied. `checkEnvironment` is applied to the lifted copy.
closureHeapProof prepareClosureHeapFacts(
    mlir::ModuleOp module, llvm::function_ref<bool(mlir::ModuleOp)> checkEnvironment =
                               [](mlir::ModuleOp) { return false; });

} // namespace ctcompile::ctnative::partial_eval
