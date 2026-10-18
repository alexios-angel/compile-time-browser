//===- ClosureLifting/SelfCalls.cpp - A lifted function calling itself ----===//
//
// A named function expression calls itself through its own closure,
// `(function walk(el) { ... walk(kid); })(root)`: inside the body `walk` is
// `%arg2`, the callee the interpreter passed in. Once the function is lifted
// that closure is gone, so each such call becomes a ctjs.call_direct of the
// function itself, passing its own capture parameters on.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

/// Rewrites every `ctjs.call %arg2(...)` in `target` into a direct call of
/// `target`. Runs inside lift(), after the capture parameters are inserted and
/// the slot lists set: a lifted call passes the captures first, and the
/// function's own capture parameters already hold them (a carried slot holds
/// the owning frame's address, which is passed on as it is). whyNotLiftable
/// admitted only calls with at most `parameters` arguments.
void closureLifter::liftSelfCalls(ctjs::FuncOp target, unsigned captures, unsigned parameters) {
    mlir::Block & entry = target.getBody().front();
    llvm::SmallVector<ctjs::CallOp> calls;
    for (mlir::OpOperand & use : entry.getArgument(ctjs::arg_callee).getUses()) {
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        if (call && use.getOperandNumber() == 0) { calls.push_back(call); }
    }
    llvm::SmallVector<mlir::Value> forwarded;
    for (unsigned k = 0; k < captures; ++k) {
        forwarded.push_back(entry.getArgument(captureArgument(k)));
    }
    for (ctjs::CallOp call : calls) {
        mlir::OpBuilder at(call);
        const mlir::Value undefined = undefinedAt(at, call.getLoc());
        // The callee operand is undefined, as for a lifted method: the closure
        // value no longer exists, and the native call drops the operand.
        auto direct = directCall(at, call.getLoc(), target, call.getReceiver(), undefined,
                                 undefined, forwarded, call.getArgs(), captures + parameters);
        for (llvm::StringRef slots : {kNativeCellArgs, kNativeObjectArgs}) {
            if (mlir::Attribute listed = target->getAttr(slots)) { direct->setAttr(slots, listed); }
        }
        call.getResult().replaceAllUsesWith(direct.getResult());
        call.erase();
    }
}

} // namespace ctcompile::ctnative::lowering_detail
