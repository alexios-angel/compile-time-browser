#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace ctcompile::ctnative {

class OwnedGlobalRoots;

inline constexpr llvm::StringLiteral kNativeObjectIdentity = "ctnative.object_identity";
inline constexpr llvm::StringLiteral kNativeObjectFieldGroup = "ctnative.object_field_group";

int64_t nativeObjectFieldGroup(mlir::Operation * op);

struct NativeObjectFieldPresence {
    bool assigned = false;
    bool exhausted = false;
    uint64_t work = 0;
};

// A fresh, bounded same-invocation query. Standard Map method recognition is
// an upstream prerequisite; schema groups, map_present and host reports never
// establish an allocation origin or field initialization. Saved get results
// retain their original object across Map mutations. Unknown effects, calls
// and loop-carried origins withhold the proof. This removes only the implicit
// absence seed from inference, never an explicitly stored value's type.
NativeObjectFieldPresence queryNativeObjectFieldPresence(mlir::Operation * read,
                                                         uint64_t maxWork = 100000);

// Prove owning identities in Map keys/values, strict comparisons, closed calls/returns,
// immutable captures and structured flow. Map payloads can join exact scalars;
// every object alias retains an owner. Ordinary scalar fields require separate
// receiver/type admission; publication and outgoing ownership edges refuse.
// Comparison-only field families check their own closed source environment;
// the optional live global-owner proof permits only its actual ordinary roots.
void prepareNativeObjectIdentities(mlir::ModuleOp module,
                                   const OwnedGlobalRoots * globals = nullptr);

} // namespace ctcompile::ctnative
