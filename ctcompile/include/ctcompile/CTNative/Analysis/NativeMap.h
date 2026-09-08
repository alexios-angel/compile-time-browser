//===- NativeMap.h - proofs for confined standard Map instances -----------===//
#ifndef CTCOMPILE_CTNATIVE_ANALYSIS_NATIVEMAP_H
#define CTCOMPILE_CTNATIVE_ANALYSIS_NATIVEMAP_H

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"

#include <cstdint>

namespace ctcompile::ctnative {

class OwnedGlobalRoots;

inline constexpr llvm::StringLiteral kNativeMapSite = "ctnative.map_site";
inline constexpr llvm::StringLiteral kNativeMapAction = "ctnative.map_action";
inline constexpr llvm::StringLiteral kNativeMapMethod = "ctnative.map_method";
inline constexpr llvm::StringLiteral kNativeMapConstructor = "ctnative.map_constructor";
inline constexpr llvm::StringLiteral kNativeMapReason = "ctnative.map_reason";
inline constexpr llvm::StringLiteral kNativeMapGroup = "ctnative.map_group";
inline constexpr llvm::StringLiteral kNativeMapArgGroups = "ctnative.map_arg_groups";
inline constexpr llvm::StringLiteral kNativeMapPresent = "ctnative.map_present";
// A live same-instance/key last-write proof, independent of the family schema.
inline constexpr llvm::StringLiteral kNativeMapReadType = "ctnative.map_read_type";
inline constexpr llvm::StringLiteral kNativeMapSnapshotCopy = "ctnative.map_snapshot_copy";
inline constexpr llvm::StringLiteral kNativeMapSnapshotBuiltin = "ctnative.map_snapshot_builtin";

/// Annotate only after proving both the standard constructor/method identity
/// and every instance use. No runtime assumption or boxed fallback is added.
void prepareNativeMaps(mlir::ModuleOp module, const OwnedGlobalRoots * globals = nullptr);

/// The operation performed by a proved call or size read, or empty.
llvm::StringRef nativeMapAction(mlir::Operation * op);

/// A proved keys/values snapshot or its confined standard Array.from copy.
bool isNativeMapSnapshot(mlir::Operation * op);

/// Schema family shared through set results, closed call arguments and returns.
/// A family may contain distinct runtime instances; it is not an identity proof.
/// Returns -1 when the value has no proved Map flow.
int64_t nativeMapGroup(mlir::Value value);

/// These values name an erased standard constructor or method, never data.
bool isNativeMapBookkeeping(mlir::Operation * op);

} // namespace ctcompile::ctnative
#endif
