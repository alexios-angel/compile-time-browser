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
// ONE OF ITS TWO RULES IS DECLARATIVE. "A call whose single result nothing
// reads is a statement" is a structural match on an operation and an attribute
// on the same operation, which is the shape part 4 of the master plan sends to
// PDLL - so it lives in PruneDeadStores.pdll and reaches this file as a
// generated header. THE OTHER RULE DOES NOT GO THERE and the difference is the
// point: erasing a write-only variable needs every USE of a value classified,
// its users erased with it, and the whole thing run to a fixpoint. PDL matches
// structure, not use lists, and it has no fixpoint of its own beyond the greedy
// driver's - so that half stays C++ under the policy's "must inspect uses" and
// "creates or splits blocks" carve-outs.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Transforms/Passes.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#include "llvm/ADT/DenseSet.h"

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVEPRUNEDEADSTORES
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

// --- the PDLL pattern's native bodies -----------------------------------------
//
// NAMED FUNCTIONS, ONE CALL EACH FROM THE .pdll. The policy allows "native
// constraint and rewrite bodies invoked from PDLL, each a single call into a
// named function" and forbids anything longer inside the `[{ }]`, because a
// pattern file is not clang-formatted, not covered by the warning flags and
// not separately testable. These are those functions. They are NOT in the
// anonymous namespace below: the generated header spells them by qualified
// name.
//
// EVERY ONE TAKES THE REWRITER IT DOES NOT USE. mlir-pdll emits the wrapper as
// `static LogicalResult FooPDLFn(PatternRewriter &rewriter, ...)` whether the
// body wants the rewriter or not, and this project builds with -Wextra -Werror,
// where an unused parameter is a build failure. Threading it through is the
// cheapest way to keep the generated file compiling clean.
namespace pdll {

// EXACTLY ONE RESULT - which PDL cannot state. `emitc.call` and
// `emitc.call_opaque` both declare `Variadic<EmitCType>` results in ODS, so a
// PDLL result list does not pin the count: `op<emitc.call> -> (r: Type)`
// compiles to a `!pdl.range<value>` handed to whatever the constraint declared,
// with no diagnostic from mlir-pdll.
mlir::LogicalResult hasExactlyOneResult(mlir::PatternRewriter &, mlir::Operation * o) {
    return mlir::success(o->getNumResults() == 1);
}

// NOTHING READS IT. A use count is not operation structure, so PDL cannot ask.
mlir::LogicalResult nothingReadsTheResult(mlir::PatternRewriter &, mlir::Operation * o) {
    return mlir::success(o->getNumResults() == 1 && o->getResult(0).use_empty());
}

// AND THE PATTERN'S TERMINATION CONDITION. The rewrite neither replaces nor
// erases its root, so without this the greedy driver would re-enqueue the
// operation it just modified and match it again for ever.
mlir::LogicalResult notAlreadyAStatement(mlir::PatternRewriter &, mlir::Operation * o) {
    return mlir::success(!o->hasAttr("ctnative.statement"));
}

// THE REWRITE, THROUGH THE REWRITER rather than by a bare setAttr: an in-place
// modification the driver is not told about is a modification it cannot
// schedule around.
void markAsStatement(mlir::PatternRewriter & rewriter, mlir::Operation * o) {
    rewriter.modifyOpInPlace(
        o, [&] { o->setAttr("ctnative.statement", mlir::UnitAttr::get(o->getContext())); });
}

} // namespace pdll

namespace {

namespace ec = mlir::emitc;

// mlir-pdll's output, from PruneDeadStores.pdll. Generated into the BUILD tree
// by add_mlir_pdll_library - Principle 9: never committed, never a source.
#include "PruneDeadStores.h.inc"

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

// WHAT THE PATTERN DRIVER ERASED WHILE IT WAS HERE.
//
// Hosting a PDL pattern means hosting a pattern DRIVER, and every greedy entry
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
        // A CALL WHOSE RESULT NOTHING READS IS A STATEMENT, and only this tier
        // may say so - upstream's emitter declares a variable for it, and its
        // tests are kept verbatim here. The emitter honours the attribute; the
        // call itself stays, because a call may do anything.
        //
        // THE RULE ITSELF IS IN PruneDeadStores.pdll. What is left here is the
        // driver and the counting, and both are things PDL does not do:
        //
        //   THE DRIVER IS NOT A LA CARTE. A PDL pattern can only be run by a
        //   pattern driver, and the greedy one arrives with folding, constant
        //   CSE and AGGRESSIVE region simplification (block merging) all on by
        //   default - none of which this pass has ever done, and one of which
        //   (running upstream folding over this IR) has crashed before. Every
        //   one is turned off explicitly rather than inherited.
        //
        //   THE DRIVER DOES NOT SAY HOW OFTEN A PATTERN FIRED. There is no
        //   per-pattern hit count to read, and a native rewrite is registered
        //   as a plain function pointer, so it cannot capture a counter. The
        //   count the `report` option prints - which unused-call.mlir pins - is
        //   therefore recovered from the IR: the marks that were not there
        //   before are the marks this run made.
        llvm::DenseSet<mlir::Operation *> markedBefore;
        root->walk([&](mlir::Operation * o) {
            if (o->hasAttr("ctnative.statement")) { markedBefore.insert(o); }
        });

        driver_erasures erased;
        mlir::RewritePatternSet patterns(&getContext());
        populateGeneratedPDLLPatterns(patterns);
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
        unsigned prunedVariableCount = erased.variables;
        unsigned prunedOpCount = erased.operations;
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
                               << prunedOpCount << " operation(s), marked " << markedStatementCount
                               << " call(s) as statements";
        }
    }
};

} // namespace

} // namespace ctcompile::ctnative
