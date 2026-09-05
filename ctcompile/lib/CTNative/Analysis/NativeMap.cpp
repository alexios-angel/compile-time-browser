//===- NativeMap.cpp - standard identity and instance-use proofs ---------===//
#include "ctcompile/CTNative/Analysis/NativeMap.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

#include <string>

namespace ctcompile::ctnative {

llvm::StringRef nativeMapAction(mlir::Operation * op) {
    if (op == nullptr) { return {}; }
    auto action = op->getAttrOfType<mlir::StringAttr>(kNativeMapAction);
    return action ? action.getValue() : llvm::StringRef{};
}

ctjs::ConstructOp nativeMapRoot(mlir::Value value) {
    while (auto call = value.getDefiningOp<ctjs::CallOp>()) {
        if (nativeMapAction(call) != "set") { return {}; }
        value = call.getReceiver();
    }
    auto made = value.getDefiningOp<ctjs::ConstructOp>();
    return made && made->hasAttr(kNativeMapSite) ? made : ctjs::ConstructOp{};
}

bool isNativeMapBookkeeping(mlir::Operation * op) {
    return op != nullptr && (op->hasAttr(kNativeMapConstructor) || op->hasAttr(kNativeMapMethod));
}

namespace {

llvm::StringRef keyOf(mlir::Value key) {
    auto constant = key.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return {}; }
    auto text = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
    return text ? text.getValue() : llvm::StringRef{};
}

struct plan {
    ctjs::ConstructOp made;
    llvm::SmallVector<ctjs::GetPropertyOp> methods;
    llvm::SmallVector<ctjs::GetPropertyOp> sizes;
    llvm::SmallVector<ctjs::CallOp> calls;
};

// A method value may only be called on the exact instance from which it was
// loaded. Detached methods, .call/.apply, property writes, escaping instances
// and real loop-carried aliases need additional proofs and are refused.
std::string collect(plan & out) {
    if (!out.made.getArgs().empty()) { return "native Map requires an empty constructor"; }
    if (out.made.getNewTarget() != out.made.getCallee()) {
        return "native Map requires its standard constructor as new.target";
    }
    llvm::SmallVector<mlir::Value> work{out.made.getResult()};
    llvm::DenseSet<mlir::Value> seen;
    for (size_t i = 0; i < work.size(); ++i) {
        const mlir::Value object = work[i];
        if (!seen.insert(object).second) { continue; }
        for (mlir::OpOperand & use : object.getUses()) {
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                call && use.getOperandNumber() == 1) {
                auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (get && get.getObject() == object) { continue; }
            }
            auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
            if (!get || use.getOperandNumber() != 0) {
                return ("native Map instance escapes or is mutated through `" +
                        use.getOwner()->getName().getStringRef() + "`")
                    .str();
            }
            const llvm::StringRef key = keyOf(get.getKey());
            if (key == "size") {
                out.sizes.push_back(get);
                continue;
            }
            const unsigned arity = key == "set"                                         ? 2U
                                   : key == "get" || key == "has" || key == "delete"    ? 1U
                                   : key == "clear" || key == "keys" || key == "values" ? 0U
                                                                                        : 99U;
            if (arity == 99U) {
                return "native Map property is not a supported constant method or size";
            }
            out.methods.push_back(get);
            for (mlir::OpOperand & methodUse : get.getResult().getUses()) {
                auto call = llvm::dyn_cast<ctjs::CallOp>(methodUse.getOwner());
                if (!call || methodUse.getOperandNumber() != 0 || call.getReceiver() != object) {
                    return "native Map method is detached, escapes, or has a different receiver";
                }
                if (call.getArgs().size() != arity) {
                    return "native Map method requires its exact argument count";
                }
                out.calls.push_back(call);
                if (key == "set") { work.push_back(call.getResult()); }
            }
        }
    }
    return {};
}

} // namespace

