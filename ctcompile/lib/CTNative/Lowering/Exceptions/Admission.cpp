#include "../Admission/Admission.h"

namespace ctcompile::ctnative::lowering_detail {

bool admission::exceptionRegion(ctjs::TryOp attempt) {
    // Recovery preserves explicit throws. Every other protected operation must
    // be proved unable to throw a JavaScript value before its old status edge
    // can disappear. A successful normal-result type alone is not that proof.
    const auto primitive = [&](mlir::Value value) {
        const auto type = typeOf(value);
        return llvm::isa_and_nonnull<NumType, BoolType>(type) ||
               (llvm::isa_and_nonnull<OptType>(type) &&
                llvm::isa<BottomType>(llvm::cast<OptType>(type).getElementType()));
    };
    auto & caught = attempt.getCatchBody().front();
    if (!llvm::isa_and_nonnull<NumType>(typeOf(caught.getArgument(0)))) {
        return refuse("native try/catch requires a proved numeric thrown value");
    }
    for (auto argument : caught.getArguments().drop_front()) {
        if (!llvm::isa_and_nonnull<NumType, BoolType>(typeOf(argument))) {
            return refuse("native try/catch requires numeric or boolean catch state");
        }
    }
    bool safe = true;
    attempt->walk([&](mlir::Operation * op) {
        if (!safe || op == attempt.getOperation()) { return; }
        if (llvm::isa<ctjs::TryExitOp, ctjs::TryYieldOp, ctjs::FrameEnterOp, ctjs::FrameExitOp,
                      ctjs::RootOp, ctjs::FromBoolOp, mlir::scf::IfOp, mlir::scf::YieldOp,
                      mlir::arith::ConstantOp, mlir::arith::CmpIOp, mlir::arith::TruncIOp,
                      mlir::arith::ExtUIOp, mlir::arith::IndexCastUIOp>(op) ||
            op->getName().getStringRef() == "ub.poison") {
            return;
        }
        if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(op)) {
            safe = primitive(constant.getResult());
        } else if (llvm::isa<ctjs::BinaryOp, ctjs::UnaryOp, ctjs::CompareOp, ctjs::TruthyOp>(op)) {
            safe = llvm::all_of(op->getOperands(), primitive);
        } else {
            safe = false;
        }
        if (!safe) {
            refuse(("native try/catch cannot prove a nonthrowing operation: `" +
                    op->getName().getStringRef() + "`")
                       .str());
        }
    });
    return safe;
}

} // namespace ctcompile::ctnative::lowering_detail
