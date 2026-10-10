//===- Rewrite.cpp - Fold proved constants and branches -------------------===//
//
// Implements symbolic::rewrite (see Facts.h).
//
//===----------------------------------------------------------------------===//

#include "Facts.h"

#include "../PartialEvaluation/Heap.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Rewrite/PatternApplicator.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"

#include <cassert>

namespace ctcompile::ctnative::symbolic {
namespace {
/// The proved literal for `op`'s result, adapted to the result's type: an
/// IntegerAttr for an integer result, a ctjs literal for a ctjs value. Null if
/// nothing is proved. The analysis proves the value; this only adapts it.
mlir::Attribute literal(mlir::Operation * op, const Analysis & analysis) {
    const auto known = analysis.get(op->getResult(0)).literal;
    if (!known) { return {}; }
    const auto type = op->getResult(0).getType();
    if (auto integer = llvm::dyn_cast<mlir::IntegerType>(type)) {
        return partial_eval::integerConstant(integer, known);
    }
    if (!llvm::isa<ctjs::ValueType>(type) || llvm::isa<mlir::IntegerAttr>(known)) { return {}; }
    return known;
}

/// Replaces a `Root` operation whose result is proved constant with a
/// `Constant` of the same result type. `Attr` is the attribute class the
/// constant's builder wants: `arith.constant` takes a TypedAttr, and literal()
/// only ever hands an integer result an IntegerAttr.
template <typename Root, typename Constant, typename Attr = mlir::Attribute>
struct Precompute : mlir::OpRewritePattern<Root> {
    Precompute(mlir::MLIRContext * context, const Analysis & analysis)
        : mlir::OpRewritePattern<Root>(context), analysis(analysis) {}
    const Analysis & analysis;

    mlir::LogicalResult matchAndRewrite(Root root,
                                        mlir::PatternRewriter & rewriter) const override {
        const mlir::Attribute value = literal(root, analysis);
        if (!value) { return mlir::failure(); }
        rewriter.replaceOpWithNewOp<Constant>(root, root->getResult(0).getType(),
                                              llvm::cast<Attr>(value));
        return mlir::success();
    }
};

/// Replaces `branch` with the contents of its `take` region. Moving a region
/// and remapping its yield is control-flow surgery, so it stays a hand-written
/// rewrite rather than a pattern.
bool select(mlir::scf::IfOp branch, bool take) {
    auto & selected = take ? branch.getThenRegion() : branch.getElseRegion();
    if (selected.empty()) {
        branch.erase();
        return true;
    }
    auto yield = take ? branch.thenYield() : branch.elseYield();
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

Changes rewrite(mlir::ModuleOp module, const Analysis & analysis, WorkBudget & budget) {
    // The patterns capture this invocation's analysis by reference; the
    // frozen set and applicator are destroyed before it goes out of scope.
    // Truthiness returns i1 and arith.trunci an integer, so those two become
    // arith constants; ctjs value results become ctjs.constant.
    mlir::RewritePatternSet patterns(module.getContext());
    patterns.add<Precompute<ctjs::BinaryOp, ctjs::ConstantOp>,
                 Precompute<ctjs::BinaryStaticOp, ctjs::ConstantOp>,
                 Precompute<ctjs::UnaryOp, ctjs::ConstantOp>,
                 Precompute<ctjs::CompareOp, ctjs::ConstantOp>,
                 Precompute<ctjs::TruthyOp, mlir::arith::ConstantOp, mlir::TypedAttr>,
                 Precompute<mlir::arith::TruncIOp, mlir::arith::ConstantOp, mlir::TypedAttr>>(
        module.getContext(), analysis);
    mlir::FrozenRewritePatternSet frozen(std::move(patterns));
    mlir::PatternApplicator applicator(frozen);
    applicator.applyDefaultCostModel();

    // Derive the candidate set from the patterns' root kinds, including
    // candidates without a proved literal, so each original candidate costs
    // one budget step and the root list is not written out twice.
    llvm::SmallDenseSet<mlir::OperationName, 8> roots;
    applicator.walkAllPatterns([&](const mlir::Pattern & pattern) {
        const auto root = pattern.getRootKind();
        assert(root && "symbolic patterns must have named roots");
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
        if (!budget.spend()) { break; }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
            if (auto take = condition(analysis.get(branch.getCondition()));
                take && select(branch, *take)) {
                ++changes.branches;
            }
            continue;
        }
        if (mlir::succeeded(applicator.matchAndRewrite(op, rewriter))) { ++changes.expressions; }
        // Operand producers are deliberately kept, including effectful calls
        // and object coercions whose Boolean or String result let a consumer
        // fold.
    }
    return changes;
}
} // namespace ctcompile::ctnative::symbolic
