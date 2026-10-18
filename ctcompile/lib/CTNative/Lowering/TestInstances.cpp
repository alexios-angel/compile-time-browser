//===- TestInstances.cpp - Print the instance registry proofs -------------===//
//
// The test-only pass ctnative-test-instance-registries: it writes the answer
// of proveInstanceRegistries onto the IR as attributes, so lit tests can pin
// each proof and refusal before the lowering consumes them.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifting/ClassInheritance.h"
#include "ClosureLifting/ClosureLifter.h"
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
    using CTNativeTestInstanceRegistriesBase::CTNativeTestInstanceRegistriesBase;

    void runOnOperation() override {
        if (lifted) {
            // The steps closureLifter::run() takes before its censuses, up to
            // the object-table calls and the class statics rewrite, after
            // flattening inheritance.
            lowering_detail::flattenClassInheritance(getOperation());
            lowering_detail::closureLifter lifter{getOperation()};
            lowering_detail::liftReport out;
            lifter.indexAndNewTargets();
            lifter.makeRunOnceCellsStatic(out, /*functions=*/false);
            lifter.makeRunOnceCellsStatic(out, /*functions=*/true);
            lifter.specializeCallbacks(out);
            lifter.bindLocalFunctions(out);
            lifter.bindSingletonMethods(out);
            lifter.bindClassStatics(out);
        }
        mlir::Builder builder(&getContext());
        for (const InstanceRegistry & registry : proveInstanceRegistries(getOperation())) {
            registry.constructor->setAttr(
                "ctnative.instance_registry",
                builder.getStringAttr(registry.refusal.empty() ? "proved" : registry.refusal));
            // A family's key per class, and the reads each key selects.
            if (!registry.key.empty()) {
                registry.constructor->setAttr("ctnative.instance_key",
                                              builder.getStringAttr(registry.key));
            }
            ctjs::FuncOp constructor = registry.constructor;
            for (mlir::Operation * read : registry.keyedReads) {
                read->setAttr(kNativeInstanceKeyClass,
                              builder.getStringAttr(constructor.getSymName()));
            }
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
