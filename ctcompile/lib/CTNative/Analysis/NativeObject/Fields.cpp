//===- Fields.cpp - Scalar object field checks ------------------*- C++ -*-===//
//
// Checks that property reads and writes on plain objects cannot run user code
// (no getters, setters or prototype changes), and answers whether a given
// field read definitely sees a field its function initialised earlier
// (queryNativeObjectFieldPresence).
//
//===----------------------------------------------------------------------===//
#include "Fields.h"
#include "../OwnedGlobalRoots.h"
#include "../PrimitiveMapKey.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSwitch.h"

namespace ctcompile::ctnative {

int64_t nativeObjectFieldGroup(mlir::Operation * op) {
    auto group = op->getAttrOfType<mlir::IntegerAttr>(kNativeObjectFieldGroup);
    return group ? group.getInt() : -1;
}

namespace object_detail {
/// Whether `op` cannot reach a getter, setter or prototype change: it is not a
/// prototype, accessor, deletion, key-enumeration, iteration, suspension or
/// throw operation, and any property key it uses is an ordinary one.
bool scalarFieldOperation(mlir::Operation * op) {
    // Native Map recognition already excludes unknown calls, constructors,
    // globals and module imports. Also reject the direct prototype and
    // accessor routes, including writes to an unrelated object's __proto__.
    if (llvm::isa<ctjs::GetProtoOp, ctjs::SetProtoOp, ctjs::DefineAccessorOp, ctjs::DeleteNamedOp,
                  ctjs::DeletePropertyOp, ctjs::LoadHomeOp, ctjs::OwnKeysOp, ctjs::IterableOp,
                  ctjs::WrapPromiseOp, ctjs::SuspendOp, ctjs::ThrowOp, ctjs::ResumeThrowOp>(op)) {
        return false;
    }
    const mlir::Value key = ctjs::propertyKey(op);
    return !key || ctjs::ordinaryKey(key);
}

bool scalarFieldEnvironment(mlir::ModuleOp module, const OwnedGlobalRoots * globals) {
    llvm::DenseSet<mlir::Operation *> snapshotReads;
    if (globals && globals->proved()) {
        for (const auto & root : globals->roots()) {
            if (!root.capturedMap()) { continue; }
            for (auto * op : root.capturedMap()->snapshotOperations) {
                if (llvm::isa<ctjs::GetPropertyOp>(op)) { snapshotReads.insert(op); }
            }
        }
    }
    bool safe = true;
    // Reads of a captured Map snapshot are allowed only when OwnedGlobalRoots
    // has proved ownership of the global. Every other operation must pass
    // scalarFieldOperation's prototype/accessor check.
    module.walk([&](mlir::Operation * op) {
        safe &= snapshotReads.contains(op) || scalarFieldOperation(op);
    });
    return safe;
}

bool scalarFieldUse(mlir::OpOperand & use) {
    const mlir::Value key = ctjs::propertyKey(use.getOwner());
    return use.getOperandNumber() == 0 && key && ctjs::ordinaryKey(key);
}

} // namespace object_detail

namespace {

/// The facts known at one program point of queryNativeObjectFieldPresence.
struct fieldState {
    /// Set after an unknown effect. Such an effect can also change the
    /// prototypes later allocations get, so once set, no later fact counts,
    /// even after the local aliases are forgotten.
    bool blocked = false;
    /// The allocation each value is known to be in this invocation. A schema
    /// may describe many allocations; only these SSA edges prove that two
    /// values are the very same object.
    llvm::DenseMap<mlir::Value, mlir::Value> origins{};
    /// (allocation, key) pairs whose field has been written.
    llvm::DenseSet<std::pair<mlir::Value, mlir::Attribute>> assigned{};
    /// An object stored in a native Map under `key`.
    struct entry {
        mlir::Value map, key, object;
    };
    llvm::SmallVector<entry> entries;

    uint64_t size() const { return origins.size() + assigned.size() + entries.size(); }
    void invalidate() {
        origins.clear();
        assigned.clear();
        entries.clear();
        blocked = true;
    }
};

/// Walks `owner` forward until it reaches `read`, then reports whether the
/// field it reads was definitely assigned on its object. Every step spends
/// from `limit`; running out reports `exhausted` and not assigned.
struct fieldPresenceQuery {
    mlir::Operation * read;
    ctjs::FuncOp owner;
    uint64_t limit;
    NativeObjectFieldPresence result;
    mlir::DominanceInfo dominance;
    bool reached = false;

