#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

ec::CallOpaqueOp callWithConstValueOperands(mlir::OpBuilder & builder, mlir::Location where,
                                            mlir::TypeRange results, mlir::StringAttr callee,
                                            mlir::ValueRange operands) {
    auto call = ec::CallOpaqueOp::create(builder, where, results, callee, operands);
    llvm::SmallVector<int32_t> readOnly;
    for (auto [index, operand] : llvm::enumerate(operands)) {
        if (!llvm::isa<ec::LValueType>(operand.getType())) {
            readOnly.push_back(static_cast<int32_t>(index));
        }
    }
    call->setAttr("ctnative.const_operands", builder.getDenseI32ArrayAttr(readOnly));
    return call;
}

} // namespace ctcompile::ctnative::lowering_detail
