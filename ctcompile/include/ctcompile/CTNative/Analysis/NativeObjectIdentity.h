#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

inline constexpr llvm::StringLiteral kNativeObjectIdentity = "ctnative.object_identity";
inline constexpr llvm::StringLiteral kNativeObjectFieldGroup = "ctnative.object_field_group";

int64_t nativeObjectFieldGroup(mlir::Operation * op);

// Prove owning identities in Map keys/values, closed calls/returns,
// immutable captures and structured flow. Map payloads can join exact scalars;
// every object alias retains an owner. Ordinary scalar fields require separate
// receiver/type admission; publication and outgoing ownership edges refuse.
void prepareNativeObjectIdentities(mlir::ModuleOp module);

} // namespace ctcompile::ctnative
