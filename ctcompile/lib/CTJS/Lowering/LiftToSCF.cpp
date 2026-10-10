//===- LiftToSCF.cpp - Lift ctjs CFGs to structured control flow ----------===//
//
// Implements --ctjs-lift-to-scf. The importer emits blocks and cf branches,
// because bytecode is a register machine with relative jumps; left as is, they
// become `goto`s in the C++ output, which a C++ compiler optimises poorly.
//
// Upstream's transformCFGToSCF (Bahmann et al., "Perfect Reconstructability of
// Control Flow from Demand Dependence Graphs") lifts any CFG, irreducible ones
// included. Upstream's own --lift-cf-to-scf cannot be used: it walks only
// func::FuncOp, so on ctjs.func it matches nothing and silently changes
// nothing. This pass is the FunctionOpInterface walk around the upstream
// utility, plus clean-ups that make the lifted loops usable by native
// lowering.
//
//===----------------------------------------------------------------------===//
#include "Globals/RegisterFlow.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/Transforms/Passes.h"

#include "mlir/Conversion/ControlFlowToSCF/ControlFlowToSCF.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Transforms/CFGToSCF.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/RegionUtils.h"
#include "llvm/ADT/DenseSet.h"

#include <limits>
#include <memory>

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSLIFTTOSCF
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {

/// Replaces each block argument that only ever receives itself or one other
/// value with that value, and returns how many were dropped.
///
/// The importer hands every block the whole register file as block arguments,
/// so a loop header carries an argument for every variable, including one
/// assigned once before the loop and never inside it. Upstream's
/// simplifyRegions drops an argument only when every predecessor passes the
/// same value, and such a header argument receives the variable on the entry
/// edge and itself on the back edge, so it survives; transformCFGToSCF then
/// threads it through the scf.while it builds.
///
/// For a number that is an unneeded copy. For an object literal it is fatal to
/// native lowering: native lowering only accepts an object literal whose every
/// use is a get or set on the literal itself, so an access through the loop
/// argument makes it refuse the function. These arguments are trivial phis and
/// should collapse back to the one value they carry.
///
/// The rule is Braun et al. 2013, section 3.1, applied to a CFG: a phi whose
/// operands are itself or one value v is v. Here the phi is a non-entry block
/// argument and its operands are what each predecessor's terminator passes at
/// that index. Replace it by v, drop the operand from every predecessor, drop
/// the argument. Repeat to a fixpoint, because removing one can make another
/// trivial: an inner loop's header feeds an outer one's, which reads as
/// self-carried only once the inner one is gone.
///
/// Only block arguments and successor operands are touched; upstream's
/// canonicalizer is never run on ctjs IR (it once folded an scf.if of ctjs
/// constants and crashed). A predecessor whose terminator is not a
/// BranchOpInterface, a produced operand, and an argument with no incoming
/// value but itself (dead, and runRegionDCE's business) are left alone. The
/// dominance test is a second line of defence: in a valid CFG the value passed
/// on an entry edge already dominates the header.
unsigned dropSelfCarriedArguments(mlir::RewriterBase & rewriter, mlir::Region & region,
                                  mlir::DominanceInfo & dominance) {
    unsigned dropped = 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (mlir::Block & block : region) {
            if (block.isEntryBlock() || block.getNumArguments() == 0) { continue; }
            // Collect the edges before anything is rewritten: one entry per
            // (predecessor, successor index), so a conditional branch with
            // both arms into this block counts twice, as it must.
            llvm::SmallVector<std::pair<mlir::BranchOpInterface, unsigned>> edges;
            bool everyEdgeIsABranch = true;
            for (auto pred = block.pred_begin(), end = block.pred_end(); pred != end; ++pred) {
                auto branch = llvm::dyn_cast<mlir::BranchOpInterface>((*pred)->getTerminator());
                if (!branch) {
                    everyEdgeIsABranch = false;
                    break;
                }
                edges.emplace_back(branch, pred.getSuccessorIndex());
            }
            if (!everyEdgeIsABranch) { continue; }
            for (unsigned index = block.getNumArguments(); index-- > 0;) {
                mlir::BlockArgument arg = block.getArgument(index);
                mlir::Value common;
                bool trivial = true;
                for (auto [branch, successor] : edges) {
                    mlir::SuccessorOperands operands = branch.getSuccessorOperands(successor);
                    if (operands.isOperandProduced(index)) {
                        trivial = false;
                        break;
                    }
                    const mlir::Value incoming = operands[index];
                    if (incoming == arg) { continue; }
                    if (!common) {
                        common = incoming;
                        continue;
                    }
                    // Two different values reach it: a real phi (a variable
                    // assigned inside the loop, or on only one path before
                    // it), which stays. Without this check, `switching` in
                    // test/CTNative/Lowering/Objects/struct.mlir drops the
                    // `acc = lo` before its loop and returns hi.v even when
                    // the loop never runs. The dominance test below would not
                    // catch it: both literals are made before the loop, so
                    // both dominate the header.
                    if (incoming != common) {
                        trivial = false;
                        break;
                    }
                }
                if (!trivial || !common) { continue; }
                if (!dominance.properlyDominates(common, &block.front())) { continue; }
                rewriter.replaceAllUsesWith(arg, common);
                for (auto [branch, successor] : edges) {
                    branch.getSuccessorOperands(successor).erase(index);
                }
                block.eraseArgument(index);
                ++dropped;
                changed = true;
            }
        }
    }
    return dropped;
}

