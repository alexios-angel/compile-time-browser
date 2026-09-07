#include "RegisterFlow.h"

#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/DenseSet.h"

namespace ctcompile::ctjs::globals_detail {

std::optional<llvm::SmallVector<mlir::OpOperand *>> registerFlowUses(mlir::Value value,
                                                                     unsigned maxSteps) {
    llvm::SmallVector<mlir::Value> pending{value};
    llvm::DenseSet<mlir::Value> seen;
    llvm::SmallVector<mlir::OpOperand *> uses;
    while (!pending.empty()) {
        if (maxSteps == 0) { return std::nullopt; }
        --maxSteps;
        value = pending.pop_back_val();
        if (!seen.insert(value).second) { continue; }
        for (mlir::OpOperand & use : value.getUses()) {
            if (maxSteps == 0) { return std::nullopt; }
            --maxSteps;
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

bool registerFlowHasOrigin(mlir::Value value, mlir::Value source, unsigned maxSteps) {
    llvm::SmallVector<mlir::Value> pending{value};
    llvm::DenseSet<mlir::Value> seen;
    bool foundSource = false;
    while (!pending.empty()) {
        if (maxSteps == 0) { return false; }
        --maxSteps;
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
            if (maxSteps == 0) { return false; }
            --maxSteps;
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
