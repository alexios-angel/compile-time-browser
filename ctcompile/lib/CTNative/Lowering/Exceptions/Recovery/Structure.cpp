// recovery - structuring the cloned regions with LLVM's CFG-to-SCF lifting, and
// the two normalisations its output needs before the native emitter sees it.
//
// One of six files carved out of a 1,251-line Exceptions/Recovery.cpp on
// 2026-09-08. All are member functions of `recovery`, the disposable-clone
// transaction declared in Attempt.h beside this; Exceptions/Recovery.h, the
// header the rest of the lowering sees, did not change.

#include "Attempt.h"

namespace ctcompile::ctnative::lowering_detail {

bool recovery::structure(mlir::Region & region) {
    // The upstream algorithm has no work callback. Precharge a conservative
    // quadratic bound for this acyclic, finite register CFG before calling
    // it; a failure only damages the disposable function clone.
    const uint64_t blocks = region.getBlocks().size();
    const uint64_t scale = uint64_t(width) + 8;
    // Compare by division before multiplying, including the block square.
    if (blocks + 1 > remaining / scale / (blocks + 1)) {
        return reject("native exception recovery work budget exhausted");
    }
    if (!spend((blocks + 1) * (blocks + 1) * scale)) { return false; }
    mlir::IRRewriter rewriter(function.getContext());
    (void)mlir::simplifyRegions(rewriter, llvm::MutableArrayRef<mlir::Region>(region));
    mlir::DominanceInfo dominance(function);
    mlir::ControlFlowToSCFTransformation transformation;
    std::string diagnostic;
    mlir::FailureOr<bool> transformed = mlir::failure();
    {
        mlir::ScopedDiagnosticHandler capture(function.getContext(), [&](mlir::Diagnostic & note) {
            if (diagnostic.empty()) { diagnostic = note.str(); }
            return mlir::success();
        });
        transformed = mlir::transformCFGToSCF(region, transformation, dominance);
    }
    if (mlir::failed(transformed) || !region.hasOneBlock()) {
        return reject(diagnostic.empty() ? "native exception region did not structure completely"
                                         : diagnostic);
    }
    return true;
}

bool recovery::normalizeIndexSwitches(mlir::Operation * guarded) {
    // LLVM may use an index_switch to dispatch merged completion exits.
    // Expose that dispatch as comparisons and nested ifs, retaining each
    // selected region and its exact yields. No case body is evaluated or
    // cloned, and the default body remains the final else region.
    llvm::SmallVector<mlir::scf::IndexSwitchOp> switches;
    auto walked = guarded->walk([&](mlir::Operation * op) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        if (auto found = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(op)) {
            switches.push_back(found);
        }
        return mlir::WalkResult::advance();
    });
    if (walked.wasInterrupted()) { return false; }
    // The walk is postorder, so moving an enclosing case never leaves a
    // queued switch inside an operation that has already been erased.
    for (mlir::scf::IndexSwitchOp switcher : switches) {
        const unsigned count = switcher.getNumResults();
        if (switcher.getCases().size() != switcher.getCaseRegions().size()) {
            return reject("native exception switch lost its case correspondence");
        }
        for (mlir::Region & region : switcher->getRegions()) {
            if (!spend(uint64_t(1) + count)) { return false; }
            if (!region.hasOneBlock() || region.front().empty() ||
                region.front().getNumArguments() != 0 ||
                !llvm::isa<mlir::scf::YieldOp>(region.front().getTerminator()) ||
                region.front().getTerminator()->getNumOperands() != count) {
                return reject("native exception switch lost its result correspondence");
            }
        }
        for (mlir::Value result : switcher.getResults()) {
            for (mlir::OpOperand & use : result.getUses()) {
                (void)use;
                if (!spend()) { return false; }
            }
        }
        if (switcher.getCases().empty()) {
            if (!spend(uint64_t(2) + count)) { return false; }
            auto & block = switcher.getDefaultRegion().front();
            auto * yield = block.getTerminator();
            llvm::SmallVector<mlir::Value> values(yield->getOperands());
            yield->erase();
            switcher->getBlock()->getOperations().splice(switcher->getIterator(),
                                                         block.getOperations());
            for (auto [result, value] : llvm::zip(switcher.getResults(), values)) {
                result.replaceAllUsesWith(value);
            }
            switcher.erase();
            continue;
        }
        mlir::OpBuilder builder(switcher);
        mlir::scf::IfOp first;
        for (auto [index, value] : llvm::enumerate(switcher.getCases())) {
            if (!spend(uint64_t(6) + count * uint64_t(2))) { return false; }
            auto key = mlir::arith::ConstantIndexOp::create(builder, switcher.getLoc(), value);
            auto condition = mlir::arith::CmpIOp::create(
                builder, switcher.getLoc(), mlir::arith::CmpIPredicate::eq, switcher.getArg(), key);
            auto branch = mlir::scf::IfOp::create(builder, switcher.getLoc(),
                                                  switcher.getResultTypes(), condition);
            branch.getThenRegion().takeBody(switcher.getCaseRegions()[index]);
            if (first) {
                mlir::scf::YieldOp::create(builder, switcher.getLoc(), branch.getResults());
            } else {
                first = branch;
            }
            if (index + 1 == switcher.getCases().size()) {
                branch.getElseRegion().takeBody(switcher.getDefaultRegion());
            } else {
                branch.getElseRegion().emplaceBlock();
                builder.setInsertionPointToEnd(&branch.getElseRegion().front());
            }
        }
        for (auto [result, value] : llvm::zip(switcher.getResults(), first.getResults())) {
            result.replaceAllUsesWith(value);
        }
        switcher.erase();
    }
    return true;
}

