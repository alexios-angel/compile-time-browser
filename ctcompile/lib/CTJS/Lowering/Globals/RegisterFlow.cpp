#include "RegisterFlow.h"

#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/DenseSet.h"

namespace ctcompile::ctjs::globals_detail {
namespace {
struct work {
    unsigned remaining;
    unsigned * steps;

    work(unsigned maxSteps, unsigned * steps) : remaining(maxSteps), steps(steps) {
        if (steps) { *steps = 0; }
    }

    bool spend() {
        if (remaining == 0) { return false; }
        --remaining;
        if (steps) { ++*steps; }
        return true;
    }
};
} // namespace

std::optional<llvm::SmallVector<mlir::OpOperand *>> registerFlowUses(mlir::Value value,
                                                                     unsigned maxSteps,
                                                                     unsigned * steps) {
    work budget(maxSteps, steps);
    llvm::SmallVector<mlir::Value> pending{value};
    llvm::DenseSet<mlir::Value> seen;
    llvm::SmallVector<mlir::OpOperand *> uses;
    while (!pending.empty()) {
        if (!budget.spend()) { return std::nullopt; }
        value = pending.pop_back_val();
        if (!seen.insert(value).second) { continue; }
        for (mlir::OpOperand & use : value.getUses()) {
            if (!budget.spend()) { return std::nullopt; }
            if (auto branch = llvm::dyn_cast<mlir::BranchOpInterface>(use.getOwner())) {
                if (auto argument = branch.getSuccessorBlockArgument(use.getOperandNumber())) {
                    pending.push_back(*argument);
                    continue;
                }
            }
            uses.push_back(&use);
        }
    }
    return uses;
}

bool registerFlowHasOrigin(mlir::Value value, mlir::Value source, unsigned maxSteps,
                           unsigned * steps) {
    work budget(maxSteps, steps);
    llvm::SmallVector<mlir::Value> pending{value};
    llvm::DenseSet<mlir::Value> seen;
    bool foundSource = false;
    while (!pending.empty()) {
        if (!budget.spend()) { return false; }
        value = pending.pop_back_val();
        if (value == source) {
            foundSource = true;
            continue;
        }
        if (!seen.insert(value).second) { continue; }
        auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
        if (!argument || argument.getOwner()->isEntryBlock()) { return false; }
        mlir::Block & block = *argument.getOwner();
        if (block.hasNoPredecessors()) { return false; }
        for (auto pred = block.pred_begin(), end = block.pred_end(); pred != end; ++pred) {
            if (!budget.spend()) { return false; }
            auto branch = llvm::dyn_cast<mlir::BranchOpInterface>((*pred)->getTerminator());
            if (!branch) { return false; }
            auto operands = branch.getSuccessorOperands(pred.getSuccessorIndex());
            unsigned index = argument.getArgNumber();
            if (index >= operands.size() || operands.isOperandProduced(index)) { return false; }
            pending.push_back(operands[index]);
        }
    }
    return foundSource;
}

} // namespace ctcompile::ctjs::globals_detail
