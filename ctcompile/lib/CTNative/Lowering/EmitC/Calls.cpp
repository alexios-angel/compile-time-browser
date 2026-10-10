//===- Calls.cpp - EmitC call helpers ------------------------- -*- C++ -*-===//
//
// Small builders for emitc.call_opaque and emitc.member_call_opaque that the
// rest of the EmitC lowering uses to spell runtime calls.
//
//===----------------------------------------------------------------------===//
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

mlir::Value callValue(mlir::OpBuilder & builder, mlir::Location where, mlir::Type result,
                      const llvm::Twine & callee, mlir::ValueRange operands) {
    return callWithConstValueOperands(builder, where, mlir::TypeRange{result},
                                      builder.getStringAttr(callee), operands)
        .getResult(0);
}

mlir::Value memberCall(mlir::OpBuilder & builder, mlir::Location where, mlir::Type result,
                       mlir::Value receiver, const llvm::Twine & name, mlir::ValueRange operands) {
    return ec::MemberCallOpaqueOp::create(builder, where, mlir::TypeRange{result}, receiver,
                                          builder.getStringAttr(name), mlir::ArrayAttr{},
                                          mlir::ArrayAttr{}, operands)
        .getResult(0);
}

} // namespace ctcompile::ctnative::lowering_detail
