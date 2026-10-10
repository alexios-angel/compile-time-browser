//===- NativeObjectIdentity.cpp - Object identity proof ---------*- C++ -*-===//
//
// Finds plain objects (`{...}` literals) used only for their identity: as
// native Map keys, as native Map values, or in `===` comparisons. Such objects
// get ctnative.object_identity, and their scalar field accesses a shared
// ctnative.object_field_group, so they can be lowered natively. Any other use
// records a ctnative.object_reason instead.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "ClosedValueFlow.h"
#include "NativeObject/Fields.h"
#include "NativeObject/ValueFlow.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/DenseSet.h"

namespace ctcompile::ctnative {
namespace {

/// True when `use` is the key operand of a native Map set/get/has/delete.
bool mapKeyUse(mlir::OpOperand & use) {
    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    if (!call || use.getOperandNumber() != 2) { return false; }
    return nativeMapKeyAction(nativeMapAction(call));
}

bool strictComparison(mlir::Operation * op) {
    auto compare = llvm::dyn_cast<ctjs::CompareOp>(op);
    return compare && compare.getKind() == ctjs::CompareKind::StrictEq;
}

/// True when `use` is a `===` comparison, a root, or a value-carrying edge of
/// scf.if / scf.while / scf.for that connectValues followed.
bool comparisonObservation(mlir::OpOperand & use) {
    auto * op = use.getOwner();
    if (strictComparison(op)) { return true; }
    if (llvm::isa<ctjs::RootOp>(op)) { return use.getOperandNumber() == 1; }
    // Accept only the exact edges connectValues added, so a yield from some
    // other region cannot pass on an alias nobody tracked.
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

/// Whether every operation in `module` is one that cannot reach a getter,
/// setter or unknown object, for objects used only in `===` comparisons.
///
/// Such objects (not Map keys or values) have no Map analysis behind them, so
/// their fields need this check of their own. Only operations whose effect is
/// proved here (or by the standard Map recognition) count, never attributes
/// left by other analyses.
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
            safe = !liveNativeMapAction(call).empty();
        } else if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            safe = closedValueFlow::exactCall(call, call.getTarget());
        } else if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
            safe = made->hasAttr(kNativeMapSite) && plainMapConstruction(made) &&
                   made.getCallee().getDefiningOp()->hasAttr(kNativeMapConstructor);
        } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            safe = (globals && globals->proved() && globals->lookup(load)) ||
                   (load.getName() == "Map" && load->hasAttr(kNativeMapConstructor)) ||
                   calleeSlotOnly(load);
        } else if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            safe = ordinaryObject(get.getObject()) ||
                   (globals && globals->proved() &&
                    (globals->lookup(get) || globals->lookup(get.getObject().getDefiningOp())));
            if (!safe && nativeMapGroup(get.getObject()) >= 0) {
                const auto key = ctjs::constantKey(get.getKey());
                safe = (key == "size" && nativeMapAction(get) == "size") ||
                       (get->hasAttr(kNativeMapMethod) && !get.getResult().use_empty() &&
                        llvm::all_of(get.getResult().getUses(), [](mlir::OpOperand & use) {
                            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                            return call && use.getOperandNumber() == 0 &&
                                   !liveNativeMapAction(call).empty();
                        }));
            }
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
            safe = ordinaryObject(set.getObject()) ||
                   (globals && globals->proved() && globals->lookup(set));
        } else if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) {
            safe = store.getName() != "Map" && store.getName() != "Object";
        } else {
            safe = ctjs::nonCoercing(op) ||
                   llvm::isa<mlir::ModuleOp, ctjs::FuncOp, ctjs::ConstantOp, ctjs::CreateObjectOp,
                             ctjs::CreateClosureOp, ctjs::LoadUpvalueOp, ctjs::FrameEnterOp,
                             ctjs::FrameExitOp, ctjs::RootOp, ctjs::FromBoolOp, ctjs::ReturnOp,
                             mlir::scf::IfOp, mlir::scf::YieldOp, mlir::arith::ConstantOp,
                             mlir::arith::TruncIOp>(op);
        }
        return safe ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
    });
    return !result.wasInterrupted();
}

} // namespace

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