void prepareNativeMaps(mlir::ModuleOp module) {
    // Proof annotations are derived, not trusted input, including on reruns.
    module.walk([&](mlir::Operation * op) {
        for (llvm::StringRef name : {kNativeMapSite, kNativeMapAction, kNativeMapMethod,
                                     kNativeMapConstructor, kNativeMapReason}) {
            op->removeAttr(name);
        }
    });
    llvm::SmallVector<plan> plans;
    llvm::SmallVector<ctjs::LoadGlobalOp> constructors;
    module.walk([&](ctjs::LoadGlobalOp load) {
        if (load.getName() == "Map") { constructors.push_back(load); }
    });
    if (constructors.empty()) { return; }

    std::string reason;
    llvm::DenseSet<mlir::Operation *> sites;
    for (ctjs::LoadGlobalOp load : constructors) {
        for (mlir::OpOperand & use : load.getResult().getUses()) {
            auto made = llvm::dyn_cast<ctjs::ConstructOp>(use.getOwner());
            if (!made || use.getOperandNumber() > 1 || made.getCallee() != load.getResult()) {
                if (reason.empty()) {
                    reason = "standard Map constructor identity escapes or is inspected";
                }
                continue;
            }
            if (sites.insert(made).second) {
                plan candidate;
                candidate.made = made;
                const std::string problem = collect(candidate);
                if (reason.empty()) { reason = problem; }
                plans.push_back(std::move(candidate));
            }
        }
    }
    llvm::DenseSet<mlir::Operation *> calls;
    for (const plan & candidate : plans) {
        for (ctjs::CallOp call : candidate.calls) { calls.insert(call); }
    }
    // Standard builtins are installed by the reference/native program's
    // initial environment. A spelling alone is insufficient: explicit stores,
    // global-object/reflection access and unknown invocations can replace the
    // constructor or its prototype. Refuse module-wide in this first slice.
    module.walk([&](mlir::Operation * op) {
        if (!reason.empty()) { return; }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op);
            store && store.getName() == "Map") {
            reason = "standard Map binding is assigned in this program";
        } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
                   load && load.getName() != "Map") {
            for (mlir::OpOperand & use : load.getResult().getUses()) {
                if (!llvm::isa<ctjs::CallDirectOp>(use.getOwner()) || use.getOperandNumber() != 2) {
                    reason = "standard Map identity is unproved with other host/global value reads";
                    break;
                }
            }
        } else if (llvm::isa<ctjs::CallOp>(op) && !calls.contains(op)) {
            reason = "standard Map identity is unproved across an unknown call";
        } else if (llvm::isa<ctjs::ConstructOp>(op) && !sites.contains(op)) {
            reason = "standard Map identity is unproved across an unknown constructor";
        } else if (llvm::isa<ctjs::PassNewTargetOp, ctjs::DeleteNamedOp, ctjs::CallSpreadOp,
                             ctjs::ConstructSpreadOp, ctjs::DynamicImportOp>(op) ||
                   op->getName().getStringRef().starts_with("ctjs.module_")) {
            reason = "standard Map identity is unproved across dynamic invocation or deletion";
        }
    });
    auto * context = module.getContext();
    if (!reason.empty()) {
        for (ctjs::LoadGlobalOp load : constructors) {
            load->setAttr(kNativeMapReason, mlir::StringAttr::get(context, reason));
        }
        for (const plan & candidate : plans) {
            candidate.made->setAttr(kNativeMapReason, mlir::StringAttr::get(context, reason));
        }
        return;
    }
    for (ctjs::LoadGlobalOp load : constructors) {
        load->setAttr(kNativeMapConstructor, mlir::UnitAttr::get(context));
    }
    for (const plan & candidate : plans) {
        candidate.made->setAttr(kNativeMapSite, mlir::UnitAttr::get(context));
        for (ctjs::GetPropertyOp get : candidate.methods) {
            get->setAttr(kNativeMapMethod, mlir::UnitAttr::get(context));
        }
        for (ctjs::GetPropertyOp size : candidate.sizes) {
            size->setAttr(kNativeMapAction, mlir::StringAttr::get(context, "size"));
        }
        for (ctjs::CallOp call : candidate.calls) {
            auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            call->setAttr(kNativeMapAction, mlir::StringAttr::get(context, keyOf(get.getKey())));
        }
    }
}

} // namespace ctcompile::ctnative
