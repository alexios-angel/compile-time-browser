#include "ctcompile/CTNative/IR/CTNativeOps.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/OpImplementation.h"
#include "llvm/ADT/STLExtras.h"

#define GET_OP_CLASSES
#include "ctcompile/CTNative/IR/CTNativeOps.cpp.inc"

namespace ctcompile::ctnative {
namespace {

bool isExceptionPayload(mlir::Type type) {
    if (type.isF64() || type.isSignlessInteger(1)) { return true; }
    auto opaque = llvm::dyn_cast<mlir::emitc::OpaqueType>(type);
    return opaque && opaque.getValue() == "std::string";
}

// A throw in a catch body belongs to an enclosing handler, never to the
// handler already executing. Check the nearest protected region even through
// nested ifs/tries; JavaScript has no payload-dependent catch filtering.
CppTryOp protectedBy(CppThrowOp thrown) {
    for (auto * region = thrown->getParentRegion(); region; region = region->getParentRegion()) {
        auto attempt = llvm::dyn_cast_or_null<CppTryOp>(region->getParentOp());
        if (attempt && region == &attempt.getBody()) { return attempt; }
        if (region->getParentOp() &&
            region->getParentOp()->hasTrait<mlir::OpTrait::IsIsolatedFromAbove>()) {
            break;
        }
    }
    return {};
}

} // namespace

mlir::LogicalResult CppTryOp::verify() {
    if (!llvm::hasSingleElement(getBody()) || !llvm::hasSingleElement(getCatchBody())) {
        return emitOpError("requires one block in each try and catch region");
    }
    if (getBody().front().getNumArguments() != 0) {
        return emitOpError("requires no try region arguments");
    }
    mlir::Block & handler = getCatchBody().front();
    if (handler.getNumArguments() != 1 || !isExceptionPayload(handler.getArgument(0).getType())) {
        return emitOpError("requires exactly one f64, i1 or owning std::string catch argument");
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
    const auto matched = getBody().walk([&](CppThrowOp thrown) {
        if (protectedBy(thrown) == *this &&
            thrown.getValue().getType() != handler.getArgument(0).getType()) {
            emitOpError("requires each protected throw to match the homogeneous catch payload");
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    if (matched.wasInterrupted()) { return mlir::failure(); }
    return mlir::success();
}

mlir::LogicalResult CppThrowOp::verify() {
    if (!isExceptionPayload(getValue().getType())) {
        return emitOpError("requires an f64, i1 or owning std::string payload");
    }
    mlir::Operation * next = getOperation()->getNextNode();
    if (!next || !next->hasTrait<mlir::OpTrait::IsTerminator>()) {
        return emitOpError("must be immediately followed by the enclosing region terminator");
    }
    return mlir::success();
}

} // namespace ctcompile::ctnative
