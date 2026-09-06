#include "Facts.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Rewrite/PatternApplicator.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"

#include <cassert>

namespace ctcompile::ctnative::symbolic {
namespace {
#include "SymbolicPrecompute.h.inc"

// The analysis proves the value; this only adapts its literal to the result
// representation. Operation construction and replacement live in PDLL.
mlir::Attribute literal(mlir::Operation * op, const Analysis & analysis) {
    const auto known = analysis.get(op->getResult(0)).literal;
    if (!known) { return {}; }
    const auto type = op->getResult(0).getType();
    if (auto integer = llvm::dyn_cast<mlir::IntegerType>(type)) {
        if (auto original = llvm::dyn_cast<mlir::IntegerAttr>(known)) {
            return mlir::IntegerAttr::get(integer,
                                          original.getValue().zextOrTrunc(integer.getWidth()));
        }
        if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(known)) {
            return mlir::IntegerAttr::get(integer, boolean.getValue());
        }
        return {};
    }
    if (!llvm::isa<ctjs::ValueType>(type) || llvm::isa<mlir::IntegerAttr>(known)) { return {}; }
    return known;
}

// Moving a selected region and remapping its yield is control-flow surgery,
// which stays native rather than hiding it behind a PDLL rewrite callback.
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
    mlir::RewritePatternSet patterns(module.getContext());
    populateGeneratedPDLLPatterns(patterns);
    // These callbacks belong to this invocation. The frozen patterns and
    // applicator are destroyed before analysis goes out of scope.
    patterns.getPDLPatterns().registerConstraintFunction(
        "HasSymbolicLiteral", [&analysis](mlir::PatternRewriter &, mlir::Operation * op) {
            return mlir::success(static_cast<bool>(literal(op, analysis)));
        });
    patterns.getPDLPatterns().registerRewriteFunction(
        "SymbolicLiteral", [&analysis](mlir::PatternRewriter &, mlir::Operation * op) {
            return literal(op, analysis);
        });
    mlir::FrozenRewritePatternSet frozen(std::move(patterns));
    mlir::PatternApplicator applicator(frozen);
    applicator.applyDefaultCostModel();

    // Derive the scalar candidate set from PDLL, including candidates without
    // a proved literal. This preserves one budget step per original candidate
    // without a second C++ inventory of the pattern roots.
    llvm::SmallDenseSet<mlir::OperationName, 8> roots;
    applicator.walkAllPatterns([&](const mlir::Pattern & pattern) {
        const auto root = pattern.getRootKind();
        assert(root && "symbolic PDLL patterns must have named roots");
        roots.insert(*root);
    });
    llvm::SmallVector<mlir::Operation *> candidates;
    module.walk<mlir::WalkOrder::PostOrder>([&](mlir::Operation * op) {
        if (roots.contains(op->getName()) || llvm::isa<mlir::scf::IfOp>(op)) {
            candidates.push_back(op);
        }
    });
    Changes changes;
    mlir::PatternRewriter rewriter(module.getContext());
    for (mlir::Operation * op : candidates) {
        if (!budget.take()) { break; }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
            if (auto take = condition(analysis.get(branch.getCondition()));
                take && select(branch, *take)) {
                ++changes.branches;
            }
            continue;
        }
        if (mlir::succeeded(applicator.matchAndRewrite(op, rewriter))) { ++changes.expressions; }
        // Producers are deliberately retained, including effectful calls and
        // object coercions whose boolean/string result made a consumer fold.
    }
    return changes;
}
} // namespace ctcompile::ctnative::symbolic
