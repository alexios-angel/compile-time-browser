#pragma once

#include "mlir/Dialect/EmitC/IR/EmitC.h"

namespace ctcompile {

// `type` IS THE EMITC OPAQUE TYPE SPELLED `name`, exactly.
inline bool isOpaque(mlir::Type type, llvm::StringRef name) {
    auto opaque = llvm::dyn_cast<mlir::emitc::OpaqueType>(type);
    return opaque && opaque.getValue() == name;
}

} // namespace ctcompile
