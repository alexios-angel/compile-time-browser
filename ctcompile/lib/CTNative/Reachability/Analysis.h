#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <string>

namespace ctcompile::ctnative::reachability {

struct result {
    llvm::SmallVector<ctjs::FuncOp> dead;
    std::string reason;
    uint64_t steps = 0;
    unsigned retained = 0;
};

// Analyze one flat CTJS module without changing it. Any unsupported reference
// or exhausted budget clears the deletion set; annotations confer no authority.
result analyze(mlir::ModuleOp module, unsigned maxSteps);

} // namespace ctcompile::ctnative::reachability
