#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "ClosedValueFlow.h"
#include "NativeObject/Fields.h"
#include "NativeObject/ValueFlow.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Dominance.h"
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

bool strictComparison(mlir::Operation * op) {
    auto compare = llvm::dyn_cast<ctjs::CompareOp>(op);
    return compare && compare.getKind() == ctjs::CompareKind::StrictEq;
}

bool comparisonObservation(mlir::OpOperand & use) {
    auto * op = use.getOwner();
    if (strictComparison(op)) { return true; }
    if (llvm::isa<ctjs::RootOp>(op)) { return use.getOperandNumber() == 1; }
    // Accept only the exact structural edges connectValues added. A yield
    // from an arbitrary region cannot silently publish an untracked alias.
    if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(op)) {
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp())) {
            return yield.getNumOperands() == branch.getNumResults();
        }
        if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(yield->getParentOp())) {
            return yield->getParentRegion() == &loop.getAfter() &&
                   yield.getNumOperands() == loop.getBeforeArguments().size();
        }
        if (auto loop = llvm::dyn_cast<mlir::scf::ForOp>(yield->getParentOp())) {
            return yield.getNumOperands() == loop.getNumResults();
        }
        return false;
    }
    if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(op)) {
        auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(condition->getParentOp());
        return loop && use.getOperandNumber() > 0 &&
               condition.getArgs().size() == loop.getAfterArguments().size() &&
               condition.getArgs().size() == loop.getNumResults();
    }
    if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(op)) {
        return loop.getInits().size() == loop.getBeforeArguments().size();
    }
    if (auto loop = llvm::dyn_cast<mlir::scf::ForOp>(op)) {
        return use.getOperandNumber() >= 3 &&
               loop.getInitArgs().size() == loop.getRegionIterArgs().size() &&
               loop.getInitArgs().size() == loop.getNumResults();
    }
    return false;
}

llvm::StringRef stringKey(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    auto text =
        constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue()) : ctjs::StringAttr{};
    return text ? text.getValue() : llvm::StringRef{};
}

bool liveMapCall(ctjs::CallOp call) {
    auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    const auto action = nativeMapAction(call);
    if (!get || get.getObject() != call.getReceiver() || nativeMapGroup(call.getReceiver()) < 0 ||
        stringKey(get.getKey()) != action) {
        return false;
    }
    if (action == "set") { return call.getArgs().size() == 2; }
    if (action == "get" || action == "has" || action == "delete") {
        return call.getArgs().size() == 1;
    }
    return action == "clear" && call.getArgs().empty();
}

// Unlike the original Map families, a comparison family need not have any
// upstream Map proof. Its scalar fields therefore need their own closed
// property environment. Only live source edges (and already checked standard
// Map recognition) authorize effects here, never object/host report tags.
template <typename OrdinaryObject>
bool comparisonFieldEnvironment(mlir::ModuleOp module, const OwnedGlobalRoots * globals,
                                OrdinaryObject ordinaryObject) {
    mlir::DominanceInfo dominance;
    const auto result = module.walk([&](mlir::Operation * op) {
        if (llvm::any_of(op->getOperands(), [&](mlir::Value operand) {
                return !dominance.properlyDominates(operand, op);
            })) {
            return mlir::WalkResult::interrupt();
        }
        bool safe = false;
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
            safe = liveMapCall(call);
        } else if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            safe = exactCall(call, closedValueFlow::target(call));
        } else if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
            auto load = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
            safe = made->hasAttr(kNativeMapSite) && made.getArgs().empty() &&
                   made.getNewTarget() == made.getCallee() && load && load.getName() == "Map" &&
                   load->hasAttr(kNativeMapConstructor);
        } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            safe = (globals && globals->proved() && globals->lookup(load)) ||
                   (load.getName() == "Map" && load->hasAttr(kNativeMapConstructor));
            if (!safe) {
                safe = llvm::all_of(load.getResult().getUses(), [](mlir::OpOperand & use) {
                    return llvm::isa<ctjs::CallDirectOp>(use.getOwner()) &&
                           use.getOperandNumber() == 2;
                });
            }
        } else if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            safe = ordinaryObject(get.getObject()) ||
                   (globals && globals->proved() &&
                    (globals->lookup(get) || globals->lookup(get.getObject().getDefiningOp())));
            if (!safe && nativeMapGroup(get.getObject()) >= 0) {
                const auto key = stringKey(get.getKey());
                safe = (key == "size" && nativeMapAction(get) == "size") ||
                       (get->hasAttr(kNativeMapMethod) && !get.getResult().use_empty() &&
                        llvm::all_of(get.getResult().getUses(), [](mlir::OpOperand & use) {
                            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                            return call && use.getOperandNumber() == 0 && liveMapCall(call);
                        }));
            }
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
            safe = ordinaryObject(set.getObject()) ||
                   (globals && globals->proved() && globals->lookup(set));
        } else if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) {
            safe = store.getName() != "Map" && store.getName() != "Object";
        } else if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
            safe = unary.getKind() == ctjs::UnaryKind::Not ||
                   unary.getKind() == ctjs::UnaryKind::TypeOf ||
                   unary.getKind() == ctjs::UnaryKind::Void;
        } else {
            safe = strictComparison(op) ||
                   llvm::isa<mlir::ModuleOp, ctjs::FuncOp, ctjs::ConstantOp, ctjs::CreateObjectOp,
                             ctjs::CreateClosureOp, ctjs::LoadUpvalueOp, ctjs::FrameEnterOp,
                             ctjs::FrameExitOp, ctjs::RootOp, ctjs::TruthyOp, ctjs::FromBoolOp,
                             ctjs::ReturnOp, mlir::scf::IfOp, mlir::scf::YieldOp,
                             mlir::arith::ConstantOp, mlir::arith::TruncIOp>(op);
        }
        return safe ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
    });
    return !result.wasInterrupted();
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

