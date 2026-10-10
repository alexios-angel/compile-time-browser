//===- Records.cpp - Data records returned by value -------------*- C++ -*-===//
//
// A data record (TypeInference::markRecordReturns) is an object literal its
// function returns and its caller only reads and writes through constant
// keys. The function returns its shape's class by value, and the caller keeps
// the result in a local of that class:
//
//   ctn_x_y make() { ctn_x_y v1; v1.x = 1.0; v1.y = 2.0; return v1; }
//   ctn_x_y v2; v2 = make(); ... v2.x ...
//
// The shape census already put the call's result in the literal's group, so
// both ends spell the same class and the field accesses lower unchanged.
//
//===----------------------------------------------------------------------===//
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

void lowering::passRecords(ctjs::FuncOp fn) {
    llvm::SmallVector<ctjs::CallDirectOp> calls;
    fn.getBody().walk([&](ctjs::CallDirectOp call) {
        if (TypeInference::isRecordCall(call.getResult())) { calls.push_back(call); }
    });
    for (ctjs::CallDirectOp call : calls) {
        mlir::Value result = call.getResult();
        const auto record = ec::OpaqueType::get(context, spelling(shapeAt(result)));
        result.setType(record);
        // The local owns the copy from here on; every field access reads it.
        mlir::OpBuilder at(call);
        at.setInsertionPointAfter(call);
        auto local = ec::VariableOp::create(at, call.getLoc(), ec::LValueType::get(record),
                                            ec::OpaqueAttr::get(context, ""));
        result.replaceAllUsesWith(local.getResult());
        ec::AssignOp::create(at, call.getLoc(), local, result);
    }
}

mlir::Type lowering::recordReturnType(ctjs::FuncOp fn) const {
    if (!fn->hasAttr(kRecordReturn)) { return {}; }
    mlir::Type record;
    fn.getBody().walk([&](ctjs::ReturnOp ret) {
        if (auto local = llvm::dyn_cast<ec::LValueType>(ret.getValue().getType())) {
            record = local.getValueType();
        }
    });
    return record;
}

} // namespace ctcompile::ctnative::lowering_detail
