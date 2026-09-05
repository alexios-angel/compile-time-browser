#include "Analysis.h"

#include "ctcompile/CTNative/Transforms/Passes.h"
#include "mlir/IR/Builders.h"

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVEPRUNEUNREACHABLE
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {
struct CTNativePruneUnreachablePass
    : impl::CTNativePruneUnreachableBase<CTNativePruneUnreachablePass> {
    using CTNativePruneUnreachableBase::CTNativePruneUnreachableBase;

    void runOnOperation() override {
        auto module = getOperation();
        module->removeAttr("ctnative.reachability_summary");
        module->removeAttr("ctnative.reachability_reason");
        const auto proof = reachability::analyze(module, maxSteps);
        const auto removed = static_cast<int64_t>(proof.dead.size());
        // Commit only after the complete graph is proved. No executable
        // operation inside a surviving function is changed or reordered.
        for (ctjs::FuncOp function : proof.dead) { function.erase(); }
        mlir::Builder at(&getContext());
        module->setAttr("ctnative.reachability_summary",
                        at.getDictionaryAttr(
                            {at.getNamedAttr("removed", at.getI64IntegerAttr(removed)),
                             at.getNamedAttr("retained", at.getI64IntegerAttr(proof.retained)),
                             at.getNamedAttr("steps", at.getI64IntegerAttr(
                                                          static_cast<int64_t>(proof.steps)))}));
        if (!proof.reason.empty()) {
            module->setAttr("ctnative.reachability_reason", at.getStringAttr(proof.reason));
        }
        if (report) {
            module.emitRemark() << "reachability: " << removed << " private function(s) removed, "
                                << proof.retained << " retained"
                                << (proof.reason.empty() ? "" : "; " + proof.reason);
        }
    }
};
} // namespace

} // namespace ctcompile::ctnative
