//===- PruneDeadStores.cpp - Remove write-only EmitC variables ------------===//
//
// Generated C++ must compile without warnings. Converting scf loops to EmitC
// makes every loop result an emitc.variable assigned on every path and loaded
// after the loop, whether or not anything reads it. Canonicalize removes the
// dead load but not the variable, because emitc.assign has a write effect, so
// it survives as `double v5;` that is set and never read and triggers
// -Wunused-but-set-variable. This pass removes such write-only variables and
// then whatever they left dead.
//
// It also marks a call whose single result is unused as a statement. That is
// an OpRewritePattern run by the greedy driver; erasing write-only variables
// stays a hand-written loop because it must classify every use of a value,
// erase those users too, and iterate to its own fixpoint.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Transforms/Passes.h"
#include "ctcompile/Support/OpaqueType.h"

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

/// Marks a call whose single result nothing reads with `ctnative.statement`,
/// which the forked C++ emitter keys off to print `f(x);` instead of
/// `double v = f(x);`. The call itself is never erased, since a call may do
/// anything. Refusing an operation that already carries the attribute is the
/// termination condition: the rewrite neither replaces nor erases its root, so
/// without that check the greedy driver would re-enqueue the operation it just
/// modified and match it forever. `emitc.call` and `emitc.call_opaque` both
/// declare variadic results, hence the explicit result count.
template <typename Call> struct UnreadCallIsAStatement : mlir::OpRewritePattern<Call> {
    using mlir::OpRewritePattern<Call>::OpRewritePattern;

    mlir::LogicalResult matchAndRewrite(Call call,
                                        mlir::PatternRewriter & rewriter) const override {
        mlir::Operation * o = call;
        if (o->getNumResults() != 1 || !o->getResult(0).use_empty() ||
            o->hasAttr("ctnative.statement")) {
            return mlir::failure();
        }
        // Go through the rewriter rather than a bare setAttr: the driver
        // cannot schedule around an in-place modification it is not told
        // about.
        rewriter.modifyOpInPlace(
            o, [&] { o->setAttr("ctnative.statement", mlir::UnitAttr::get(o->getContext())); });
        return mlir::success();
    }
};

/// Whether `o` is a C expression with no side effects, and so dead when
/// unused. EmitC ops declare no memory effects, so MLIR's generic "trivially
/// dead" test refuses all of them. Calls (except js_num's `value()`
/// accessor), assigns and variables are not on the list.
bool isPureExpression(mlir::Operation * o) {
    if (auto call = llvm::dyn_cast<ec::MemberCallOpaqueOp>(o)) {
        return isOpaque(call.getReceiver().getType(), "ctnative::js_num") &&
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

/// True when nothing ever reads the variable: every use is the target of an
/// assign, or there is no use at all.
bool isWriteOnly(ec::VariableOp var) {
    for (mlir::OpOperand & use : var.getResult().getUses()) {
        auto assign = llvm::dyn_cast<ec::AssignOp>(use.getOwner());
        if (!assign || assign.getVar() != var.getResult()) { return false; }
    }
    return true;
}

/// Counts the operations the greedy driver erases. Every greedy entry point
/// runs simple dead-code elimination before matching, with no option to turn
/// it off, and an unused emitc.variable (allocate-only) is exactly what that
/// removes. Counting through this listener keeps the pass's pruned-variable
/// and pruned-op totals accurate. The classification mirrors the loop in
/// runOnOperation: a variable is a variable, anything else is an operation.
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
            // Collect the variables first: erasing inside a walk is undefined
            // behaviour.
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
            // Then erase what that left dead: the assigned values' producers,
            // if pure and now unused. Post-order so a user goes before its
            // producer; the outer loop repeats until a fixpoint.
            llvm::SmallVector<mlir::Operation *> trivially;
            root->walk<mlir::WalkOrder::PostOrder>([&](mlir::Operation * o) {
                if (o == root) { return; }
                if (mlir::isOpTriviallyDead(o) || (isPureExpression(o) && o->use_empty())) {
                    trivially.push_back(o);
                }
            });
            for (mlir::Operation * o : trivially) {
                // Post-order lists children before their parent, so no entry
                // has been erased through its parent's region when reached.
                o->erase();
                ++prunedOps;
                ++prunedOpCount;
                changed = true;
            }
        }
        // Mark unread calls only after the cleanup fixpoint, since cleanup can
        // erase a call's last use. Upstream's emitter would declare a variable
        // for such a call (and its tests are kept verbatim); the forked emitter
        // prints it as a statement when ctnative.statement is set.
        //
        // The greedy driver enables folding, constant CSE and aggressive
        // region simplification (block merging) by default. This pass wants
        // none of them (upstream folding over this IR has crashed), so each is
        // disabled explicitly.
        //
        // The driver does not report how often a pattern fired, so the count
        // printed by the `report` option (pinned by unused-call.mlir) is the
        // number of ctnative.statement marks that were not there before.
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
        // A release LLVM build accepts --mlir-pass-statistics but prints no
        // custom counters (ResolveGlobals' statistics have the same problem).
        // Keep the ODS statistics for builds that enable them, and also give
        // tests and people an explicit report that works in any build.
        if (report) {
            root->emitRemark() << "pruned " << prunedVariableCount << " variable(s) and "
                               << prunedOpCount << " operation(s), marked " << markedStatementCount
                               << " call(s) as statements";
        }
    }
};

} // namespace

} // namespace ctcompile::ctnative