/// Whether a catch reads what was thrown. The importer copies the landing's
/// value into the register vector whether or not anything reads that register,
/// so follow it through successor operands to a use that is not another copy.
/// The walk is effectively unbounded; if the budget were hit it would count as
/// a read.
bool observesThrown(CatchLandOp landing) {
    const auto uses =
        globals_detail::registerFlowUses(landing.getThrown(), std::numeric_limits<unsigned>::max());
    return !uses || !uses->empty();
}

/// Upstream also replaces after-region arguments with then-yielded values
/// before remapping captures, but only remaps uses inside the then region, so
/// a value local to the before region would escape directly into the sibling
/// after region. Returns that value (the one \p branch yields for condition
/// argument \p index), or null when it is safe.
mlir::Value beforeLocalCapture(mlir::scf::WhileOp loop, mlir::scf::IfOp branch, mlir::Value value,
                               std::size_t index) {
    auto yielded =
        branch.thenYield().getOperand(llvm::cast<mlir::OpResult>(value).getResultNumber());
    const bool escapes = yielded.getParentRegion() == &loop.getBefore() &&
                         !loop.getAfterArguments()[index].use_empty();
    return escapes ? yielded : mlir::Value{};
}

/// Whether upstream's WhileMoveIfDown pattern is safe on \p loop.
/// WhileMoveIfDown replaces all uses while visiting condition operands. A
/// repeated if result would hide its later occurrences, leaving those body
/// arguments carrying the else value. Other rewrites can introduce aliases or
/// move nested loops, so each pattern application is guarded on the current
/// IR.
bool safeWhileInputs(mlir::scf::WhileOp loop) {
    llvm::DenseSet<mlir::Value> forwarded;
    for (auto [index, value] : llvm::enumerate(loop.getConditionOp().getArgs())) {
        auto branch = value.getDefiningOp<mlir::scf::IfOp>();
        if (!branch) { continue; }
        if (!forwarded.insert(value).second) { return false; }
        if (beforeLocalCapture(loop, branch, value, index)) { return false; }
    }
    return true;
}

/// Wraps an upstream scf.while canonicalization pattern so it applies only when
/// safeWhileInputs() holds.
struct CheckedWhilePattern : mlir::RewritePattern {
    explicit CheckedWhilePattern(std::unique_ptr<mlir::RewritePattern> original)
        : RewritePattern(mlir::scf::WhileOp::getOperationName(), original->getBenefit(),
                         original->getContext()),
          original(std::move(original)) {}

    mlir::LogicalResult matchAndRewrite(mlir::Operation * operation,
                                        mlir::PatternRewriter & rewriter) const override {
        if (!safeWhileInputs(llvm::cast<mlir::scf::WhileOp>(operation))) { return mlir::failure(); }
        return original->matchAndRewrite(operation, rewriter);
    }

