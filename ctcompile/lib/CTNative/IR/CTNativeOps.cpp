#include "ctcompile/CTNative/IR/CTNativeOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/OpImplementation.h"
#include "llvm/ADT/STLExtras.h"

#define GET_OP_CLASSES
#include "ctcompile/CTNative/IR/CTNativeOps.cpp.inc"

namespace ctcompile::ctnative {

mlir::LogicalResult CppTryOp::verify() {
    if (!llvm::hasSingleElement(getBody()) || !llvm::hasSingleElement(getCatchBody())) {
        return emitOpError("requires one block in each try and catch region");
    }
    if (getBody().front().getNumArguments() != 0) {
        return emitOpError("requires no try region arguments");
    }
    mlir::Block & handler = getCatchBody().front();
    if (handler.getNumArguments() != 1 || !handler.getArgument(0).getType().isF64()) {
        return emitOpError("requires exactly one f64 catch argument");
    }
    for (mlir::Region * region : {&getBody(), &getCatchBody()}) {
        mlir::Block & block = region->front();
        if (block.empty()) {
            return emitOpError("requires non-empty blocks in both try and catch regions");
        }
        if (!mlir::isa<CppTryEndOp>(block.back())) {
            return emitOpError("requires ctnative.cpp_try_end as each try and catch terminator");
        }
    }
    return mlir::success();
}

mlir::LogicalResult CppThrowOp::verify() {
    mlir::Operation * next = getOperation()->getNextNode();
    if (!next || !next->hasTrait<mlir::OpTrait::IsTerminator>()) {
        return emitOpError("must be immediately followed by the enclosing region terminator");
    }
    return mlir::success();
}

} // namespace ctcompile::ctnative
