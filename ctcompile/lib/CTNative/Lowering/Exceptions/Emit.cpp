//===- Emit.cpp - try/catch emission ----------------------------*- C++ -*-===//
//
// Lowers an admitted ctjs.try and its exits to the ctnative C++ try/catch
// operations (CppTryOp, CppThrowOp, CppTryEndOp).
//
//===----------------------------------------------------------------------===//

#include "../EmitC/Emitter.h"
#include "ctcompile/CTNative/IR/CTNativeOps.h"

namespace ctcompile::ctnative::lowering_detail {

/// Gives each ctjs.try a result slot and one slot per extra catch argument,
/// declared before the try, so the catch block reads them instead of block
/// arguments.
void lowering::prepareExceptions(ctjs::FuncOp fn) {
    fn.walk([&](ctjs::TryOp attempt) {
        mlir::OpBuilder before(attempt);
        auto & storage = exceptionSlots[attempt];
        const auto variable = [&](mlir::Type type) -> mlir::Value {
            return ec::VariableOp::create(before, attempt.getLoc(), ec::LValueType::get(type),
                                          ec::OpaqueAttr::get(context, "{}"));
        };
        storage.result = variable(attempt.getResult().getType());
        auto & caught = attempt.getCatchBody().front();
        for (auto argument : caught.getArguments().drop_front()) {
            storage.state.push_back(variable(argument.getType()));
        }
        // State lives outside the protected region. Each throw writes every
        // slot before unwinding, and the catch copies each slot into SSA.
        mlir::OpBuilder entry = mlir::OpBuilder::atBlockBegin(&caught);
        for (unsigned index = 1; index < caught.getNumArguments(); ++index) {
            auto argument = caught.getArgument(index);
            auto copied = ec::LoadOp::create(entry, argument.getLoc(), argument.getType(),
                                             storage.state[index - 1]);
            argument.replaceAllUsesWith(copied);
        }
        while (caught.getNumArguments() > 1) { caught.eraseArgument(caught.getNumArguments() - 1); }
    });
}

/// Rewrites a ctjs.try, try_exit or try_yield against the slots
/// prepareExceptions() made. A throwing exit stores the catch state and
/// throws; a normal exit stores the result. Returns false for other ops.
bool lowering::replaceException(mlir::Operation * op) {
    auto attempt = llvm::dyn_cast<ctjs::TryOp>(op);
    auto exit = llvm::dyn_cast<ctjs::TryExitOp>(op);
    auto yielded = llvm::dyn_cast<ctjs::TryYieldOp>(op);
    if (!attempt && !exit && !yielded) { return false; }
    mlir::OpBuilder at(op);
    auto where = op->getLoc();
    if (attempt) {
        auto made = CppTryOp::create(at, where);
        made.getBody().takeBody(attempt.getBody());
        made.getCatchBody().takeBody(attempt.getCatchBody());
        auto loaded =
            ec::LoadOp::create(at, where, attempt.getResult().getType(), exceptionSlots[op].result);
        attempt.getResult().replaceAllUsesWith(loaded);
        exceptionSlots.erase(op);
        op->erase();
        return true;
    }
    auto parent = op->getParentOfType<ctjs::TryOp>();
    auto & storage = exceptionSlots[parent];
    if (exit) {
        auto branch = ec::IfOp::create(at, where, exit.getIsThrow(), true);
        mlir::OpBuilder throwing = mlir::OpBuilder::atBlockBegin(&branch.getThenRegion().front());
        for (auto [slot, value] : llvm::zip(storage.state, exit.getCaughtValues().drop_front())) {
            ec::AssignOp::create(throwing, where, slot, value);
        }
        CppThrowOp::create(throwing, where, exit.getCaughtValues().front());
        ec::YieldOp::create(throwing, where);
        mlir::OpBuilder normal = mlir::OpBuilder::atBlockBegin(&branch.getElseRegion().front());
        ec::AssignOp::create(normal, where, storage.result, exit.getNormalResult());
        ec::YieldOp::create(normal, where);
    } else {
        ec::AssignOp::create(at, where, storage.result, yielded.getValue());
    }
    CppTryEndOp::create(at, where);
    op->erase();
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