    bool spend(uint64_t amount = 1) {
        if (result.exhausted) { return false; }
        if (amount > limit - result.work) {
            result.exhausted = true;
            result.assigned = false;
            return false;
        }
        result.work += amount;
        return true;
    }

    /// The literal String of an ordinary constant key, else null.
    mlir::Attribute key(mlir::Value value) const {
        return ctjs::ordinaryKey(value) ? ctjs::constantAttr<ctjs::StringAttr>(value)
                                        : mlir::Attribute{};
    }

    mlir::Value origin(mlir::Value value, mlir::Operation * at, const fieldState & state) {
        if (!spend() || !dominance.properlyDominates(value, at)) { return {}; }
        return state.origins.lookup(value);
    }

    /// Follows `set` results (which return their receiver) back to the Map.
    mlir::Value mapInstance(mlir::Value map) {
        while (auto call = map.getDefiningOp<ctjs::CallOp>()) {
            if (!spend()) { return {}; }
            if (liveNativeMapAction(call) != "set") { break; }
            map = call.getReceiver();
        }
        return map;
    }

    /// Merges two branch states into `left`, keeping only common facts.
    void intersect(fieldState & left, const fieldState & right) {
        if (!spend(left.size() + right.size())) { return; }
        if (left.blocked || right.blocked) {
            left.invalidate();
            return;
        }
        llvm::SmallVector<mlir::Value> forgotten;
        for (const auto & item : left.origins) {
            if (right.origins.lookup(item.first) != item.second) {
                forgotten.push_back(item.first);
            }
        }
        for (mlir::Value value : forgotten) { left.origins.erase(value); }
        llvm::SmallVector<std::pair<mlir::Value, mlir::Attribute>> absent;
        for (const auto & item : left.assigned) {
            if (!right.assigned.contains(item)) { absent.push_back(item); }
        }
        for (const auto & item : absent) { left.assigned.erase(item); }
        llvm::SmallVector<fieldState::entry> common;
        for (const auto & entry : left.entries) {
            for (const auto & other : right.entries) {
                if (!spend()) { return; }
                if (entry.map == other.map && entry.object == other.object &&
                    comparePrimitiveMapKeys(entry.key, other.key) ==
                        PrimitiveMapKeyRelation::Same) {
                    common.push_back(entry);
                    break;
                }
            }
        }
        left.entries = std::move(common);
    }

    void region(mlir::Region & body, fieldState & state) {
        if (body.empty()) { return; }
        if (!body.hasOneBlock()) {
            state.invalidate();
            return;
        }
        for (mlir::Operation & op : body.front()) {
            if (reached || result.exhausted) { return; }
            operation(&op, state);
        }
    }