    std::unique_ptr<mlir::RewritePattern> original;
};

/// Upstream moves then-yielded header values directly into after-region uses,
/// but only remaps captures inside the then region. This forwards those values
/// explicitly first, as extra after-region arguments, so upstream's
/// replacement finds no live after-region uses of the affected arguments. The
/// extra results are unused; both edges keep their values. Returns the loop
/// to continue with (a replacement, or \p loop unchanged).
mlir::scf::WhileOp forwardHeaderCaptures(mlir::scf::WhileOp loop, mlir::scf::IfOp branch,
                                         mlir::IRRewriter & rewriter) {
    llvm::DenseSet<mlir::Value> forwarded;
    llvm::SmallVector<std::pair<unsigned, mlir::Value>> captures;
    for (auto [index, value] : llvm::enumerate(loop.getConditionOp().getArgs())) {
        if (auto other = value.getDefiningOp<mlir::scf::IfOp>(); other && other != branch) {
            return loop;
        }
        if (value.getDefiningOp() != branch) { continue; }
        if (!forwarded.insert(value).second) { return loop; }
        if (mlir::Value yielded = beforeLocalCapture(loop, branch, value, index)) {
            captures.emplace_back(static_cast<unsigned>(index), yielded);
        }
    }
    if (captures.empty()) { return loop; }
    llvm::SmallVector<mlir::Type> types(loop.getResultTypes());
    for (auto [index, value] : captures) { types.push_back(value.getType()); }
    rewriter.setInsertionPoint(loop);
    auto replacement = mlir::scf::WhileOp::create(rewriter, loop.getLoc(), types, loop.getInits());
    replacement->setAttrs(loop->getAttrs());
    replacement.getBefore().takeBody(loop.getBefore());
    replacement.getAfter().takeBody(loop.getAfter());
    for (auto [index, value] : captures) {
        auto argument = replacement.getAfter().addArgument(value.getType(), loop.getLoc());
        replacement.getConditionOp().getArgsMutable().append(value);
        rewriter.replaceAllUsesWith(replacement.getAfterArguments()[index], argument);
    }
    rewriter.replaceOp(loop, replacement.getResults().take_front(loop.getNumResults()));
    return replacement;
}

/// CFG reconstruction encodes a loop's guard as an if result (1/0), then
/// truncates it back to i1. Recover that exact Boolean identity so upstream's
/// WhileMoveIfDown can put the guarded body in the after region. Neither arm's
/// effects move here, and non-Boolean flags keep their original control flow.
/// Returns whether anything changed, or failure if the rewrite driver failed.
mlir::FailureOr<bool> recoverLoopGuards(mlir::FunctionOpInterface function,
                                        mlir::IRRewriter & rewriter) {
    bool recovered = false;
    function->walk([&](mlir::scf::WhileOp loop) {
        auto condition = loop.getConditionOp();
        auto truncate = condition.getCondition().getDefiningOp<mlir::arith::TruncIOp>();
        auto result =
            truncate ? llvm::dyn_cast<mlir::OpResult>(truncate.getIn()) : mlir::OpResult{};
        auto branch =
            result ? llvm::dyn_cast<mlir::scf::IfOp>(result.getOwner()) : mlir::scf::IfOp{};
        if (!branch || !truncate.getType().isInteger(1) || !truncate->hasOneUse() ||
            truncate->getNextNode() != condition || branch->getNextNode() != truncate ||
            !branch.elseBlock() || !llvm::all_of(branch->getUsers(), [&](mlir::Operation * user) {
                return user == truncate || user == condition;
            })) {
            return;
        }
        const auto flag = [&](mlir::scf::YieldOp yield, unsigned expected) {
            auto constant =
                yield.getOperand(result.getResultNumber()).getDefiningOp<mlir::arith::ConstantOp>();
            auto integer = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                    : mlir::IntegerAttr{};
            return integer && integer.getValue() == expected;
        };
        if (!flag(branch.thenYield(), 1) || !flag(branch.elseYield(), 0)) { return; }
        loop = forwardHeaderCaptures(loop, branch, rewriter);
        if (!safeWhileInputs(loop)) { return; }
        rewriter.replaceOp(truncate, branch.getCondition());
        recovered = true;
    });
    if (!recovered) { return false; }
    // Apply only the checked scf.while patterns: no CTJS folding and no
    // scf.if-to-select conversion of arbitrary types. DCE removes the inert
    // mux constants.
    mlir::RewritePatternSet patterns(function->getContext());
    mlir::scf::WhileOp::getCanonicalizationPatterns(patterns, function->getContext());
    for (auto & pattern : patterns.getNativePatterns()) {
        pattern = std::make_unique<CheckedWhilePattern>(std::move(pattern));
    }
    mlir::GreedyRewriteConfig config;
    config.setRegionSimplificationLevel(mlir::GreedySimplifyRegionLevel::Disabled)
        .enableFolding(false)
        .enableConstantCSE(false);
    if (mlir::failed(mlir::applyPatternsGreedily(function, std::move(patterns), config))) {
        return mlir::failure();
    }
    return true;
}

