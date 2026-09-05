#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

inline constexpr llvm::StringLiteral kNativeObjectIdentity = "ctnative.object_identity";

// Prove property-free object allocations used only as Map keys or carried
// through closed calls/returns. The Map and every alias retain an owner.
void prepareNativeObjectIdentities(mlir::ModuleOp module);

} // namespace ctcompile::ctnative
