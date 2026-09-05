#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"

namespace ctcompile::ctnative {

// Written only by the returned-closure proof. A monomorphic callable keeps
// its code in the call graph and carries its immutable captures by value.
inline constexpr llvm::StringLiteral kNativeEnvironment = "ctnative.environment";
inline constexpr llvm::StringLiteral kNativeEnvironmentRead = "ctnative.environment_read";
inline constexpr llvm::StringLiteral kNativeStoredCallable = "ctnative.stored_callable";
inline constexpr llvm::StringLiteral kNativeStoredCall = "ctnative.stored_call";
inline constexpr llvm::StringLiteral kNativeStoredRead = "ctnative.stored_capture_read";
inline constexpr llvm::StringLiteral kNativeMethodTable = "ctnative.method_table";
inline constexpr llvm::StringLiteral kNativeTableField = "ctnative.table_field";

inline llvm::StringRef methodTableName(mlir::Operation * op) {
    auto name = op->getAttrOfType<mlir::StringAttr>(kNativeMethodTable);
    return name ? name.getValue() : llvm::StringRef{};
}

inline llvm::StringRef environmentTarget(mlir::Operation * op) {
    auto target = op->getAttrOfType<mlir::StringAttr>(kNativeEnvironment);
    return target ? target.getValue() : llvm::StringRef{};
}

} // namespace ctcompile::ctnative
