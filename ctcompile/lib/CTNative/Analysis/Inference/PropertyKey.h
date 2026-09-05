#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"

namespace ctcompile::ctnative::inference_detail {
// The constant string a property key operand carries, or empty.
inline llvm::StringRef constantKey(mlir::Value key) {
    auto constant = key.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return {}; }
    auto str = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
    return str ? str.getValue() : llvm::StringRef{};
}

} // namespace ctcompile::ctnative::inference_detail
