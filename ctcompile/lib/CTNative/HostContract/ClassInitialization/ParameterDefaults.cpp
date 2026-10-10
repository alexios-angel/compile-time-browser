//===- ParameterDefaults.cpp - Parameter default selection ------*- C++ -*-===//
//
// Part of the class-initialization proof: turns the lowered form of a default
// parameter (`param = param === undefined ? default : param`) into a value
// selection so no cell is left behind.
//
//===----------------------------------------------------------------------===//

#include "Proof.hpp"

namespace ctcompile::ctnative::class_detail {

/// Rewrites each default-parameter pattern in the entry block (a cell
/// initialized from a formal, then a single store inside an scf.if guarded by
/// a strict-equality test against undefined) into an scf.if that yields either
/// the default or the original formal, and erases the cell. Refuses any other
/// shape. Returns false on refusal or budget exhaustion.
bool classInitialization::selectParameterDefaults(ctjs::FuncOp entry) {
    auto & block = entry.getBody().front();
    llvm::SmallVector<ctjs::CreateCellOp> candidates;
    for (auto & operation : block) {
        if (!step()) { return false; }
        if (auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(operation)) {
            candidates.push_back(cell);
        }
    }
    for (auto cell : candidates) {
        if (!step()) { return false; }
        auto formal = llvm::dyn_cast<mlir::BlockArgument>(cell.getInitial());
        if (!formal || formal.getOwner() != &block ||
            formal.getArgNumber() < ctjs::implicit_arguments || cells.count(cell.getResult())) {
            continue;
        }
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
        // Charge for the fixed set of operations the shape check below inspects.
        if (!spend(12)) { return false; }
        auto branch = llvm::dyn_cast<mlir::scf::IfOp>(assignment->getParentOp());
        if (multiple || !branch || branch->getBlock() != &block || !cell->isBeforeInBlock(branch) ||
            branch.getNumResults() != 0 || !llvm::hasSingleElement(branch.getThenRegion()) ||
            !llvm::hasSingleElement(branch.getElseRegion()) ||
            assignment->getBlock() != &branch.getThenRegion().front() ||
            !llvm::hasSingleElement(branch.getElseRegion().front())) {
            return refuse("class parameter default requires one ordered conditional store");
        }
        auto thenYield =
            llvm::dyn_cast<mlir::scf::YieldOp>(branch.getThenRegion().front().getTerminator());
        auto elseYield =
            llvm::dyn_cast<mlir::scf::YieldOp>(branch.getElseRegion().front().getTerminator());
        auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
        auto compare =
            truth ? truth.getValue().getDefiningOp<ctjs::CompareOp>() : ctjs::CompareOp{};
        auto current =
            compare ? compare.getLhs().getDefiningOp<ctjs::CellGetOp>() : ctjs::CellGetOp{};
        if (!thenYield || !elseYield || thenYield.getNumOperands() || elseYield.getNumOperands() ||
            !compare || compare.getKind() != ctjs::CompareKind::StrictEq || !current ||
            current.getCell() != cell.getResult() || !undefined(compare.getRhs()) ||
            current->getBlock() != &block || !current->isBeforeInBlock(branch) ||
            compare->getBlock() != &block || truth->getBlock() != &block) {
            return refuse("class parameter default lacks its original undefined test");
        }

        llvm::SmallVector<std::pair<ctjs::CellGetOp, bool>> reads;
        llvm::SmallVector<ctjs::RootOp> roots;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            if (!step()) { return false; }
            auto * user = use.getOwner();
            if (user == assignment && use.getOperandNumber() == 0) { continue; }
            auto * position =
                positionIn<mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::IndexSwitchOp>(user,
                                                                                          &block);
            if (position->getBlock() != &block || !cell->isBeforeInBlock(position)) {
                return refuse("class parameter default cell has a nonlocal use");
            }
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(user);
                root && use.getOperandNumber() == 1) {
                roots.push_back(root);
                continue;
            }
            auto read = llvm::dyn_cast<ctjs::CellGetOp>(user);
            if (!read || use.getOperandNumber() != 0 || position == branch) {
                return refuse("class parameter default cell escapes its ordered reads");
            }
            // Charge one step per use the rewrite below will redirect.
            for (mlir::OpOperand & selected : read.getResult().getUses()) {
                (void)selected;
                if (!step()) { return false; }
            }
            reads.emplace_back(read, branch->isBeforeInBlock(position));
        }
        if (!spend(7 + reads.size() + roots.size())) { return false; }
        // A supplied argument and its default stay distinct SSA values. The
        // original arms move into the new scf.if intact, so the default's
        // computation and effects still run only when the argument is undefined,
        // and the DOM proof still checks them.
        mlir::OpBuilder at(branch);
        auto selected = mlir::scf::IfOp::create(
            at, branch.getLoc(), mlir::TypeRange{formal.getType()}, branch.getCondition());
        selected->setAttrs(branch->getAttrs());
        selected.getThenRegion().takeBody(branch.getThenRegion());
        selected.getElseRegion().takeBody(branch.getElseRegion());
        thenYield->setOperands(mlir::ValueRange{assignment.getValue()});
        elseYield->setOperands(mlir::ValueRange{formal});
        for (auto [read, after] : reads) {
            // Reads before the branch, including the undefined test itself, keep
            // the original formal; they cannot use the selection their result feeds.
            read.getResult().replaceAllUsesWith(after ? selected.getResult(0)
                                                      : mlir::Value(formal));
            read.erase();
        }
        // Roots keep their positions and contexts for the frame proof.
        for (auto root : roots) { root.getValueMutable().assign(formal); }
        assignment.erase();
        branch.erase();
        cell.erase();
    }
    return true;
}

} // namespace ctcompile::ctnative::class_detail
