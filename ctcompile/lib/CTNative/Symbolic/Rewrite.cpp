#include "Facts.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative::symbolic {
namespace {
mlir::Value constant(mlir::Operation * op, mlir::Attribute literal) {
    mlir::OpBuilder at(op);
    const auto type = op->getResult(0).getType();
    if (auto integer = llvm::dyn_cast<mlir::IntegerType>(type)) {
        mlir::IntegerAttr value;
        if (auto original = llvm::dyn_cast<mlir::IntegerAttr>(literal)) {
            value = mlir::IntegerAttr::get(integer,
                                           original.getValue().zextOrTrunc(integer.getWidth()));
        } else if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(literal)) {
            value = mlir::IntegerAttr::get(integer, boolean.getValue());
        }
        return value ? mlir::arith::ConstantOp::create(at, op->getLoc(), value).getResult()
                     : mlir::Value{};
    }
    if (!llvm::isa<ctjs::ValueType>(type) || llvm::isa<mlir::IntegerAttr>(literal)) { return {}; }
    mlir::OperationState state(op->getLoc(), "ctjs.constant");
    state.addTypes(type);
    state.addAttribute("value", literal);
    return at.create(state)->getResult(0);
}

bool select(mlir::scf::IfOp branch, bool take) {
    auto & selected = take ? branch.getThenRegion() : branch.getElseRegion();
    if (selected.empty()) {
        if (branch.getNumResults() != 0) { return false; }
        branch.erase();
        return true;
    }
    if (!llvm::hasSingleElement(selected) || selected.front().getNumArguments() != 0) {
        return false;
    }
    auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(selected.front().getTerminator());
    if (!yield || yield.getNumOperands() != branch.getNumResults()) { return false; }
    for (auto [result, value] : llvm::zip(branch.getResults(), yield.getOperands())) {
        result.replaceAllUsesWith(value);
    }
    for (mlir::Operation & op : llvm::make_early_inc_range(selected.front().without_terminator())) {
        op.moveBefore(branch);
    }
    branch.erase();
    return true;
}
} // namespace

Changes rewrite(mlir::ModuleOp module, const Analysis & analysis, Budget & budget) {
    llvm::SmallVector<mlir::Operation *> candidates;
    module.walk<mlir::WalkOrder::PostOrder>([&](mlir::Operation * op) {
        if (llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp, ctjs::CompareOp,
                      ctjs::TruthyOp, mlir::arith::TruncIOp, mlir::scf::IfOp>(op)) {
            candidates.push_back(op);
        }
    });
    Changes changes;
    for (mlir::Operation * op : candidates) {
        if (!budget.take()) { break; }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
            if (auto take = condition(analysis.get(branch.getCondition()));
                take && select(branch, *take)) {
                ++changes.branches;
            }
            continue;
        }
        const Fact result = analysis.get(op->getResult(0));
        if (!result.literal) { continue; }
        if (auto replacement = constant(op, result.literal)) {
            op->getResult(0).replaceAllUsesWith(replacement);
            op->erase();
            ++changes.expressions;
        }
        // Producers are deliberately retained, including effectful calls and
        // object coercions whose boolean/string result made a consumer fold.
    }
    return changes;
}
} // namespace ctcompile::ctnative::symbolic
