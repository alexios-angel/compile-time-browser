#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

inline constexpr llvm::StringLiteral kNativeObjectIdentity = "ctnative.object_identity";

// Prove property-free identities in Map keys/values, closed calls/returns,
// immutable captures and structured flow. Map payloads can join exact scalars;
// every object alias retains an owner. Property access and publication refuse.
void prepareNativeObjectIdentities(mlir::ModuleOp module);

} // namespace ctcompile::ctnative
