#include "Facts.h"

#include "ctcompile/CTNative/Transforms/Passes.h"
#include "mlir/IR/Builders.h"

namespace ctcompile::ctnative {
#define GEN_PASS_DEF_CTNATIVEPRECOMPUTE
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {
struct CTNativePrecomputePass : impl::CTNativePrecomputeBase<CTNativePrecomputePass> {
    using CTNativePrecomputeBase::CTNativePrecomputeBase;
    void runOnOperation() override {
        auto module = getOperation();
        module.walk([](mlir::Operation * op) {
            op->removeAttr("ctnative.symbolic_results");
            op->removeAttr("ctnative.precompute_summary");
        });
        symbolic::Budget budget{maxSteps};
        symbolic::Analysis analysis(module, budget);
        analysis.run();
        const auto changes = symbolic::rewrite(module, analysis, budget);
        mlir::Builder at(&getContext());
        module.walk([&](mlir::Operation * op) {
            if (op->getNumResults() == 0) { return; }
            llvm::SmallVector<mlir::Attribute> results;
            for (mlir::Value result : op->getResults()) {
                results.push_back(at.getStringAttr(symbolic::domainName(analysis.get(result))));
            }
            op->setAttr("ctnative.symbolic_results", at.getArrayAttr(results));
        });
        module->setAttr(
            "ctnative.precompute_summary",
            at.getDictionaryAttr(
                {at.getNamedAttr("expressions", at.getI64IntegerAttr(changes.expressions)),
                 at.getNamedAttr("branches", at.getI64IntegerAttr(changes.branches)),
                 at.getNamedAttr("steps", at.getI64IntegerAttr(budget.steps)),
                 at.getNamedAttr("budget_exhausted", at.getBoolAttr(budget.exhausted))}));
        if (report) {
            module.emitRemark() << "precomputation: " << changes.expressions << " expression(s), "
                                << changes.branches << " structured branch(es), " << budget.steps
                                << " step(s)" << (budget.exhausted ? "; budget exhausted" : "");
        }
    }
};
} // namespace
} // namespace ctcompile::ctnative
