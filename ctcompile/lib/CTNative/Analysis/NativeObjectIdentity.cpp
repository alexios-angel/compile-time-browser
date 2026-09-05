#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "ClosedValueFlow.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"

namespace ctcompile::ctnative {
namespace {

bool exactCall(ctjs::CallDirectOp call, ctjs::FuncOp fn) {
    return closedValueFlow::closed(fn) &&
           call->getNumOperands() == fn.getBody().front().getNumArguments();
}

bool mapKeyUse(mlir::OpOperand & use) {
    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    if (!call || use.getOperandNumber() != 2) { return false; }
    const auto action = nativeMapAction(call);
    return action == "set" || action == "get" || action == "has" || action == "delete";
}

} // namespace

void prepareNativeObjectIdentities(mlir::ModuleOp module) {
    // Derived from uses, never trusted from a prior lowering or input IR.
    module.walk([](mlir::Operation * op) { op->removeAttr(kNativeObjectIdentity); });
    closedValueFlow flow;
    flow.build(module);
    module.walk([&](ctjs::CreateObjectOp made) { flow.add(made.getResult()); });
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> families;
    for (mlir::Value value : flow.nodes) { families[flow.find(value)].push_back(value); }
    for (auto & [root, family] : families) {
        bool usedAsKey = false;
        llvm::SmallVector<ctjs::CreateObjectOp> made;
        for (mlir::Value value : family) {
            if (auto object = value.getDefiningOp<ctjs::CreateObjectOp>()) {
                made.push_back(object);
            }
            for (mlir::OpOperand & use : value.getUses()) { usedAsKey |= mapKeyUse(use); }
        }
        if (!usedAsKey || made.empty()) { continue; }
        std::string reason;
        const auto reject = [&](llvm::StringRef why) {
            if (reason.empty()) { reason = ("identity-only Map key " + why).str(); }
        };
        for (mlir::Value value : family) {
            if (value.getDefiningOp<ctjs::CreateObjectOp>()) {
                // Fresh allocation, even when another allocation has the same schema.
            } else if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(value)) {
                auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
                if (!closedValueFlow::closed(fn) || arg.getOwner() != &fn.getBody().front() ||
                    arg.getArgNumber() < 3 || flow.callers[fn].empty()) {
                    reject("parameter requires a closed function with visible callers");
                } else {
                    for (ctjs::CallDirectOp call : flow.callers[fn]) {
                        if (!exactCall(call, fn)) {
                            reject("parameter has a missing or surplus argument");
                        }
                    }
                }
            } else if (auto call = value.getDefiningOp<ctjs::CallDirectOp>()) {
                auto fn = closedValueFlow::target(call);
                if (!exactCall(call, fn) || flow.returns[fn].empty()) {
                    reject("result requires a closed function with visible returns");
                }
            } else {
                reject("flow contains a non-object producer");
            }
            for (mlir::OpOperand & use : value.getUses()) {
                if (mapKeyUse(use)) { continue; }
                if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                    call && use.getOperandNumber() >= 3 &&
                    exactCall(call, closedValueFlow::target(call))) {
                    continue;
                }
                if (auto ret = llvm::dyn_cast<ctjs::ReturnOp>(use.getOwner());
                    ret && closedValueFlow::closed(ret->getParentOfType<ctjs::FuncOp>())) {
                    continue;
                }
                reject(("has an unsupported use through `" +
                        use.getOwner()->getName().getStringRef() + "`")
                           .str());
            }
        }
        for (ctjs::CreateObjectOp object : made) {
            if (reason.empty()) {
                object->setAttr(kNativeObjectIdentity, mlir::UnitAttr::get(module.getContext()));
            } else {
                object->setAttr("ctnative.object_reason",
                                mlir::StringAttr::get(module.getContext(), reason));
            }
        }
    }
}

} // namespace ctcompile::ctnative
