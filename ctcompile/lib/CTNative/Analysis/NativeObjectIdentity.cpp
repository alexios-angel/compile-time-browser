#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "ClosedValueFlow.h"
#include "NativeObject/Fields.h"
#include "NativeObject/ValueFlow.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"

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

bool erasedCapture(mlir::OpOperand & use) {
    auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner());
    if (!cell || use.getOperandNumber() != 0 || !cell->hasAttr("ctnative.unboxed")) {
        return false;
    }
    return llvm::all_of(cell.getResult().getUses(), [](mlir::OpOperand & capture) {
        return llvm::isa<ctjs::CreateClosureOp>(capture.getOwner()) &&
               capture.getOperandNumber() >= 2 && capture.getOwner()->hasAttr("ctnative.lifted");
    });
}

} // namespace

void prepareNativeObjectIdentities(mlir::ModuleOp module) {
    // Derived from uses, never trusted from a prior lowering or input IR.
    module.walk([](mlir::Operation * op) {
        op->removeAttr(kNativeObjectIdentity);
        op->removeAttr(kNativeObjectFieldGroup);
    });
    const bool fieldsSafe = object_detail::scalarFieldEnvironment(module);
    int64_t nextFieldGroup = 0;
    closedValueFlow flow;
    flow.build(module);
    // A returned closure owns its captures. Connect slot extractions to the
    // original object schema, as for Maps; each factory call still allocates
    // a fresh identity and environment. These tags were rederived by the
    // native closure lifter immediately before this analysis.
    llvm::StringMap<ctjs::CreateClosureOp> environments;
    llvm::DenseSet<mlir::Operation *> reads;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (!environmentTarget(made).empty()) { environments[environmentTarget(made)] = made; }
    });
    module.walk([&](ctjs::LoadUpvalueOp read) {
        if (!read->hasAttr(kNativeEnvironmentRead)) { return; }
        auto made = environments.lookup(environmentTarget(read));
        if (made && read.getIndex() >= 0 &&
            static_cast<size_t>(read.getIndex()) < made.getUpvalues().size()) {
            flow.join(read.getResult(), made.getUpvalues()[static_cast<size_t>(read.getIndex())]);
            reads.insert(read);
        }
    });
    module.walk([&](ctjs::CreateObjectOp made) { flow.add(made.getResult()); });
    object_detail::connectValues(flow, module);
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> families;
    for (mlir::Value value : flow.nodes) { families[flow.find(value)].push_back(value); }
    for (auto & [root, family] : families) {
        bool usedAsKey = false, usedAsPayload = false;
        llvm::SmallVector<ctjs::CreateObjectOp> made;
        for (mlir::Value value : family) {
            if (auto object = value.getDefiningOp<ctjs::CreateObjectOp>()) {
                made.push_back(object);
            }
            for (mlir::OpOperand & use : value.getUses()) {
                usedAsKey |= mapKeyUse(use);
                usedAsPayload |= object_detail::mapPayloadUse(use);
            }
        }
        if ((!usedAsKey && !usedAsPayload) || made.empty()) { continue; }
        std::string reason;
        llvm::DenseSet<mlir::Operation *> fields;
        const auto reject = [&](llvm::StringRef why) {
            if (reason.empty()) {
                reason = ((usedAsKey ? "identity-only Map key " : "identity-only Map value ") + why)
                             .str();
            }
        };
        for (mlir::Value value : family) {
            if (value.getDefiningOp<ctjs::CreateObjectOp>()) {
                // Fresh allocation, even when another allocation has the same schema.
            } else if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(value)) {
                auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
                if (llvm::isa<mlir::scf::WhileOp, mlir::scf::ForOp>(
                        arg.getOwner()->getParentOp())) {
                    // Every initial/backedge value was connected above.
                } else if (!closedValueFlow::closed(fn) ||
                           arg.getOwner() != &fn.getBody().front() || arg.getArgNumber() < 3 ||
                           flow.callers[fn].empty()) {
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
            } else if (reads.contains(value.getDefiningOp())) {
                // Connected to a proved owning slot above.
            } else if (nativeMapAction(value.getDefiningOp()) == "get" ||
                       llvm::isa_and_nonnull<mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::ForOp>(
                           value.getDefiningOp())) {
                // Producers and all alternative values share this checked family.
            } else if (usedAsPayload && object_detail::primitiveProducer(value)) {
                // The type lattice retains these alternatives; only object
                // allocations receive the identity proof, never the whole family.
            } else {
                reject(("flow contains a non-object producer `" +
                        value.getDefiningOp()->getName().getStringRef() + "`")
                           .str());
            }
            for (mlir::OpOperand & use : value.getUses()) {
                if (mapKeyUse(use)) { continue; }
                if (fieldsSafe && object_detail::scalarFieldUse(use)) {
                    fields.insert(use.getOwner());
                    continue;
                }
                if (object_detail::mapPayloadUse(use) ||
                    object_detail::valueObservation(use.getOwner())) {
                    continue;
                }
                if (erasedCapture(use)) { continue; }
                if (llvm::isa<ctjs::CreateClosureOp>(use.getOwner()) &&
                    use.getOperandNumber() >= 2 && !environmentTarget(use.getOwner()).empty()) {
                    continue;
                }
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
        if (reason.empty() && !fields.empty()) {
            const auto group = mlir::IntegerAttr::get(
                mlir::IntegerType::get(module.getContext(), 64), nextFieldGroup++);
            for (mlir::Operation * field : fields) {
                field->setAttr(kNativeObjectFieldGroup, group);
            }
        }
        for (ctjs::CreateObjectOp object : made) {
            if (reason.empty()) {
                object->removeAttr("ctnative.object_reason");
                object->setAttr(kNativeObjectIdentity, mlir::UnitAttr::get(module.getContext()));
            } else {
                object->setAttr("ctnative.object_reason",
                                mlir::StringAttr::get(module.getContext(), reason));
            }
        }
    }
}

} // namespace ctcompile::ctnative