void prepareNativeObjectIdentities(mlir::ModuleOp module, const OwnedGlobalRoots * globals) {
    // These attributes are derived from uses, never trusted from an earlier
    // run or from the input IR.
    module.walk([](mlir::Operation * op) {
        op->removeAttr(kNativeObjectIdentity);
        op->removeAttr(kNativeObjectFieldGroup);
        // Other analyses also write `ctnative.object_reason`. Remove only the
        // reasons this analysis wrote for comparison-only objects on an
        // earlier run (prefixed "strict-comparison object").
        if (auto reason = op->getAttrOfType<mlir::StringAttr>("ctnative.object_reason");
            reason && reason.getValue().starts_with("strict-comparison object ")) {
            op->removeAttr("ctnative.object_reason");
        }
    });
    const bool fieldsSafe = object_detail::scalarFieldEnvironment(module, globals);
    int64_t nextFieldGroup = 0;
    closedValueFlow flow;
    flow.build(module);
    // A returned closure owns its captures. Join reads of captured slots to
    // the captured object's schema, as for Maps; each factory call still
    // allocates a fresh object and environment. The environment-read
    // attributes were recomputed by the native closure lifter just before
    // this analysis runs.
    llvm::DenseSet<mlir::Operation *> reads;
    llvm::DenseMap<mlir::Operation *, mlir::Value> keyInitializations;
    if (globals && globals->proved()) {
        for (const HostObjectGlobalRead & edge : globals->objectReads()) {
            auto object = edge.object;
            auto read = edge.read;
            flow.join(object.getResult(), read.getResult());
            reads.insert(read);
            auto initialization = edge.initialization;
            keyInitializations[initialization] = initialization.getValue();
        }
    }
    flow.connectEnvironmentReads(module, &reads);
    module.walk([&](ctjs::CreateObjectOp made) { flow.add(made.getResult()); });
    object_detail::connectValues(flow, module);
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> families;
    for (mlir::Value value : flow.nodes()) { families[flow.find(value)].push_back(value); }
    // Whether every value in `value`'s family is a fresh object or flows from
    // one through a checked edge, so a property access on it cannot reach an
    // unknown object or getter. Families group possible producers; they never
    // say two allocations are the same runtime instance.
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
                if (!flow.closedResult(call)) { return false; }
            } else if (auto call = member.getDefiningOp<ctjs::CallOp>()) {
                if (liveNativeMapAction(call) != "get") { return false; }
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
        bool usedAsKey = false, usedAsPayload = false, retainedRecord = false, compared = false;
        llvm::SmallVector<ctjs::CreateObjectOp> made;
        for (mlir::Value value : family) {
            if (auto object = value.getDefiningOp<ctjs::CreateObjectOp>()) {
                made.push_back(object);
            }
            for (mlir::OpOperand & use : value.getUses()) {
                usedAsKey |= mapKeyUse(use);
                usedAsPayload |= object_detail::mapPayloadUse(use);
                retainedRecord |= isNativeMapRecordStore(use);
                compared |= strictComparison(use.getOwner());
            }
        }
        // A record Map (see NativeMap.cpp's proveRecords) already holds these
        // objects as stack-owned records; they keep that representation.
        if (retainedRecord) { continue; }
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
                // A fresh allocation, even when another one has the same schema.
            } else if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(value)) {
                if (llvm::isa<mlir::scf::WhileOp, mlir::scf::ForOp>(
                        arg.getOwner()->getParentOp())) {
                    // Every initial and back-edge value was joined above.
                } else if (!flow.closedParameter(arg)) {
                    reject("parameter requires a closed function with visible callers");
                } else if (!flow.exactCallers(arg)) {
                    reject("parameter has a missing or surplus argument");
                }
            } else if (auto call = value.getDefiningOp<ctjs::CallDirectOp>()) {
                if (!flow.closedResult(call)) {
                    reject("result requires a closed function with visible returns");
                }
            } else if (reads.contains(value.getDefiningOp())) {
                // Connected to a proved owning slot above.
            } else if (nativeMapAction(value.getDefiningOp()) == "get" ||
                       llvm::isa_and_nonnull<mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::ForOp>(
                           value.getDefiningOp())) {
                // Its possible values all belong to this checked family.
            } else if (usedAsPayload && object_detail::primitiveProducer(value)) {
                // The type lattice keeps these primitive alternatives; only the
                // object allocations get the identity attribute.
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
                if (keyInitializations.lookup(use.getOwner()) == value) { continue; }
                if (llvm::isa<ctjs::StoreGlobalOp>(use.getOwner()) && globals) {
                    const auto scalar = globals->returnedScalar(value);
                    if (scalar.known && (scalar.truthy | scalar.falsy)) { continue; }
                }
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
                if (closedValueFlow::captured(use)) { continue; }
                if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                    call && use.getOperandNumber() >= 3 &&
                    closedValueFlow::exactCall(call, call.getTarget())) {
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
