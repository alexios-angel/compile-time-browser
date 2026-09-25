//===- PruneDeadStores.cpp - variables nothing reads, and what then dies ---===//
//
// PART 24 PHASE 63 STEP 7: the generated file compiles clean, and that is a
// test. -Wunused-variable and -Wunused-but-set-variable are the two
// diagnostics the loop conversion earns: every scf result becomes an
// emitc.variable assigned on every path and loaded after the loop, whether
// or not anything reads it. The load is trivially dead and a canonicalize
// removes it; the variable is not, because an emitc.assign has a write
// effect, so it survives as `double v5;` set on every iteration and never
// read. This pass removes exactly that, then whatever it left dead.
//
// ITS SECOND RULE IS A PATTERN: "a call whose single result nothing reads is a
// statement" is a structural match on one operation and an attribute on the
// same operation, so it is an OpRewritePattern run by the greedy driver. It
// was a PDLL file until 2026-09-15; every constraint and the rewrite were
// already one-call C++ bodies, so the .pdll bought an mlir-pdll build step, a
// PDL text parse at pass-run time and the bytecode interpreter for nothing a
// ten-line struct does not say more plainly. Erasing a write-only variable
// stays a hand loop because it needs every USE of a value classified, its
// users erased with it, and a fixpoint of its own.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Transforms/Passes.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#include "llvm/ADT/DenseSet.h"

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVEPRUNEDEADSTORES
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {

namespace ec = mlir::emitc;

// A CALL WHOSE SINGLE RESULT NOTHING READS IS A STATEMENT: the attribute is
// what the forked C++ emitter keys off to print `f(x);` instead of
// `double v = f(x);`. The call itself is never erased - a call may do anything
// - so the rewrite adds an attribute and stops. Refusing an operation that
// already carries it is the termination condition, and it is load-bearing: the
// rewrite neither replaces nor erases its root, so without it the greedy
// driver would re-enqueue the operation it just modified and match it again
// for ever. `emitc.call` and `emitc.call_opaque` both declare variadic
// results, hence the explicit count.
template <typename Call> struct UnreadCallIsAStatement : mlir::OpRewritePattern<Call> {
    using mlir::OpRewritePattern<Call>::OpRewritePattern;

    mlir::LogicalResult matchAndRewrite(Call call,
                                        mlir::PatternRewriter & rewriter) const override {
        mlir::Operation * o = call;
        if (o->getNumResults() != 1 || !o->getResult(0).use_empty() ||
            o->hasAttr("ctnative.statement")) {
            return mlir::failure();
        }
        // THROUGH THE REWRITER rather than by a bare setAttr: an in-place
        // modification the driver is not told about is one it cannot
        // schedule around.
        rewriter.modifyOpInPlace(
            o, [&] { o->setAttr("ctnative.statement", mlir::UnitAttr::get(o->getContext())); });
        return mlir::success();
    }
};

// EmitC ops declare NO memory effects - they model C expressions, and a call
// may do anything - so MLIR's generic "trivially dead" test refuses every one
// of them. These are the ones that are C expressions without side effects:
// unused, they are dead. A call, an assign, a variable are not on the list.
bool isPureExpression(mlir::Operation * o) {
    if (auto call = llvm::dyn_cast<ec::MemberCallOpaqueOp>(o)) {
        auto receiver = llvm::dyn_cast<ec::OpaqueType>(call.getReceiver().getType());
        return receiver && receiver.getValue() == "ctnative::js_num" &&
               call.getCallee() == "value" && call.getArgOperands().empty() && !call.getArgs() &&
               !call.getTemplateArgs() && call.getNumResults() == 1 &&
               call.getResult(0).getType().isF64();
    }
    return llvm::isa<ec::ConstantOp, ec::LiteralOp, ec::AddOp, ec::SubOp, ec::MulOp, ec::DivOp,
                     ec::RemOp, ec::CmpOp, ec::CastOp, ec::LogicalAndOp, ec::LogicalOrOp,
                     ec::LogicalNotOp, ec::UnaryMinusOp, ec::UnaryPlusOp, ec::ConditionalOp,
                     ec::BitwiseAndOp, ec::BitwiseOrOp, ec::BitwiseXorOp, ec::BitwiseNotOp,
                     ec::BitwiseLeftShiftOp, ec::BitwiseRightShiftOp, ec::LoadOp, ec::MemberOp,
                     ec::MemberOfPtrOp, ec::SubscriptOp>(o);
}

// True when nothing ever READS the variable: every use is the target of an
// assign, or there is no use at all.
bool isWriteOnly(ec::VariableOp var) {
    for (mlir::OpOperand & use : var.getResult().getUses()) {
        auto assign = llvm::dyn_cast<ec::AssignOp>(use.getOwner());
        if (!assign || assign.getVar() != var.getResult()) { return false; }
    }
    return true;
}

// WHAT THE PATTERN DRIVER ERASED WHILE IT WAS HERE.
//
// Hosting a pattern means hosting a pattern DRIVER, and every greedy entry
// point in this release "performs simple dead-code elimination before
// attempting to match any of the provided patterns". No GreedyRewriteConfig
// option turns that off. An `emitc.variable` nobody uses is memory-effect
// allocate-only, which is precisely what that DCE removes - so the moment this
// pass started running a driver, one of the erasures it used to make itself
// started happening inside the driver instead, and prune-dead-stores.mlir went
// red on the COUNT while the output IR stayed identical.
//
// The counts stay true by asking. A listener is the driver's own hook for
// exactly this, and the classification mirrors the loop below: a variable is a
// variable, everything else is an operation.
struct driver_erasures : mlir::RewriterBase::Listener {
    unsigned variables = 0;
    unsigned operations = 0;

    void notifyOperationErased(mlir::Operation * o) override {
        if (llvm::isa<ec::VariableOp>(o)) {
            ++variables;
        } else {
            ++operations;
        }
    }
};

struct CTNativePruneDeadStoresPass
    : impl::CTNativePruneDeadStoresBase<CTNativePruneDeadStoresPass> {
    using Base::Base;

    void runOnOperation() override {
        mlir::Operation * root = getOperation();
        unsigned prunedVariableCount = 0;
        unsigned prunedOpCount = 0;
        for (bool changed = true; changed;) {
            changed = false;
            // THE VARIABLES, collected first: erasing inside a walk is UB.
            llvm::SmallVector<ec::VariableOp> dead;
            root->walk([&](ec::VariableOp var) {
                if (isWriteOnly(var)) { dead.push_back(var); }
            });
            for (ec::VariableOp var : dead) {
                for (mlir::Operation * user :
                     llvm::make_early_inc_range(var.getResult().getUsers())) {
                    user->erase();
                }
                var.erase();
                ++prunedVariables;
                ++prunedVariableCount;
                changed = true;
            }
            // WHAT THAT LEFT DEAD: the assigned values' producers, if pure and
            // now unused. Post-order so a user goes before its producer; to a
            // fixpoint through the outer loop.
            llvm::SmallVector<mlir::Operation *> trivially;
            root->walk<mlir::WalkOrder::PostOrder>([&](mlir::Operation * o) {
                if (o == root) { return; }
                if (mlir::isOpTriviallyDead(o) || (isPureExpression(o) && o->use_empty())) {
                    trivially.push_back(o);
                }
            });
            for (mlir::Operation * o : trivially) {
                // A later entry may already have been erased through a parent
                // region; PostOrder lists children first, so no: children are
                // erased before their parent is considered. Erase directly.
                o->erase();
                ++prunedOps;
                ++prunedOpCount;
                changed = true;
            }
        }
        // Cleanup can erase a call's final use. Mark after its fixpoint so
        // the effect survives without declaring an unused result variable.
        // A CALL WHOSE RESULT NOTHING READS IS A STATEMENT, and only this tier
        // may say so - upstream's emitter declares a variable for it, and its
        // tests are kept verbatim here. The emitter honours the attribute; the
        // call itself stays, because a call may do anything.
        //
        // THE DRIVER IS NOT A LA CARTE: the greedy one arrives with folding,
        // constant CSE and AGGRESSIVE region simplification (block merging)
        // all on by default - none of which this pass has ever done, and one
        // of which (running upstream folding over this IR) has crashed
        // before. Every one is turned off explicitly rather than inherited.
        //
        // THE DRIVER DOES NOT SAY HOW OFTEN A PATTERN FIRED, so the count the
        // `report` option prints - which unused-call.mlir pins - is recovered
        // from the IR: the marks that were not there before are the marks
        // this run made.
        llvm::DenseSet<mlir::Operation *> markedBefore;
        root->walk([&](mlir::Operation * o) {
            if (o->hasAttr("ctnative.statement")) { markedBefore.insert(o); }
        });

        driver_erasures erased;
        mlir::RewritePatternSet patterns(&getContext());
        patterns.add<UnreadCallIsAStatement<ec::CallOp>, UnreadCallIsAStatement<ec::CallOpaqueOp>>(
            &getContext());
        mlir::GreedyRewriteConfig config;
        config.setRegionSimplificationLevel(mlir::GreedySimplifyRegionLevel::Disabled)
            .enableFolding(false)
            .enableConstantCSE(false)
            .setListener(&erased);
        if (mlir::failed(mlir::applyPatternsGreedily(root, std::move(patterns), config))) {
            signalPassFailure();
            return;
        }

        unsigned markedStatementCount = 0;
        root->walk([&](mlir::Operation * o) {
            if (o->hasAttr("ctnative.statement") && !markedBefore.contains(o)) {
                ++markedStatements;
                ++markedStatementCount;
            }
        });
        prunedVariables += erased.variables;
        prunedOps += erased.operations;
        prunedVariableCount += erased.variables;
        prunedOpCount += erased.operations;
        // Homebrew's release LLVM 23 accepts --mlir-pass-statistics but prints
        // no custom counters (the same is true of ResolveGlobals' statistics).
        // Keep the ODS statistics for builds that enable them, and give tests
        // and people an explicit, release-build-independent report as well.
        if (report) {
            root->emitRemark() << "pruned " << prunedVariableCount << " variable(s) and "
                               << prunedOpCount << " operation(s), marked " << markedStatementCount
                               << " call(s) as statements";
        }
    }
};

} // namespace

} // namespace ctcompile::ctnative
