#include "Strategies.h"

#include "../Lowering/EmitC/NativeMapHelpers.h"
#include "RuntimeHelpers.h"
#include "ctcompile/CTNative/Transforms/Passes.h"

#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVEDEFOREST
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {

using namespace deforestation_detail;

struct CTNativeDeforestPass : impl::CTNativeDeforestBase<CTNativeDeforestPass> {
    using Base::Base;

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        llvm::SmallVector<ec::CallOpaqueOp> snapshots;
        module.walk([&](mlir::Operation * op) {
            op->removeAttr("ctnative.deforest_reason");
            op->removeAttr("ctnative.deforested");
            if (auto call = llvm::dyn_cast<ec::CallOpaqueOp>(op); call && isSnapshot(call)) {
                snapshots.push_back(call);
            }
        });
        // Reserved helper names alone are not a proof. Require the exact
        // runtime contract emitted after native Map admission; an arbitrary
        // opaque call with a familiar name does not license this rewrite.
        ec::VerbatimOp runtime;
        bool helpersPresent = false;
        bool orderedStorage = false;
        bool snapshotHelpers = false;
        for (ec::VerbatimOp text : module.getOps<ec::VerbatimOp>()) {
            if (text.getFmtArgs().empty() && text.getValue() == kNativeMapHelpers) {
                runtime = text;
            }
            orderedStorage |=
                text.getFmtArgs().empty() && text.getValue() == kNativeOrderedMapStorage;
            snapshotHelpers |=
                text.getFmtArgs().empty() && text.getValue() == kNativeMapSnapshotHelpers;
            helpersPresent |= text.getValue() == kProjectionHelpers;
        }
        if (!orderedStorage || !snapshotHelpers) { runtime = {}; }
        llvm::SmallVector<strategy> strategies;
        unsigned inspected = 0;
        mlir::OpBuilder builder(module.getContext());
        for (ec::CallOpaqueOp producer : snapshots) {
            std::string reason;
            std::optional<strategy> chosen;
            if (inspected++ >= maxSnapshots) {
                reason = "snapshot candidate budget exhausted";
            } else {
                chosen = inferStrategy(producer, maxScan, reason);
                if (chosen && !runtime) {
                    chosen.reset();
                    reason = "native Map runtime contract is not present";
                }
            }
            if (chosen) {
                strategies.push_back(std::move(*chosen));
            } else {
                producer->setAttr("ctnative.deforest_reason", builder.getStringAttr(reason));
            }
        }
        unsigned projections = 0;
        for (strategy & chosen : strategies) {
            builder.setInsertionPoint(chosen.consumer);
            llvm::SmallVector<mlir::Value> args{chosen.producer.getOperand(0)};
            const bool index = chosen.kind == consumption::index;
            if (index) { args.push_back(chosen.consumer.getOperand(1)); }
            const llvm::StringRef helper = !index        ? "ctnative::map_size"
                                           : chosen.keys ? "ctnative::map_snapshot_at<true>"
                                                         : "ctnative::map_snapshot_at<false>";
            auto replacement = ec::CallOpaqueOp::create(builder, chosen.consumer.getLoc(),
                                                        chosen.consumer.getResultTypes(),
                                                        builder.getStringAttr(helper), args);
            replacement->setAttr("ctnative.deforested",
                                 builder.getStringAttr(index ? "index" : "length"));
            chosen.consumer.getResult(0).replaceAllUsesWith(replacement.getResult(0));
            chosen.consumer.erase();
            // The graph is closed and every slot has one producer bound.
            // Erase forwarding users before their definitions, then the root.
            llvm::sort(chosen.forwarding, [](mlir::Operation * left, mlir::Operation * right) {
                return right->isBeforeInBlock(left);
            });
            for (mlir::Operation * op : chosen.forwarding) { op->erase(); }
            chosen.producer.erase();
            if (index) { ++projections; }
        }
        if (projections && !helpersPresent) {
            builder.setInsertionPointAfter(runtime);
            ec::VerbatimOp::create(builder, module.getLoc(),
                                   builder.getStringAttr(kProjectionHelpers));
        }
        if (report) {
            module.emitRemark() << "deforestation: " << strategies.size()
                                << " snapshot site(s) eliminated, "
                                << snapshots.size() - strategies.size()
                                << " retained with identity strategy";
        }
    }
};

} // namespace
} // namespace ctcompile::ctnative
