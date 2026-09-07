#pragma once

#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "llvm/ADT/DenseSet.h"

#include <string>

namespace ctcompile::ctnative::map_detail {

struct snapshotCopies {
    llvm::DenseSet<mlir::Operation *> builtins;
    llvm::DenseSet<mlir::Operation *> calls;
};

// Collect candidates without trusting or publishing input annotations. The
// caller also proves the module has no unknown calls or global reflection.
// Each iterator must have one immediate Array.from consumer, so its eager
// native vector cannot expose iterator state or move a read across effects.
std::string collectSnapshotCopies(mlir::ModuleOp module,
                                  const llvm::DenseSet<mlir::Operation *> & mapCalls,
                                  snapshotCopies & out);

} // namespace ctcompile::ctnative::map_detail
