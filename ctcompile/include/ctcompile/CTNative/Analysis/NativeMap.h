//===- NativeMap.h - proofs for confined standard Map instances -----------===//
#ifndef CTCOMPILE_CTNATIVE_ANALYSIS_NATIVEMAP_H
#define CTCOMPILE_CTNATIVE_ANALYSIS_NATIVEMAP_H

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctnative {

inline constexpr llvm::StringLiteral kNativeMapSite = "ctnative.map_site";
inline constexpr llvm::StringLiteral kNativeMapAction = "ctnative.map_action";
inline constexpr llvm::StringLiteral kNativeMapMethod = "ctnative.map_method";
inline constexpr llvm::StringLiteral kNativeMapConstructor = "ctnative.map_constructor";
inline constexpr llvm::StringLiteral kNativeMapReason = "ctnative.map_reason";

/// Annotate only after proving both the standard constructor/method identity
/// and every instance use. No runtime assumption or boxed fallback is added.
void prepareNativeMaps(mlir::ModuleOp module);

/// The operation performed by a proved call or size read, or empty.
llvm::StringRef nativeMapAction(mlir::Operation * op);

/// Follow only the identity-preserving result of Map.set back to its new Map.
ctjs::ConstructOp nativeMapRoot(mlir::Value value);

/// These values name an erased standard constructor or method, never data.
bool isNativeMapBookkeeping(mlir::Operation * op);

} // namespace ctcompile::ctnative
#endif
