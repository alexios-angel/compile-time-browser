#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctjs {

void TryOp::getSuccessorRegions(mlir::RegionBranchPoint point,
                                llvm::SmallVectorImpl<mlir::RegionSuccessor> & regions) {
    if (point.isParent()) {
        regions.emplace_back(&getBody());
        return;
    }
    auto terminator = point.getTerminatorPredecessorOrNull();
    if (llvm::isa<TryExitOp>(terminator.getOperation())) { regions.emplace_back(&getCatchBody()); }
    regions.emplace_back(getOperation());
}

mlir::ValueRange TryOp::getSuccessorInputs(mlir::RegionSuccessor successor) {
    if (auto * region = successor.getSuccessor()) {
        return region->empty() ? mlir::ValueRange{} : region->front().getArguments();
    }
    return getOperation()->getResults();
}

void TryOp::getRegionInvocationBounds(llvm::ArrayRef<mlir::Attribute>,
                                      llvm::SmallVectorImpl<mlir::InvocationBounds> & bounds) {
    bounds.emplace_back(1, 1);
    bounds.emplace_back(0, 1);
}

mlir::LogicalResult TryOp::verify() {
    if (!llvm::hasSingleElement(getBody()) || !llvm::hasSingleElement(getCatchBody()) ||
        getBody().front().empty() || getCatchBody().front().empty()) {
        return emitOpError("requires a nonempty block in each try and catch region");
    }
    if (!getBody().front().getArguments().empty() ||
        getCatchBody().front().getNumArguments() == 0) {
        return emitOpError("requires an argument-free body and a catch payload argument");
    }
    for (auto argument : getCatchBody().front().getArguments()) {
        if (!llvm::isa<ValueType>(argument.getType())) {
            return emitOpError("catch arguments must be JavaScript values");
        }
    }
    if (!llvm::isa<TryExitOp>(getBody().front().getTerminator()) ||
        !llvm::isa<TryYieldOp>(getCatchBody().front().getTerminator())) {
        return emitOpError("requires try_exit in the body and try_yield in the catch");
    }
    return mlir::success();
}

mlir::MutableOperandRange TryExitOp::getMutableSuccessorOperands(mlir::RegionSuccessor point) {
    if (point.isRegion()) {
        return mlir::MutableOperandRange(getOperation(), 2, getNumOperands() - 2);
    }
    return mlir::MutableOperandRange(getOperation(), 1, 1);
}

void TryExitOp::getSuccessorRegions(llvm::ArrayRef<mlir::Attribute> operands,
                                    llvm::SmallVectorImpl<mlir::RegionSuccessor> & regions) {
    auto parent = llvm::cast<TryOp>((*this)->getParentOp());
    const auto flag = operands.empty()
                          ? mlir::IntegerAttr{}
                          : llvm::dyn_cast_or_null<mlir::IntegerAttr>(operands.front());
    if (!flag || !flag.getValue().isZero()) { regions.emplace_back(&parent.getCatchBody()); }
    if (!flag || flag.getValue().isZero()) { regions.emplace_back(parent.getOperation()); }
}

mlir::LogicalResult TryExitOp::verify() {
    auto parent = llvm::dyn_cast<TryOp>((*this)->getParentOp());
    if (!parent || (*this)->getParentRegion() != &parent.getBody()) {
        return emitOpError("must terminate a try body");
    }
    if (parent.getCatchBody().empty() ||
        getCaughtValues().size() != parent.getCatchBody().front().getNumArguments()) {
        return emitOpError("must carry the payload and every catch state value");
    }
    return mlir::success();
}

mlir::MutableOperandRange TryYieldOp::getMutableSuccessorOperands(mlir::RegionSuccessor) {
    return mlir::MutableOperandRange(getOperation());
}

mlir::LogicalResult TryYieldOp::verify() {
    auto parent = llvm::dyn_cast<TryOp>((*this)->getParentOp());
    if (!parent || (*this)->getParentRegion() != &parent.getCatchBody()) {
        return emitOpError("must terminate a catch body");
    }
    return mlir::success();
}

} // namespace ctcompile::ctjs
