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
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Transforms/Passes.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/Operation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVEPRUNEDEADSTORES
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {

namespace ec = mlir::emitc;

// EmitC ops declare NO memory effects - they model C expressions, and a call
// may do anything - so MLIR's generic "trivially dead" test refuses every one
// of them. These are the ones that are C expressions without side effects:
// unused, they are dead. A call, an assign, a variable are not on the list.
bool isPureExpression(mlir::Operation * o) {
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
        // Homebrew's release LLVM 23 accepts --mlir-pass-statistics but prints
        // no custom counters (the same is true of ResolveGlobals' statistics).
        // Keep the ODS statistics for builds that enable them, and give tests
        // and people an explicit, release-build-independent report as well.
        if (report) {
            root->emitRemark() << "pruned " << prunedVariableCount << " variable(s) and "
                               << prunedOpCount << " operation(s)";
        }
    }
};

} // namespace

} // namespace ctcompile::ctnative
