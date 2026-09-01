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
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Transforms/CFGToSCF.h"
#include "mlir/Transforms/RegionUtils.h"

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSLIFTTOSCF
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {

struct CTJSLiftToSCFPass : impl::CTJSLiftToSCFBase<CTJSLiftToSCFPass> {
    using CTJSLiftToSCFBase::CTJSLiftToSCFBase;

    void runOnOperation() override {
        mlir::ControlFlowToSCFTransformation transformation;
        bool changed = false;

        // BY INTERFACE, WHICH IS THE WHOLE POINT OF THIS FILE. Anything that
        // is a function participates - ctjs.func today, func.func if this
        // pipeline ever grows one.
        const mlir::WalkResult walked =
            getOperation()->walk([&](mlir::FunctionOpInterface body) -> mlir::WalkResult {
                if (body.getFunctionBody().empty()) { return mlir::WalkResult::advance(); }

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

                auto & dominance = getAnalysis<mlir::DominanceInfo>();
                for (mlir::Region & region : body->getRegions()) {
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
                return mlir::WalkResult::advance();
            });
        (void)walked;

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