    void operation(mlir::Operation * op, fieldState & state) {
        if (!spend()) { return; }
        for (mlir::Value operand : op->getOperands()) {
            auto * parent = operand.getParentBlock()->getParentOp();
            auto function = llvm::dyn_cast<ctjs::FuncOp>(parent);
            if (!function) { function = parent->getParentOfType<ctjs::FuncOp>(); }
            if (!spend() || function != owner || !dominance.properlyDominates(operand, op)) {
                state.invalidate();
                return;
            }
        }
        if (op == read) {
            reached = true;
            auto get = llvm::cast<ctjs::GetPropertyOp>(op);
            auto object = origin(get.getObject(), op, state);
            auto name = key(get.getKey());
            result.assigned = object && name && state.assigned.contains({object, name});
            return;
        }
        if (state.blocked) { return; }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
            if (!spend(state.size())) { return; }
            fieldState other = state;
            region(branch.getThenRegion(), state);
            if (reached || result.exhausted) { return; }
            region(branch.getElseRegion(), other);
            if (reached || result.exhausted) { return; }
            for (unsigned index = 0; index < branch.getNumResults(); ++index) {
                auto thenYield = branch.thenYield(), elseYield = branch.elseYield();
                const auto left = origin(thenYield.getOperand(index), thenYield, state);
                const auto right = origin(elseYield.getOperand(index), elseYield, other);
                if (left && left == right) {
                    state.origins[branch.getResult(index)] = left;
                    other.origins[branch.getResult(index)] = left;
                }
            }
            intersect(state, other);
            return;
        }
        // Nothing is assumed about loops or other region-holding operations:
        // their effects may also break later fresh-object initialisation.
        if (op->getNumRegions() != 0) {
            state.invalidate();
            return;
        }
        if (auto made = llvm::dyn_cast<ctjs::CreateObjectOp>(op)) {
            state.origins[made.getResult()] = made.getResult();
            return;
        }
        if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
            auto object = origin(store.getObject(), op, state);
            auto name = key(store.getKey());
            if (object && name) {
                state.assigned.insert({object, name});
            } else {
                // An unknown receiver or key can invoke an accessor, including
                // one that deletes a previously initialized local field.
                state.invalidate();
            }
            return;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
            const auto name = liveNativeMapAction(call);
            const auto map = name.empty() ? mlir::Value{} : mapInstance(call.getReceiver());
            if (!map) {
                state.invalidate();
                return;
            }
            if (name == "get") {
                for (const auto & entry : state.entries) {
                    if (!spend()) { return; }
                    if (entry.map == map && comparePrimitiveMapKeys(entry.key, call.getArgs()[0]) ==
                                                PrimitiveMapKeyRelation::Same) {
                        state.origins[call.getResult()] = entry.object;
                        break;
                    }
                }
            } else if (name != "has") {
                const auto value =
                    name == "set" ? origin(call.getArgs()[1], op, state) : mlir::Value{};
                // Without a proof that they differ, even different Map SSA
                // receivers might alias, so drop every entry whose key may be
                // equal. Earlier `get` results keep their origins.
                if (!spend(state.entries.size())) { return; }
                llvm::erase_if(state.entries, [&](const fieldState::entry & entry) {
                    return name == "clear" ||
                           comparePrimitiveMapKeys(entry.key, call.getArgs()[0]) !=
                               PrimitiveMapKeyRelation::Distinct;
                });
                if (value) { state.entries.push_back({map, call.getArgs()[0], value}); }
            }
            return;
        }
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            // An own initialised field on a fresh ordinary object cannot call
            // a getter. Neither can a recognised standard Map method or size.
            const auto object = origin(get.getObject(), op, state);
            const auto name = key(get.getKey());
            if (object && name && state.assigned.contains({object, name})) { return; }
            if (nativeMapGroup(get.getObject()) >= 0) {
                const auto text = llvm::dyn_cast_if_present<ctjs::StringAttr>(name);
                const auto method = text ? text.getValue() : llvm::StringRef{};
                if ((get->hasAttr(kNativeMapMethod) &&
                     (mapMethodArity(method) || method == "keys" || method == "values")) ||
                    (method == "size" && nativeMapAction(get) == "size")) {
                    return;
                }
            }
            state.invalidate();
            return;
        }
        if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op);
            made && made->hasAttr(kNativeMapSite) && plainMapConstruction(made) &&
            made.getCallee().getDefiningOp()->hasAttr(kNativeMapConstructor)) {
            return;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            if (load.getName() == "Map" && load->hasAttr(kNativeMapConstructor)) { return; }
        }
        // Frame and root bookkeeping from the importer cannot run JavaScript
        // property code; native lowering erases it.
        if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, mlir::arith::ConstantOp>(
                op)) {
            return;
        }
        if (ctjs::nonCoercing(op) ||
            llvm::isa<ctjs::ConstantOp, ctjs::LoadUpvalueOp, ctjs::CreateClosureOp,
                      ctjs::FromBoolOp, ctjs::ReturnOp, mlir::scf::YieldOp>(op)) {
            return;
        }
        state.invalidate();
    }
};

} // namespace

NativeObjectFieldPresence queryNativeObjectFieldPresence(mlir::Operation * read, uint64_t maxWork) {
    auto get = llvm::dyn_cast_or_null<ctjs::GetPropertyOp>(read);
    auto owner = get ? get->getParentOfType<ctjs::FuncOp>() : ctjs::FuncOp{};
    if (!owner) { return {}; }
    fieldPresenceQuery query{read, owner, maxWork, {}, {}, false};
    auto module = owner->getParentOfType<mlir::ModuleOp>();
    if (!module) { return {}; }
    // Recheck that no operation in the module can reach an accessor, without
    // trusting field attributes from earlier runs. This spends from the same
    // budget.
    auto checked = module.walk([&](mlir::Operation * op) {
        return query.spend() && object_detail::scalarFieldOperation(op)
                   ? mlir::WalkResult::advance()
                   : mlir::WalkResult::interrupt();
    });
    if (checked.wasInterrupted()) { return query.result; }
    fieldState initial;
    query.region(owner.getBody(), initial);
    if (query.result.exhausted) { query.result.assigned = false; }
    return query.result;
}

} // namespace ctcompile::ctnative
