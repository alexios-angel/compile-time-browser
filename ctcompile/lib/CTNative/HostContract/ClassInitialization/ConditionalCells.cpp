//===- ConditionalCells.cpp - Branch-stored cell scalarization --*- C++ -*-===//
//
// Part of the class-initialization proof: removes entry-function cells that
// start undefined and are assigned inside exactly one scf.if arm.
//
//===----------------------------------------------------------------------===//

#include "Proof.hpp"

namespace ctcompile::ctnative::class_detail {

/// Removes each entry-block cell that starts undefined and is written exactly
/// once, inside an scf.if arm: every read after that store is replaced with the
/// stored value. Refuses the class proof if such a cell has several stores,
/// escapes, or has a read the store does not dominate.
bool classInitialization::scalarizeConditionalCells(ctjs::FuncOp entry) {
    auto & block = entry.getBody().front();
    llvm::SmallVector<ctjs::CreateCellOp> candidates;
    for (auto & operation : block) {
        if (!step()) { return false; }
        if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(operation);
            cell && undefined(cell.getInitial()) && !cells.count(cell.getResult()) &&
            !booleanCells.contains(cell.getResult())) {
            candidates.push_back(cell);
        }
    }
    for (auto cell : candidates) {
        if (!step()) { return false; }
        ctjs::CellSetOp assignment;
        bool conditional = false, multiple = false;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!step()) { return false; }
            auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
            if (!write || use.getOperandNumber() != 0) { continue; }
            conditional |= write->getBlock() != &block;
            if (assignment) { multiple = true; }
            assignment = write;
        }
        if (!conditional) { continue; }
        auto * position = positionIn<mlir::scf::IfOp>(assignment, &block);
        if (multiple || position->getBlock() != &block || !cell->isBeforeInBlock(position)) {
            return refuse("class conditional cell requires one ordered branch store");
        }
        llvm::SmallVector<ctjs::CellGetOp> reads;
        llvm::SmallVector<ctjs::RootOp> roots;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!step()) { return false; }
            auto * user = use.getOwner();
            if (user == assignment && use.getOperandNumber() == 0) { continue; }
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(user);
                root && use.getOperandNumber() == 1) {
                auto * rootPosition = positionIn<mlir::scf::IfOp>(root, &block);
                if (rootPosition->getBlock() != &block || !cell->isBeforeInBlock(rootPosition)) {
                    return refuse("class conditional cell has a nonlocal root");
                }
                roots.push_back(root);
                continue;
            }
            auto read = llvm::dyn_cast<ctjs::CellGetOp>(user);
            if (!read || use.getOperandNumber() != 0) {
                return refuse("class conditional cell escapes its exact reads");
            }
            auto * readPosition = positionIn<mlir::scf::IfOp>(read, assignment->getBlock());
            if (readPosition->getBlock() != assignment->getBlock() ||
                !assignment->isBeforeInBlock(readPosition)) {
                return refuse("class conditional cell read is not dominated by its store");
            }
            for (mlir::OpOperand & selected : read.getResult().getUses()) {
                (void)selected;
                if (!step()) { return false; }
            }
            reads.push_back(read);
        }
        if (!spend(2 + reads.size() + roots.size())) { return false; }
        // Every read sits after the store in the same arm, so it sees exactly the
        // stored value. The value's computation, the surrounding effects and the
        // branch stay in place; the native body proof that runs later checks them.
        for (auto read : reads) {
            read.getResult().replaceAllUsesWith(assignment.getValue());
            read.erase();
        }
        // Roots keep their positions; the body proof still validates their frame
        // and context.
        for (auto root : roots) { root.getValueMutable().assign(cell.getInitial()); }
        assignment.erase();
        cell.erase();
    }
    return true;
}

} // namespace ctcompile::ctnative::class_detail