bool recovery::isFullPoison(mlir::Value value) {
    auto poison = value.getDefiningOp<mlir::ub::PoisonOp>();
    return poison && (!poison.getValue() || llvm::isa<mlir::ub::PoisonAttr>(poison.getValue()));
}

bool recovery::trimUnusedIfResults(mlir::Operation * guarded) {
    // Catch-state pruning can orphan the corresponding SCF results,
    // including slots containing only poison. Trim result/yield pairs
    // explicitly: branch bodies and their effects must still execute.
    // Preorder exposes an outer result's unused inner producers in the
    // same round. A fixpoint also handles producers in earlier siblings
    // and all-poison slots forwarded through nested conditionals. Only
    // explicit, fully poisoned incoming values authorize that collapse;
    // an inferred Bottom type is never sufficient evidence.
    llvm::SmallVector<mlir::scf::IfOp> branches;
    auto walked = guarded->walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * op) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) { branches.push_back(branch); }
        return mlir::WalkResult::advance();
    });
    if (walked.wasInterrupted()) { return false; }
    bool changed = true;
    while (changed) {
        changed = false;
        for (mlir::scf::IfOp & branch : branches) {
            const unsigned count = branch.getNumResults();
            if (!spend(uint64_t(1) + count)) { return false; }
            if (count == 0) { continue; }
            for (mlir::Region & region : branch->getRegions()) {
                if (!region.hasOneBlock() || region.front().empty() ||
                    !llvm::isa<mlir::scf::YieldOp>(region.front().getTerminator()) ||
                    region.front().getTerminator()->getNumOperands() != count) {
                    return reject("native exception conditional lost its result correspondence");
                }
            }
            llvm::BitVector unused(count);
            llvm::SmallVector<mlir::Type> types;
            for (auto [index, result] : llvm::enumerate(branch.getResults())) {
                if (result.use_empty()) {
                    unused.set(static_cast<unsigned>(index));
                    continue;
                }
                for (mlir::OpOperand & use : result.getUses()) {
                    (void)use;
                    if (!spend()) { return false; }
                }
                if (!spend(2)) { return false; }
                if (isFullPoison(branch.getThenRegion().front().getTerminator()->getOperand(
                        static_cast<unsigned>(index))) &&
                    isFullPoison(branch.getElseRegion().front().getTerminator()->getOperand(
                        static_cast<unsigned>(index)))) {
                    if (!spend()) { return false; }
                    mlir::OpBuilder before(branch);
                    auto poison =
                        mlir::ub::PoisonOp::create(before, branch.getLoc(), result.getType());
                    result.replaceAllUsesWith(poison);
                    unused.set(static_cast<unsigned>(index));
                    continue;
                }
                types.push_back(result.getType());
            }
            if (unused.none()) { continue; }
            if (!spend(uint64_t(3) + count * uint64_t(2))) { return false; }
            mlir::OpBuilder builder(branch);
            mlir::OperationState state(branch.getLoc(), mlir::scf::IfOp::getOperationName());
            state.addOperands(branch.getCondition());
            state.addTypes(types);
            state.addAttributes(branch->getAttrs());
            state.addRegion();
            state.addRegion();
            auto replacement = llvm::cast<mlir::scf::IfOp>(builder.create(state));
            for (auto [source, destination] :
                 llvm::zip(branch->getRegions(), replacement->getRegions())) {
                source.front().getTerminator()->eraseOperands(unused);
                destination.takeBody(source);
            }
            unsigned next = 0;
            for (auto [index, result] : llvm::enumerate(branch.getResults())) {
                if (!unused.test(static_cast<unsigned>(index))) {
                    result.replaceAllUsesWith(replacement.getResult(next++));
                }
            }
            branch.erase();
            branch = replacement;
            changed = true;
        }
    }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
