#include "ValueFlow.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/DenseSet.h"
#include <optional>

namespace ctcompile::ctnative::object_detail {
namespace {
// The loop lifter yields a continuation flag alongside register slots. A
// poison exit slot cannot reach the loop's next iteration when that same arm
// yields false. Resolve only integer constants and truncation through this
// particular branch; no source predicate or arbitrary expression is assumed.
std::optional<bool> conditionOnArm(mlir::Value value, mlir::scf::IfOp branch,
                                   mlir::scf::YieldOp arm, unsigned depth = 0) {
    if (depth > 8) { return std::nullopt; }
    if (auto result = llvm::dyn_cast<mlir::OpResult>(value);
        result && result.getOwner() == branch) {
        return conditionOnArm(arm.getOperand(result.getResultNumber()), branch, arm, depth + 1);
    }
    if (auto constant = value.getDefiningOp<mlir::arith::ConstantOp>()) {
        if (auto integer = llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue());
            integer && (integer.getValue().isZero() || integer.getValue().isOne())) {
            return integer.getValue().isOne();
        }
    }
    if (auto truncation = value.getDefiningOp<mlir::arith::TruncIOp>()) {
        return conditionOnArm(truncation.getIn(), branch, arm, depth + 1);
    }
    return std::nullopt;
}
} // namespace

bool inertPoison(mlir::Value value) {
    auto * producer = value.getDefiningOp();
    if (!producer || producer->getName().getStringRef() != "ub.poison") { return false; }
    llvm::SmallVector<mlir::Value> pending{value};
    llvm::DenseSet<mlir::Value> visited;
    unsigned steps = 0;
    while (!pending.empty()) {
        value = pending.pop_back_val();
        if (!visited.insert(value).second) { continue; }
        if (++steps > 4096) { return false; }
        for (mlir::OpOperand & use : value.getUses()) {
            if (++steps > 4096) { return false; }
            auto * owner = use.getOwner();
            const unsigned index = use.getOperandNumber();
            if (llvm::isa<ctjs::RootOp>(owner)) { continue; }
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(owner)) {
                pending.push_back(loop.getBeforeArguments()[index]);
            } else if (auto loop = llvm::dyn_cast<mlir::scf::ForOp>(owner)) {
                if (index < 3) { return false; }
                pending.push_back(loop.getRegionIterArgs()[index - 3]);
                pending.push_back(loop.getResult(index - 3));
            } else if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(owner)) {
                if (index == 0) { return false; }
                auto loop = llvm::cast<mlir::scf::WhileOp>(condition->getParentOp());
                pending.push_back(loop.getAfterArguments()[index - 1]);
                pending.push_back(loop.getResult(index - 1));
            } else if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(owner)) {
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp())) {
                    auto result = branch.getResult(index);
                    bool exitOnly = true;
                    for (mlir::OpOperand & resultUse : result.getUses()) {
                        if (++steps > 4096) { return false; }
                        if (llvm::isa<ctjs::RootOp>(resultUse.getOwner())) { continue; }
                        auto condition =
                            llvm::dyn_cast<mlir::scf::ConditionOp>(resultUse.getOwner());
                        auto flag = condition
                                        ? conditionOnArm(condition.getCondition(), branch, yield)
                                        : std::nullopt;
                        if (!flag || *flag || resultUse.getOperandNumber() == 0) {
                            exitOnly = false;
                            break;
                        }
                        auto loop = llvm::cast<mlir::scf::WhileOp>(condition->getParentOp());
                        pending.push_back(loop.getResult(resultUse.getOperandNumber() - 1));
                    }
                    if (!exitOnly) { pending.push_back(result); }
                } else if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(yield->getParentOp())) {
                    pending.push_back(loop.getBeforeArguments()[index]);
                } else if (auto loop = llvm::dyn_cast<mlir::scf::ForOp>(yield->getParentOp())) {
                    pending.push_back(loop.getRegionIterArgs()[index]);
                    pending.push_back(loop.getResult(index));
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }
    }
    return true;
}
} // namespace ctcompile::ctnative::object_detail
