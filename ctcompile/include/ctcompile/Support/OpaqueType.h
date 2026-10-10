//===- OpaqueType.h - EmitC opaque type test --------------------*- C++ -*-===//
//
// A one-line predicate for matching an EmitC opaque type by its spelling.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "mlir/Dialect/EmitC/IR/EmitC.h"

namespace ctcompile {

/// Returns true if `type` is the EmitC opaque type spelled exactly `name`.
inline bool isOpaque(mlir::Type type, llvm::StringRef name) {
    auto opaque = llvm::dyn_cast<mlir::emitc::OpaqueType>(type);
    return opaque && opaque.getValue() == name;
}

} // namespace ctcompile
