//===- TestInstances.cpp - Print the instance registry proofs -------------===//
//
// The test-only pass ctnative-test-instance-registries: it writes the answer
// of proveInstanceRegistries onto the IR as attributes, so lit tests can pin
// each proof and refusal before the lowering consumes them.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Analysis/Instances.h"
#include "ctcompile/CTNative/Transforms/Passes.h"

#include "mlir/IR/Builders.h"
#include "llvm/ADT/MapVector.h"

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVETESTINSTANCEREGISTRIES
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {

struct TestInstanceRegistries final
    : impl::CTNativeTestInstanceRegistriesBase<TestInstanceRegistries> {
    void runOnOperation() override {
        mlir::Builder builder(&getContext());
        for (const InstanceRegistry & registry : proveInstanceRegistries(getOperation())) {
            registry.constructor->setAttr(
                "ctnative.instance_registry",
                builder.getStringAttr(registry.refusal.empty() ? "proved" : registry.refusal));
            // The declared positions of the non-null parameters, per function.
            llvm::MapVector<mlir::Operation *, llvm::SmallVector<int32_t>> positions;
            for (mlir::Value value : registry.nonNull) {
                auto parameter = llvm::cast<mlir::BlockArgument>(value);
                positions[parameter.getOwner()->getParentOp()].push_back(
                    static_cast<int32_t>(parameter.getArgNumber() - ctjs::implicit_arguments));
            }
            for (auto & [fn, list] : positions) {
                llvm::sort(list);
                fn->setAttr("ctnative.instance_non_null", builder.getDenseI32ArrayAttr(list));
            }
        }
    }
};

} // namespace
} // namespace ctcompile::ctnative
