#include "Fields.h"
#include "../PrimitiveMapKey.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringSwitch.h"

namespace ctcompile::ctnative {

int64_t nativeObjectFieldGroup(mlir::Operation * op) {
    auto group = op->getAttrOfType<mlir::IntegerAttr>(kNativeObjectFieldGroup);
    return group ? group.getInt() : -1;
}

namespace object_detail {
namespace {
bool ordinaryKey(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    auto key =
        constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue()) : ctjs::StringAttr{};
    if (!key) { return false; }
    // Missing ordinary fields are undefined only while the prototype world is
    // closed. These inherited names and the prototype setter are not fields.
    return !llvm::StringSwitch<bool>(key.getValue())
                .Cases({"__proto__", "prototype", "constructor"}, true)
                .Cases({"toString", "valueOf", "toLocaleString"}, true)
                .Cases({"hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable"}, true)
                .Cases({"__defineGetter__", "__defineSetter__"}, true)
                .Cases({"__lookupGetter__", "__lookupSetter__"}, true)
                .Default(false);
}
} // namespace

bool scalarFieldOperation(mlir::Operation * op) {
    // Native Map recognition already excludes unknown calls, constructors,
    // globals and module imports. Reject the direct prototype/accessor routes
    // too, including writes to an otherwise unrelated object's __proto__.
    if (llvm::isa<ctjs::GetProtoOp, ctjs::SetProtoOp, ctjs::DefineAccessorOp, ctjs::DeleteNamedOp,
                  ctjs::DeletePropertyOp, ctjs::LoadHomeOp, ctjs::OwnKeysOp, ctjs::IterableOp,
                  ctjs::WrapPromiseOp, ctjs::SuspendOp, ctjs::ThrowOp, ctjs::ResumeThrowOp>(op)) {
        return false;
    }
    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
        if (!ordinaryKey(get.getKey())) { return false; }
    }
    if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
        if (!ordinaryKey(set.getKey())) { return false; }
    }
    return true;
}

bool scalarFieldEnvironment(mlir::ModuleOp module) {
    bool safe = true;
    module.walk([&](mlir::Operation * op) { safe &= scalarFieldOperation(op); });
    return safe;
}

bool scalarFieldUse(mlir::OpOperand & use) {
    if (use.getOperandNumber() != 0) { return false; }
    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner())) {
        return ordinaryKey(get.getKey());
    }
    if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner())) {
        return ordinaryKey(set.getKey());
    }
    return false;
}

} // namespace object_detail

namespace {

struct fieldState {
    // Unknown effects can change the prototype environment used by later
    // allocations too. Losing local aliases must never restore that authority.
    bool blocked = false;
    // A schema may describe many allocations; only these live SSA edges say
    // that two names denote the very same allocation in this invocation.
    llvm::DenseMap<mlir::Value, mlir::Value> origins{};
    llvm::DenseSet<std::pair<mlir::Value, mlir::Attribute>> assigned{};
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

    mlir::Attribute key(mlir::Value value) const {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant && object_detail::ordinaryKey(value) ? constant.getValue()
                                                             : mlir::Attribute{};
    }

    mlir::Value origin(mlir::Value value, mlir::Operation * at, const fieldState & state) {
        if (!spend() || !dominance.properlyDominates(value, at)) { return {}; }
        return state.origins.lookup(value);
    }

    // Recognition proves the standard method, not membership or payload. Read
    // the actual callee, receiver, key and arity again so a changed source edge
    // cannot reuse a stale action or presence annotation.
    llvm::StringRef action(ctjs::CallOp call) const {
        const auto name = nativeMapAction(call);
        auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (!get || get.getObject() != call.getReceiver() ||
            nativeMapGroup(call.getReceiver()) < 0) {
            return {};
        }
        auto constant = get.getKey().getDefiningOp<ctjs::ConstantOp>();
        auto text =
            constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue()) : ctjs::StringAttr{};
        if (!text || text.getValue() != name) { return {}; }
        const unsigned arity = name == "set" ? 2 : name == "clear" ? 0 : 1;
        if (call.getArgs().size() != arity || (name != "set" && name != "get" && name != "has" &&
                                               name != "delete" && name != "clear")) {
            return {};
        }
        return name;
    }

