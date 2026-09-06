#include "Driver.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Transforms/Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {
#define GEN_PASS_DEF_CTNATIVESUPERCOMPILE
#include "ctcompile/CTNative/Transforms/Passes.h.inc"
namespace {
struct CTNativeSupercompilePass : impl::CTNativeSupercompileBase<CTNativeSupercompilePass> {
    using CTNativeSupercompileBase::CTNativeSupercompileBase;
    void runOnOperation() override {
        auto module = getOperation();
        module.walk([](mlir::Operation * op) {
            op->removeAttr("ctnative.supercompile_reason");
            op->removeAttr("ctnative.supercompile_summary");
        });
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
        module.walk([&](ctjs::CallDirectOp call) {
            auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                call, call.getCalleeAttr());
            auto caller = call->getParentOfType<ctjs::FuncOp>();
            // Seed only original indexed source functions, not generated
            // variants. Generic whistle boundaries cannot restart a fresh
            // expansion on the next pass invocation. This is selection policy,
            // not a proof inferred from optimization annotations.
            if (target && caller && caller != target && functionIndex(caller)) {
                callers[target].push_back(call);
            }
        });
        llvm::SmallVector<mlir::Operation *> candidates;
        mlir::Builder at(&getContext());
        for (auto & [operation, calls] : callers) {
            auto function = llvm::cast<ctjs::FuncOp>(operation);
            auto reason = supercompilation::refusal(function, module);
            if (!reason.empty()) {
                function->setAttr("ctnative.supercompile_reason", at.getStringAttr(reason));
            } else if (calls.size() > 1) {
                candidates.push_back(operation);
            }
        }
        unsigned kernels = 0, contexts = 0, folds = 0, whistles = 0, generalizations = 0,
                 operations = 0, steps = 0;
        for (auto * operation : candidates) {
            auto function = llvm::cast<ctjs::FuncOp>(operation);
            supercompilation::Driver driver(
                module, function,
                {maxContexts - contexts, maxSteps - steps, maxResidualOps - operations, maxGrowth});
            // Keep one real generic call as inference evidence for the source.
            const bool selected =
                driver.build(llvm::ArrayRef<ctjs::CallDirectOp>(callers[operation]).drop_front());
            steps += driver.statistics().steps;
            if (!selected) {
                function->setAttr("ctnative.supercompile_reason",
                                  at.getStringAttr(driver.reason()));
                continue;
            }
            driver.commit();
            const auto & stats = driver.statistics();
            ++kernels;
            contexts += stats.contexts;
            folds += stats.folds;
            whistles += stats.whistles;
            generalizations += stats.generalizations;
            operations += stats.residualOps;
        }
        module->setAttr(
            "ctnative.supercompile_summary",
            at.getDictionaryAttr(
                {at.getNamedAttr("kernels", at.getI64IntegerAttr(kernels)),
                 at.getNamedAttr("contexts", at.getI64IntegerAttr(contexts)),
                 at.getNamedAttr("folds", at.getI64IntegerAttr(folds)),
                 at.getNamedAttr("generalizations", at.getI64IntegerAttr(generalizations)),
                 at.getNamedAttr("whistles", at.getI64IntegerAttr(whistles)),
                 at.getNamedAttr("steps", at.getI64IntegerAttr(steps)),
                 at.getNamedAttr("residual_ops", at.getI64IntegerAttr(operations))}));
        if (report) {
            module.emitRemark() << "supercompilation: " << kernels << " kernel(s), " << contexts
                                << " configuration(s), " << folds << " fold(s), " << whistles
                                << " whistle(s), " << generalizations << " generalization(s), "
                                << operations << " residual operation(s)";
        }
    }
};
} // namespace
} // namespace ctcompile::ctnative
