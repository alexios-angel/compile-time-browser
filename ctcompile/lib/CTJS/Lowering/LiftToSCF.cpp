// RECOVERING STRUCTURED CONTROL FLOW FROM A CFG THIS PROJECT BUILT OUT OF
// JUMPS.
//
// The importer emits blocks and `cf.br`/`cf.cond_br`, because that is what
// bytecode is: a register machine with relative jumps and no notion of a loop.
// The C++ that falls out the other end is a chain of `goto label9`, which is
// correct, unreadable, and hard for a C++ compiler to optimise - it cannot see
// a loop it might unroll or a value it might keep in a register.
//
// MLIR ALREADY SOLVES THIS AND WE DO NOT HAVE TO WRITE IT. `transformCFGToSCF`
// implements Bahmann et al., "Perfect Reconstructability of Control Flow from
// Demand Dependence Graphs" - it lifts an ARBITRARY CFG, including irreducible
// ones, by inserting edge multiplexers rather than giving up. Every hard part
// is upstream's.
//
// SO WHY IS THIS FILE HERE AT ALL. Upstream's own pass, --lift-cf-to-scf, is
// one line away from working on this dialect and that line is fatal:
//
//     op->walk([&](func::FuncOp funcOp) { ... })
//
// It looks for `func.func` BY TYPE, not for FunctionOpInterface. `ctjs.func`
// implements FunctionOpInterface and is not a func::FuncOp, so the pass walks
// the module, matches nothing, reports success and changes NOTHING - which is
// the worst failure mode available, because it looks exactly like a CFG that
// was already structured. It was measured: twelve `cf` operations before,
// twelve after, zero `scf`.
//
// Everything else upstream exposes. `mlir::ControlFlowToSCFTransformation` is
// a public class implementing the seven CFGToSCFInterface methods against the
// SCF dialect, and `mlir::transformCFGToSCF` is a public function. This pass is
// the walk they are missing and nothing more.
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