void prepareNativeObjectIdentities(mlir::ModuleOp module, const OwnedGlobalRoots * globals) {
    // Derived from uses, never trusted from a prior lowering or input IR.
    module.walk([](mlir::Operation * op) {
        op->removeAttr(kNativeObjectIdentity);
        op->removeAttr(kNativeObjectFieldGroup);
        op->removeAttr("ctnative.object_reason");
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
    // Property access elsewhere in the module must not enter an unknown
    // object/getter. Schema connectivity proves only possible producers here;
    // it never says that two allocations are the same runtime instance.
    const auto ordinaryObject = [&](mlir::Value value) {
        const auto family = families.find(flow.find(value));
        if (family == families.end()) { return false; }
        bool fresh = false;
        for (mlir::Value member : family->second) {
            if (member.getDefiningOp<ctjs::CreateObjectOp>()) {
                fresh = true;
            } else if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(member)) {
                auto fn = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
                if (!closedValueFlow::closed(fn) || argument.getArgNumber() < 3 ||
                    flow.callers[fn].empty()) {
                    return false;
                }
            } else if (auto call = member.getDefiningOp<ctjs::CallDirectOp>()) {
                auto fn = closedValueFlow::target(call);
                if (!exactCall(call, fn) || flow.returns[fn].empty()) { return false; }
            } else if (auto call = member.getDefiningOp<ctjs::CallOp>()) {
                if (nativeMapAction(call) != "get" || !liveMapCall(call)) { return false; }
            } else if (!reads.contains(member.getDefiningOp()) &&
                       !llvm::isa_and_nonnull<mlir::scf::IfOp>(member.getDefiningOp())) {
                return false;
            }
        }
        return fresh;
    };
    const bool comparisonFieldsSafe =
        fieldsSafe && comparisonFieldEnvironment(module, globals, ordinaryObject);
    mlir::DominanceInfo dominance;
    for (auto & [root, family] : families) {
        bool usedAsKey = false, usedAsPayload = false, compared = false;
        llvm::SmallVector<ctjs::CreateObjectOp> made;
        for (mlir::Value value : family) {
            if (auto object = value.getDefiningOp<ctjs::CreateObjectOp>()) {
                made.push_back(object);
            }
            for (mlir::OpOperand & use : value.getUses()) {
                usedAsKey |= mapKeyUse(use);
                usedAsPayload |= object_detail::mapPayloadUse(use);
                compared |= strictComparison(use.getOwner());
            }
        }
        const bool comparisonOnly = !usedAsKey && !usedAsPayload;
        if ((comparisonOnly && !compared) || made.empty()) { continue; }
        std::string reason;
        llvm::DenseSet<mlir::Operation *> fields;
        const auto reject = [&](llvm::StringRef why) {
            if (reason.empty()) {
                reason = ((comparisonOnly ? "strict-comparison object "
                           : usedAsKey    ? "identity-only Map key "
                                          : "identity-only Map value ") +
                          why)
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
                if (comparisonOnly &&
                    llvm::any_of(use.getOwner()->getOperands(), [&](mlir::Value operand) {
                        return !dominance.properlyDominates(operand, use.getOwner());
                    })) {
                    reject("has an operand outside its live source scope");
                    continue;
                }
                if (mapKeyUse(use)) { continue; }
                if ((comparisonOnly ? comparisonFieldsSafe : fieldsSafe) &&
                    object_detail::scalarFieldUse(use)) {
                    fields.insert(use.getOwner());
                    continue;
                }
                if (object_detail::mapPayloadUse(use) ||
                    (comparisonOnly ? comparisonObservation(use)
                                    : object_detail::valueObservation(use.getOwner()))) {
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