    mlir::Value mapInstance(mlir::Value map) {
        while (auto call = map.getDefiningOp<ctjs::CallOp>()) {
            if (!spend()) { return {}; }
            if (action(call) != "set") { break; }
            map = call.getReceiver();
        }
        return map;
    }

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
            auto thenYield = branch.getThenRegion().hasOneBlock()
                                 ? llvm::dyn_cast<mlir::scf::YieldOp>(
                                       branch.getThenRegion().front().getTerminator())
                                 : mlir::scf::YieldOp{};
            auto elseYield = branch.getElseRegion().hasOneBlock()
                                 ? llvm::dyn_cast<mlir::scf::YieldOp>(
                                       branch.getElseRegion().front().getTerminator())
                                 : mlir::scf::YieldOp{};
            if (thenYield && elseYield && thenYield.getNumOperands() == branch.getNumResults() &&
                elseYield.getNumOperands() == branch.getNumResults()) {
                for (unsigned index = 0; index < branch.getNumResults(); ++index) {
                    const auto left = origin(thenYield.getOperand(index), thenYield, state);
                    const auto right = origin(elseYield.getOperand(index), elseYield, other);
                    if (left && left == right) {
                        state.origins[branch.getResult(index)] = left;
                        other.origins[branch.getResult(index)] = left;
                    }
                }
            }
            intersect(state, other);
            return;
        }
        // No loop invariant, cross-call result or foreign region is presumed.
        // Effects there may also invalidate later fresh-object initialization.
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
            const auto name = action(call);
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
                // Without a separate disjointness proof, even different Map
                // SSA receivers might alias. Invalidate every possibly equal
                // entry. Saved get aliases are independent of this table.
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
            // An own initialized field on a fresh ordinary object cannot call
            // a getter. Neither can a recognized standard Map method/size.
            const auto object = origin(get.getObject(), op, state);
            const auto name = key(get.getKey());
            if (object && name && state.assigned.contains({object, name})) { return; }
            if (nativeMapGroup(get.getObject()) >= 0) {
                const auto text = llvm::dyn_cast_if_present<ctjs::StringAttr>(name);
                if (text &&
                    ((get->hasAttr(kNativeMapMethod) &&
                      llvm::StringSwitch<bool>(text.getValue())
                          .Cases({"set", "get", "has", "delete", "clear", "keys", "values"}, true)
                          .Default(false)) ||
                     (text.getValue() == "size" && nativeMapAction(get) == "size"))) {
                    return;
                }
            }
            state.invalidate();
            return;
        }
        if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
            auto constructor = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
            if (made->hasAttr(kNativeMapSite) && made.getArgs().empty() &&
                made.getNewTarget() == made.getCallee() && constructor &&
                constructor.getName() == "Map" && constructor->hasAttr(kNativeMapConstructor)) {
                return;
            }
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            if (load.getName() == "Map" && load->hasAttr(kNativeMapConstructor)) { return; }
        }
        // Imported frame/root bookkeeping cannot invoke source property code.
        // Native lowering erases it; it is not an unknown JavaScript effect.
        if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, mlir::arith::ConstantOp>(
                op)) {
            return;
        }
        if (llvm::isa<ctjs::ConstantOp, ctjs::LoadUpvalueOp, ctjs::CreateClosureOp, ctjs::TruthyOp,
                      ctjs::FromBoolOp, ctjs::ReturnOp, mlir::scf::YieldOp>(op)) {
            return;
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
            if (unary.getKind() == ctjs::UnaryKind::Not ||
                unary.getKind() == ctjs::UnaryKind::TypeOf ||
                unary.getKind() == ctjs::UnaryKind::Void) {
                return;
            }
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op)) {
            if (compare.getKind() == ctjs::CompareKind::StrictEq) { return; }
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
    // Recheck the existing ordinary-field environment without trusting stale
    // field annotations. This is part of the same bounded query.
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
