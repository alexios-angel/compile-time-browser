#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"

namespace ctcompile::ctnative {

// Written only by the returned-closure proof. A monomorphic callable keeps
// its code in the call graph and carries its immutable captures by value.
inline constexpr llvm::StringLiteral kNativeEnvironment = "ctnative.environment";
inline constexpr llvm::StringLiteral kNativeEnvironmentRead = "ctnative.environment_read";

inline llvm::StringRef environmentTarget(mlir::Operation * op) {
    auto target = op->getAttrOfType<mlir::StringAttr>(kNativeEnvironment);
    return target ? target.getValue() : llvm::StringRef{};
}

} // namespace ctcompile::ctnative