/// Returns the reason \p body must keep its CFG for exception recovery,
/// phrased like the structuring utility's own refusal, or nullopt to lift it.
std::optional<std::string> handlerRefusal(mlir::FunctionOpInterface body) {
    // A function with a handler that uses only the ops listed below keeps its
    // CFG: exception recovery needs the importer's complete register vectors,
    // because a throw has no successor operands and the block arguments are
    // the only record of the state a catch can see. This is a preservation
    // filter only; recovery and native admission still prove control flow,
    // types and effects. Ordinary calls and property reads are preserved too,
    // including a prefix before a non-entry handler; neither they nor their
    // callees gain a proof by keeping the CFG. Other handler functions are
    // simplified and lifted normally, which exposes the value flow
    // closure/callback lifting needs.
    mlir::Operation * handler = nullptr;
    bool exceptionCandidate = true, holder = false;
    llvm::SmallVector<CatchLandOp> landings;
    body.getFunctionBody().walk([&](mlir::Operation * operation) {
        // Captured helpers also need their original saved registers. Allowing
        // them proves nothing about effects.
        if (llvm::isa<CreateClosureOp, CreateCellOp, CellGetOp>(operation)) {
            return mlir::WalkResult::advance();
        }
        // So do local iterator holders, but only when a catch reads the
        // thrown value. A landing that only suppresses an iterator close is
        // handled by the DOM close normalizer, which needs the simplified CFG.
        if (llvm::isa<BinaryStaticOp, CreateObjectOp, SetPropertyOp, DefineAccessorOp, IterableOp>(
                operation)) {
            holder = true;
            return mlir::WalkResult::advance();
        }
        if (auto landing = llvm::dyn_cast<CatchLandOp>(operation)) { landings.push_back(landing); }
        if (!llvm::isa<FrameEnterOp, FrameExitOp, RootOp, ConstantOp, BinaryOp, UnaryOp, CompareOp,
                       TruthyOp, FromBoolOp, PushHandlerOp, PopHandlerOp, CheckOp, CatchLandOp,
                       ThrowOp, ReturnOp, LoadGlobalOp, GetPropertyOp, CallOp, CallDirectOp,
                       mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(operation)) {
            exceptionCandidate = false;
            return mlir::WalkResult::interrupt();
        }
        if (!handler && llvm::isa<PushHandlerOp, CheckOp>(operation)) { handler = operation; }
        return mlir::WalkResult::advance();
    });
    if (handler && exceptionCandidate && (!holder || llvm::any_of(landings, observesThrown))) {
        return ("'" + handler->getName().getStringRef() +
                "' op transformation does not support terminators with side effects")
            .str();
    }
    return std::nullopt;
}

struct CTJSLiftToSCFPass : impl::CTJSLiftToSCFBase<CTJSLiftToSCFPass> {
    using CTJSLiftToSCFBase::CTJSLiftToSCFBase;

