#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"

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
    return ctjs::stringAttrOf(op, kNativeMethodTable);
}

inline llvm::StringRef environmentTarget(mlir::Operation * op) {
    return ctjs::stringAttrOf(op, kNativeEnvironment);
}

// The slot list `name` (kNativeObjectArgs, kNativeCellArgs, ...) on `op`, or
// empty.
inline llvm::ArrayRef<int32_t> listedSlots(mlir::Operation * op, llvm::StringRef name) {
    auto listed = op->getAttrOfType<mlir::DenseI32ArrayAttr>(name);
    return listed ? listed.asArrayRef() : llvm::ArrayRef<int32_t>{};
}

inline bool listsSlot(mlir::Operation * op, llvm::StringRef name, unsigned index) {
    return llvm::is_contained(listedSlots(op, name), static_cast<int32_t>(index));
}

} // namespace ctcompile::ctnative
