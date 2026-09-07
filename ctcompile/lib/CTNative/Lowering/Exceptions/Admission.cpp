#include "../Admission/Admission.h"

#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseSet.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {

// This is a live effect proof, separate from the ordinary result-carrier
// check. A closed call returning a number can still throw, access a property,
// mutate a global or reenter through another call. Inspect the actual private
// bodies and every transitive call, keeping no proof across IR mutations.
// The finite operation/depth bounds refuse large or recursive components.
struct nonthrowingPrimitives {
    admission & admitted;
    unsigned remaining = 4096;
    llvm::DenseSet<mlir::Operation *> active;
    llvm::DenseSet<mlir::Operation *> proved;

    explicit nonthrowingPrimitives(admission & admitted) : admitted(admitted) {}

    bool primitive(mlir::Value value) const {
        const auto type = admitted.typeOf(value);
        return llvm::isa_and_nonnull<NumType, BoolType, StrType>(type) ||
               (llvm::isa_and_nonnull<OptType>(type) &&
                llvm::isa<BottomType>(llvm::cast<OptType>(type).getElementType()));
    }

    bool call(ctjs::CallDirectOp direct) {
        auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
            direct, direct.getCalleeAttr());
        if (!target || target.getBody().empty() || !target.getBody().hasOneBlock() ||
            target.getUpvalueCount() != 0 ||
            mlir::SymbolTable::getSymbolVisibility(target) !=
                mlir::SymbolTable::Visibility::Private ||
            direct.getNumOperands() < 3 ||
            direct.getNumOperands() != target.getBody().front().getNumArguments() ||
            !llvm::all_of(direct.getArgOperands().drop_front(3),
                          [&](mlir::Value value) { return primitive(value); })) {
            return false;
        }
        if (proved.contains(target)) { return true; }
        if (active.size() >= 32 || !active.insert(target).second) { return false; }
        const auto result = target.getBody().walk([&](mlir::Operation * op) {
            return operation(op) ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
        });
        active.erase(target);
        if (result.wasInterrupted()) { return false; }
        proved.insert(target);
        return true;
    }

    bool operation(mlir::Operation * op) {
        if (!active.empty()) {
            if (remaining == 0) { return false; }
            --remaining;
        }
        if (llvm::isa<ctjs::TryExitOp, ctjs::TryYieldOp, ctjs::FrameEnterOp, ctjs::FrameExitOp,
                      ctjs::RootOp, ctjs::FromBoolOp, mlir::scf::IfOp, mlir::scf::YieldOp,
                      mlir::arith::ConstantOp, mlir::arith::CmpIOp, mlir::arith::TruncIOp,
                      mlir::arith::ExtUIOp, mlir::arith::IndexCastUIOp>(op) ||
            op->getName().getStringRef() == "ub.poison") {
            return true;
        }
        if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(op)) {
            return primitive(constant.getResult());
        }
        if (llvm::isa<ctjs::BinaryOp, ctjs::UnaryOp, ctjs::CompareOp, ctjs::TruthyOp>(op)) {
            return llvm::all_of(op->getOperands(),
                                [&](mlir::Value value) { return primitive(value); });
        }
        if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(op)) {
            return primitive(returned.getValue());
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            return admission::feedsOnlyDirectCallees(load.getResult());
        }
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(op)) { return call(direct); }
        return false;
    }
};

} // namespace

bool admission::exceptionRegion(ctjs::TryOp attempt) {
    // Recovery preserves explicit throws. Every other protected operation must
    // be proved unable to throw a JavaScript value before its old status edge
    // can disappear. A successful normal-result type alone is not that proof.
    auto & caught = attempt.getCatchBody().front();
    if (!llvm::isa_and_nonnull<NumType, BoolType, StrType>(typeOf(caught.getArgument(0)))) {
        return refuse("native try/catch requires a homogeneous number, boolean or string payload");
    }
    for (auto argument : caught.getArguments().drop_front()) {
        if (!llvm::isa_and_nonnull<NumType, BoolType, StrType>(typeOf(argument))) {
            return refuse("native try/catch requires number, boolean or owning string catch state");
        }
    }
    nonthrowingPrimitives effects{*this};
    bool safe = true;
    attempt->walk([&](mlir::Operation * op) {
        if (!safe || op == attempt.getOperation()) { return; }
        safe = effects.operation(op);
        if (!safe) {
            refuse(("native try/catch cannot prove a nonthrowing operation: `" +
                    op->getName().getStringRef() + "`")
                       .str());
        }
    });
    return safe;
}

} // namespace ctcompile::ctnative::lowering_detail