    void runOnOperation() override {
        mlir::ControlFlowToSCFTransformation transformation;
        bool changed = false;
        // Also counted locally for the `report` remark: the release LLVM build
        // prints no custom pass statistics, so tests assert on the remark
        // instead.
        unsigned droppedHere = 0;

        // Walk by FunctionOpInterface, unlike upstream's pass, so ctjs.func
        // (and any other function op) participates.
        const mlir::WalkResult walked =
            getOperation()->walk([&](mlir::FunctionOpInterface body) -> mlir::WalkResult {
                if (body.getFunctionBody().empty()) { return mlir::WalkResult::advance(); }

                if (const std::optional<std::string> kept = handlerRefusal(body)) {
                    body->setAttr("ctjs.not_structured",
                                  mlir::StringAttr::get(&getContext(), *kept));
                    return mlir::WalkResult::advance();
                }

                // Remove unreachable blocks first, because the utility refuses
                // a region containing one ("transformation does not support
                // unreachable blocks"), and the importer makes them
                // routinely: every leader gets a block up front, including the
                // one after a `ret`, which nothing can branch to.
                //
                // simplifyRegions also merges identical blocks and folds
                // trivial branches, which is only ever an improvement to the
                // input of a structuring algorithm.
                mlir::IRRewriter rewriter{&getContext()};
                (void)mlir::simplifyRegions(rewriter, body->getRegions());

                // Normalization can erase regions whose addresses a later
                // function reuses. Do not retain their dominance trees across
                // functions, even though pass analysis invalidation is at exit.
                mlir::DominanceInfo dominance;
                for (mlir::Region & region : body->getRegions()) {
                    // Drop trivial phis before the lift sees them: after
                    // simplifyRegions has dropped the single-predecessor
                    // arguments that make a back edge read as self-carried,
                    // and before transformCFGToSCF turns what is left into
                    // loop-carried values. Dropping an argument changes no
                    // edge, so the dominance the lift is about to use is
                    // still the truth.
                    const unsigned dropped = dropSelfCarriedArguments(rewriter, region, dominance);
                    droppedSelfCarriedArguments += dropped;
                    droppedHere += dropped;
                    changed |= dropped != 0;
                    // A refusal is expected and must not be an error. The
                    // utility reports one by emitting a diagnostic, and an
                    // emitted error fails the whole ctjs-opt run. This pass is
                    // an optimisation, and a region it cannot structure stays
                    // in CFG form, which the backend handles. So the
                    // diagnostic is swallowed and the reason is recorded on
                    // the function as ctjs.not_structured.
                    std::string refusal;
                    mlir::FailureOr<bool> lifted = mlir::failure();
                    {
                        mlir::ScopedDiagnosticHandler quiet{&getContext(),
                                                            [&](mlir::Diagnostic & note) {
                                                                if (refusal.empty()) {
                                                                    refusal = note.str();
                                                                }
                                                                return mlir::success();
                                                            }};
                        lifted = mlir::transformCFGToSCF(region, transformation, dominance);
                    }
                    if (mlir::failed(lifted)) {
                        body->setAttr("ctjs.not_structured",
                                      mlir::StringAttr::get(&getContext(), refusal));
                        return mlir::WalkResult::advance();
                    }
                    changed |= *lifted;
                }
                const auto normalized = recoverLoopGuards(body, rewriter);
                if (mlir::failed(normalized)) { return mlir::WalkResult::interrupt(); }
                changed |= *normalized;
                // CFG reconstruction can leave an unused mux constant before
                // frame_enter even without a loop. Keep the source entry first.
                body->walk([&](mlir::arith::ConstantOp constant) {
                    if (!constant->use_empty()) { return; }
                    rewriter.eraseOp(constant);
                    changed = true;
                });
                return mlir::WalkResult::advance();
            });
        if (walked.wasInterrupted()) { signalPassFailure(); }

        if (report) {
            getOperation()->emitRemark()
                << "dropped " << droppedHere << " self-carried block argument(s)";
        }

        // Invalidate every analysis. transformCFGToSCF rewrites blocks, so a
        // cached DominanceInfo would describe a CFG that no longer exists and
        // cause failures several passes later.
        if (changed) {
            getAnalysisManager().invalidate(mlir::AnalysisManager::PreservedAnalyses{});
        }
    }
};

} // namespace

} // namespace ctcompile::ctjs