#include <memory>

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSLIFTTOSCF
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {

// A BLOCK ARGUMENT THAT IS ONLY EVER ITSELF OR ONE OTHER VALUE IS THAT VALUE.
//
// The importer hands every block the whole register file as block arguments,
// so a loop header carries an argument for every variable - including one
// assigned once BEFORE the loop and never inside it. Upstream's
// simplifyRegions drops an argument only when every predecessor passes the
// same value, and a loop header's argument for such a variable is passed the
// variable on the entry edge and ITSELF on the back edge, so it survives;
// transformCFGToSCF then threads it through the scf.while it builds, and
// through a loop result when the variable is read after the loop.
//
// For a number that is an unneeded copy. For an object literal it is fatal to
// the native tier: every access inside the loop goes through the loop's
// argument rather than the literal, the shape is no longer closed (Phase 56A's
// proof is "every use is a get or set on the literal ITSELF"), and the
// function is refused. Obligation O-3 - one stack slot, however many SSA
// values reach it - is exactly this case: the SSA values are a trivial phi.
//
// THE RULE IS BRAUN ET AL. 2013 §3.1, applied to a CFG rather than during SSA
// construction: a phi whose operands are itself or one value v IS v. Here the
// phi is a non-entry block argument and its operands are what each
// predecessor's terminator passes at that index. Replace it by v, drop the
// operand from every predecessor, drop the argument. To a fixpoint, because
// removing one can make another trivial: an inner loop's header feeds an
// outer's, and reads as self-carried only once the inner one is gone.
//
// WHAT IT DOES NOT TOUCH. Anything but a block argument and a successor
// operand; upstream's canonicalizer is never run on ctjs IR (it folded an
// scf.if of ctjs constants once and crashed). A predecessor whose terminator
// is not a BranchOpInterface, a produced operand, and an argument with no
// incoming value but itself (dead, and runRegionDCE's business) are all left
// alone. The dominance test is a belt: in a valid CFG the value passed on an
// entry edge already dominates the header, because the back edges are the
// only predecessors the header dominates.
unsigned dropSelfCarriedArguments(mlir::RewriterBase & rewriter, mlir::Region & region,
                                  mlir::DominanceInfo & dominance) {
    unsigned dropped = 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (mlir::Block & block : region) {
            if (block.isEntryBlock() || block.getNumArguments() == 0) { continue; }
            // THE EDGES FIRST, before anything is rewritten: one entry per
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
                    // THE GUARD: two different values reach it. That is a
                    // real phi - a variable assigned inside the loop, or on
                    // only one path before it - and it stays. MEASURED, with
                    // this deleted: `switching` in native-struct.mlir lowers,
                    // and the `acc = lo` before its loop is silently dropped -
                    // it returns hi.v for every n, including n = 0, where the
                    // loop never runs. The dominance test below does NOT
                    // catch that one: both literals are made before the loop,
                    // so both dominate the header.
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

// WhileMoveIfDown replaces all uses while visiting condition operands. A
// repeated if result would hide its later occurrences, leaving those body
// arguments carrying the else value. Other rewrites can introduce aliases or
// move nested loops, so guard each pattern application on the current IR.
bool safeWhileInputs(mlir::scf::WhileOp loop) {
    llvm::DenseSet<mlir::Value> forwarded;
    for (auto [index, value] : llvm::enumerate(loop.getConditionOp().getArgs())) {
        auto branch = value.getDefiningOp<mlir::scf::IfOp>();
        if (!branch) { continue; }
        if (!forwarded.insert(value).second) { return false; }
        // Upstream also replaces after arguments with then-yielded values
        // before remapping captures, but only remaps uses inside then. A
        // before-local value would escape directly into the sibling after.
        auto yielded =
            branch.thenYield().getOperand(llvm::cast<mlir::OpResult>(value).getResultNumber());
        if (yielded.getParentRegion() == &loop.getBefore() &&
            !loop.getAfterArguments()[index].use_empty()) {
            return false;
        }
    }
    return true;
}

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

// Upstream moves then-yielded header values directly into after-region uses,
// but only remaps captures inside then. Forward those values explicitly first,
// so its replacement encounters no live after uses of the affected arguments.
// Extra results are unused; both the true and false edges retain their values.
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
        auto yielded =
            branch.thenYield().getOperand(llvm::cast<mlir::OpResult>(value).getResultNumber());
        if (yielded.getParentRegion() == &loop.getBefore() &&
            !loop.getAfterArguments()[index].use_empty()) {
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

// CFG reconstruction encodes a loop's guard as an if result (1/0), then
// truncates it back to i1. Recover that exact Boolean identity so upstream's
// WhileMoveIfDown can put the guarded body in the after region. Neither arm's
// effects move here, and non-Boolean flags keep their original control flow.
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
    // Only checked SCF while patterns; never CTJS folding or IfOp's
    // arbitrary-type select conversion. DCE removes the inert mux constants.
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

struct CTJSLiftToSCFPass : impl::CTJSLiftToSCFBase<CTJSLiftToSCFPass> {
    using CTJSLiftToSCFBase::CTJSLiftToSCFBase;

    void runOnOperation() override {
        mlir::ControlFlowToSCFTransformation transformation;
        bool changed = false;
        // COUNTED HERE AS WELL AS IN THE ODS STATISTIC, for the reason
        // --ctnative-prune-dead-stores gives: Homebrew's release LLVM 23
        // accepts --mlir-pass-statistics and prints no custom counter at all
        // (measured - ResolveGlobals' three print nothing either), so a
        // statistic cannot be asserted on by a test here. The remark can.
        unsigned droppedHere = 0;

        // BY INTERFACE, WHICH IS THE WHOLE POINT OF THIS FILE. Anything that
        // is a function participates - ctjs.func today, func.func if this
        // pipeline ever grows one.
        const mlir::WalkResult walked =
            getOperation()->walk([&](mlir::FunctionOpInterface body) -> mlir::WalkResult {
                if (body.getFunctionBody().empty()) { return mlir::WalkResult::advance(); }

                // Exception candidates need the complete importer
                // register vectors: throw has no successor operands, so its
                // block arguments identify the catch-visible state. This is
                // only a preservation filter; recovery and native admission
                // still prove the actual control flow, types and effects.
                // Preserve ordinary calls/property reads too, including a
                // prefix before a non-entry handler; neither those effects
                // nor their callees acquire proof by keeping their CFG.
                // Other handler functions keep legacy simplification, which
                // exposes the value flow needed by closure/callback lifting.
                mlir::Operation * handler = nullptr;
                bool exceptionCandidate = true;
                body.getFunctionBody().walk([&](mlir::Operation * operation) {
                    // Captured helpers also need their original saved registers.
                    if (llvm::isa<CreateClosureOp, CreateCellOp, CellGetOp>(operation)) {
                        return mlir::WalkResult::advance();
                    }
                    if (!llvm::isa<FrameEnterOp, FrameExitOp, RootOp, ConstantOp, BinaryOp, UnaryOp,
                                   CompareOp, TruthyOp, FromBoolOp, PushHandlerOp, PopHandlerOp,
                                   CheckOp, CatchLandOp, ThrowOp, ReturnOp, LoadGlobalOp,
                                   GetPropertyOp, CallOp, CallDirectOp, mlir::cf::BranchOp,
                                   mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(operation)) {
                        exceptionCandidate = false;
                        return mlir::WalkResult::interrupt();
                    }
                    if (!handler && llvm::isa<PushHandlerOp, CheckOp>(operation)) {
                        handler = operation;
                    }
                    return mlir::WalkResult::advance();
                });
                if (handler && exceptionCandidate) {
                    body->setAttr(
                        "ctjs.not_structured",
                        mlir::StringAttr::get(
                            &getContext(),
                            ("'" + handler->getName().getStringRef() +
                             "' op transformation does not support terminators with side effects")
                                .str()));
                    return mlir::WalkResult::advance();
                }

                // UNREACHABLE BLOCKS FIRST, because the utility refuses a
                // region containing one outright - "transformation does not
                // support unreachable blocks" - and this importer makes them
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
                    // THE TRIVIAL PHIS GO BEFORE THE LIFT SEES THEM: after
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
                    // A REFUSAL IS EXPECTED AND MUST NOT BE AN ERROR.
                    //
                    // The utility reports one by emitting a diagnostic, and an
                    // emitted error fails the whole ctjs-opt run - so lifting a
                    // module containing one try/catch would produce nothing at
                    // all. That is backwards: this pass is an IMPROVEMENT, and
                    // a region it cannot structure is left in CFG form for the
                    // backend that reads CFG perfectly well.
                    //
                    // So the diagnostic is swallowed here and the reason is
                    // recorded on the function instead, where the work list
                    // already lives.
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

        // THE ANALYSIS IS INVALIDATED WHOLESALE. transformCFGToSCF rewrites
        // blocks, so a cached DominanceInfo describes a CFG that no longer
        // exists - and a stale dominance analysis is the kind of thing that
        // produces a verifier failure three passes later.
        if (changed) {
            getAnalysisManager().invalidate(mlir::AnalysisManager::PreservedAnalyses{});
        }
    }
};

} // namespace

} // namespace ctcompile::ctjs
