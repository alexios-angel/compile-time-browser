//===- NativeObjectIdentity.h - Object identity in native code --*- C++ -*-===//
//
// Proofs that let native code represent JavaScript objects by owned
// identities: objects used as Map keys or values, compared with ===, passed
// through closed calls or captured, where every alias keeps an owner.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace ctcompile::ctnative {

class OwnedGlobalRoots;

/// Attribute names prepareNativeObjectIdentities() writes.
inline constexpr llvm::StringLiteral kNativeObjectIdentity = "ctnative.object_identity";
inline constexpr llvm::StringLiteral kNativeObjectFieldGroup = "ctnative.object_field_group";

/// The kNativeObjectFieldGroup id on `op`, or -1 when it has none.
int64_t nativeObjectFieldGroup(mlir::Operation * op);

/// True for a use that is the payload of an unboxed cell whose every capture
/// was taken by a lifted closure: the cell is gone by emission, so the use is
/// not an escape.
bool erasedCapture(mlir::OpOperand & use);

/// Result of queryNativeObjectFieldPresence(): whether the field is assigned
/// on every path, whether the budget ran out, and the work spent.
struct NativeObjectFieldPresence {
    bool assigned = false;
    bool exhausted = false;
    uint64_t work = 0;
};

/// Proves that the object field `read` reads was assigned earlier in the same
/// invocation, computed fresh within `maxWork` steps. Standard Map methods must
/// already be recognized; schema groups, `map_present` and host reports never
/// count as evidence of where an object came from or that a field was set. A
/// saved `get` result keeps its original object across Map mutations. Unknown
/// effects, calls and loop-carried origins withhold the proof. A positive
/// answer only removes the implicit "field absent" (`undefined`) starting
/// point from type inference; it never changes an explicitly stored value's
/// type.
NativeObjectFieldPresence queryNativeObjectFieldPresence(mlir::Operation * read,
                                                         uint64_t maxWork = 100000);

/// Marks objects that can be owned identities in native code: used as Map
/// keys or values, in strict comparisons, through closed calls and returns,
/// immutable captures and structured control flow. Map payloads may mix such
/// objects with exact scalars; every alias of an object keeps an owner.
/// Ordinary scalar fields need their own receiver and type checks; publishing
/// an object or giving away ownership refuses. Field groups used only for
/// comparison check their own closed source environment. When `globals` is
/// given, objects reached from global roots are allowed only through that
/// proof's actual roots.
void prepareNativeObjectIdentities(mlir::ModuleOp module,
                                   const OwnedGlobalRoots * globals = nullptr);

} // namespace ctcompile::ctnative
