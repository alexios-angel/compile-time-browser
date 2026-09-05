#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace ctcompile::ctnative::partial_eval {

// Reuse native closure and Map admission on a private copy. Original signatures
// and operations stay intact; only Map facts with a surviving origin transfer.
struct closureHeapProof {
    llvm::SmallPtrSet<mlir::Operation *, 16> closedCalls;
    bool checkedEnvironment = false;
};
closureHeapProof prepareClosureHeapFacts(
    mlir::ModuleOp module, llvm::function_ref<bool(mlir::ModuleOp)> checkEnvironment =
                               [](mlir::ModuleOp) { return false; });

} // namespace ctcompile::ctnative::partial_eval
