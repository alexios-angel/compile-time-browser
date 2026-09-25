#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::ctnative {

// Check the materialized copy's own bound before aliasing its members to an
// original, potentially longer NodeList. Complete DOM reproof checks progression.
template <typename Spend>
bool hasOwnSnapshotBound(ctjs::GetPropertyOp read, mlir::Value snapshot, Spend spend) {
    auto index = llvm::dyn_cast<mlir::BlockArgument>(read.getKey());
    if (!index) { return false; }
    for (auto * parent = read->getParentOp(); parent; parent = parent->getParentOp()) {
        if (!spend()) { return false; }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(parent);
            branch && branch.getThenRegion().isAncestor(read->getParentRegion())) {
            auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
            auto compare =
                truth ? truth.getValue().getDefiningOp<ctjs::CompareOp>() : ctjs::CompareOp{};
            auto bound = compare ? compare.getRhs().getDefiningOp<ctjs::GetPropertyOp>()
                                 : ctjs::GetPropertyOp{};
            if (compare && compare.getKind() == ctjs::CompareKind::Lt &&
                compare.getLhs() == index && bound && bound.getObject() == snapshot &&
                ctjs::constantKey(bound.getKey()) == "length") {
                return true;
            }
        }
        auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(parent);
        if (!loop || !loop.getAfter().hasOneBlock() || !loop.getBefore().hasOneBlock() ||
            index.getOwner() != &loop.getAfter().front()) {
            continue;
        }
        auto condition =
            llvm::dyn_cast<mlir::scf::ConditionOp>(loop.getBefore().front().getTerminator());
        auto truth =
            condition ? condition.getCondition().getDefiningOp<ctjs::TruthyOp>() : ctjs::TruthyOp{};
        auto compare =
            truth ? truth.getValue().getDefiningOp<ctjs::CompareOp>() : ctjs::CompareOp{};
        auto bound =
            compare ? compare.getRhs().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
        if (compare && compare.getKind() == ctjs::CompareKind::Lt && bound &&
            condition.getArgs().size() > index.getArgNumber() &&
            compare.getLhs() == condition.getArgs()[index.getArgNumber()] &&
            bound.getObject() == snapshot && ctjs::constantKey(bound.getKey()) == "length") {
            return true;
        }
    }
    return false;
}

} // namespace ctcompile::ctnative
