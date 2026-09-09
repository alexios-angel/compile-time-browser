#include "Analysis.h"

#include "../Analysis/PrimitiveAlternatives.h"
#include "../Analysis/PrimitiveMapKey.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

#include <algorithm>

namespace ctcompile::ctnative::host_detail {

bool analyzer::capturedMapBody(ctjs::FuncOp function, bool prepared, bool primitiveContents,
                               const HostMethodParameters & parameters, HostCapturedMap & result,
                               PrimitiveAlternatives & returnAlternatives) {
    // This is an effects and ownership proof, not an evaluation of the first
    // invocation. The immutable slot always denotes this Map; its contents
    // may change at every call. A complete body census closes all writes over
    // primitives and fresh leaf objects. No object can own another object or
    // the Map, escape to a caller, or execute an accessor. The native identity,
    // field and Map analyses must still independently prove their carriers.
    auto & body = function.getBody().front();
    const auto firstRead = result.reads.size();
    const auto firstUpvalue = result.upvalues.size();
    llvm::DenseSet<mlir::Value> maps, primitives, flags;
    // A saved read names its allocation, not the current Map entry. These SSA
    // origins never change when that entry is overwritten or deleted.
    llvm::DenseMap<mlir::Value, mlir::Value> objects;
    llvm::DenseSet<mlir::Operation *> objectWrites, objectReads, objectStores, identities;
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> alternatives;
    struct field_fact {
        mlir::Value object;
        llvm::StringRef key;
        PrimitiveAlternatives payload;
    };
    llvm::SmallVector<field_fact> fields;
    // All checked capture loads/fluent returns denote this one runtime Map.
    // Each invocation starts with unknown contents. A set preserves presence
    // even when its key may alias an earlier entry. In that case the payload
    // retains the union of both independently proved primitive alternatives.
    // Presence alone never supplies payload evidence. A possibly aliasing delete
    // removes definite membership, but cannot change a surviving payload. Its
    // alternatives stay valid whenever present; an exact set replaces them.
    // Definite absence is independent of possible absence: only an exact
    // deletion establishes it. A possibly aliasing write invalidates absence,
    // whereas another deletion cannot make an absent key present.
    // This local contents fact is independent of the family's return worklist
    // and publishes alternatives only after the entire body/use proof completes.
    struct entry_fact {
        mlir::Value key;
        PrimitiveAlternatives payload;
        bool present = true;
        mlir::Value object;
        bool absent = false;
    };
    llvm::SmallVector<entry_fact> entries;
    llvm::DenseSet<mlir::Operation *> observations;
    llvm::DenseMap<mlir::Value, unsigned> sizeBounds;
    const auto primitiveTag = [&](mlir::Value key) -> std::optional<mlir::TypeID> {
        return alternatives.lookup(key).tag();
    };
    const auto keyEvidence = [&](mlir::Value key) {
        return PrimitiveMapKeyEvidence{primitiveTag(key), sizeBounds.lookup(key)};
    };
    const auto mutate = [&](mlir::Value key, bool erase, PrimitiveAlternatives payload = {},
                            mlir::Value object = {}) {
        bool hasExactAbsence = false;
        for (auto it = entries.begin(); it != entries.end();) {
            if (!step()) { return false; }
            const auto relation =
                comparePrimitiveMapKeys(it->key, key, keyEvidence(it->key), keyEvidence(key));
            if (relation == PrimitiveMapKeyRelation::Distinct) {
                ++it;
            } else if (erase) {
                it->present = false;
                if (relation == PrimitiveMapKeyRelation::Same) {
                    it->absent = true;
                    hasExactAbsence = true;
                }
                ++it;
            } else if (relation == PrimitiveMapKeyRelation::Same) {
                it = entries.erase(it);
            } else {
                // The old payload survives if these runtime keys differ; the
                // new one wins if they compare equal. Unknown joined with any
                // finite set remains unknown, including after further writes.
                it->payload = it->payload.joined(payload);
                if (it->object != object) { it->object = {}; }
                it->absent = false;
                ++it;
            }
        }
        if (erase) {
            for (auto * observed : llvm::make_early_inc_range(observations)) {
                if (!step()) { return false; }
                auto call = llvm::cast<ctjs::CallOp>(observed);
                auto observedKey = call.getArgs()[0];
                if (comparePrimitiveMapKeys(observedKey, key, keyEvidence(observedKey),
                                            keyEvidence(key)) !=
                    PrimitiveMapKeyRelation::Distinct) {
                    observations.erase(observed);
                }
            }
        }
        if (erase) {
            // The invocation's initial contents are unknown, but this exact
            // key is now absent even if no earlier local set mentioned it.
            // Keep earlier payload evidence whenever present: a branch join
            // may still reach that payload from its nondeleting arm.
            if (!hasExactAbsence) { entries.push_back({key, {}, false, {}, true}); }
        } else {
            entries.push_back({key, payload, true, object});
        }
        return true;
    };
    const auto learn = [&](mlir::Value condition, bool branch) {
        while (auto truthy = condition.getDefiningOp<ctjs::TruthyOp>()) {
            if (!step()) { return false; }
            condition = truthy.getValue();
        }
        if (auto found = alternatives.find(condition); found != alternatives.end()) {
            found->second = found->second.filtered(branch);
        }
        auto call = condition.getDefiningOp<ctjs::CallOp>();
        if (!branch || !call || !observations.contains(call)) { return true; }
        // Only a live, checked has on this captured Map supplies membership.
        // It never creates a payload tag or selects away the other arm.
        auto key = call.getArgs()[0];
        for (auto & entry : entries) {
            if (!step()) { return false; }
            if (comparePrimitiveMapKeys(entry.key, key) == PrimitiveMapKeyRelation::Same) {
                entry.present = true;
                entry.absent = false;
                return true;
            }
        }
        entries.push_back({key, {}, true, {}});
        return true;
    };
    llvm::DenseSet<mlir::Operation *> reads, calls, upvalues;
    if (prepared) { maps.insert(body.getArgument(3)); }
    const unsigned offset = prepared ? 4u : 3u;
    if (parameters.function != function ||
        parameters.alternatives.size() != body.getNumArguments() - offset) {
        return false;
    }
    for (mlir::BlockArgument parameter : body.getArguments().drop_front(offset)) {
        if (!step()) { return false; }
        primitives.insert(parameter);
        alternatives.try_emplace(parameter,
                                 parameters.alternatives[parameter.getArgNumber() - offset]);
    }
    ctjs::ReturnOp returned;
    // SSA scalar facts and the complete use census are immutable across paths.
    // Mutable contents are copied for each arm and intersected only after both
    // arms finish. An observed startup condition never selects a future path.
    const auto walk = [&](auto && self, mlir::Block & block, unsigned depth) -> bool {
        if (depth > 32 || !step()) { return false; }
        for (mlir::Operation & operation : block) {
            if (!step()) { return false; }
            if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
                if (!llvm::isa<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::StringAttr,
                               ctjs::NullAttr, ctjs::UndefinedAttr>(constant.getValue())) {
                    return false;
                }
                primitives.insert(constant.getResult());
                alternatives.try_emplace(constant.getResult(),
                                         PrimitiveAlternatives::literal(constant.getValue()));
            } else if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(operation)) {
                // CFG-to-SCF may leave an unused integer dispatch constant. Only
                // exact i1 constants can serve as conditions in this body proof.
                if (!llvm::isa<mlir::IntegerAttr>(constant.getValue())) { return false; }
                if (constant.getType().isInteger(1)) { flags.insert(constant.getResult()); }
            } else if (auto made = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
                objects.try_emplace(made.getResult(), made.getResult());
                result.leafObjects.push_back(made);
            } else if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                const auto payload = alternatives.lookup(write.getValue());
                constexpr unsigned scalar =
                    PrimitiveAlternatives::Number | PrimitiveAlternatives::Boolean |
                    PrimitiveAlternatives::Null | PrimitiveAlternatives::Undefined;
                const unsigned mask = payload.truthy | payload.falsy;
                if (!objects.contains(write.getObject()) || !ordinaryKey(keyOf(write.getKey())) ||
                    !primitives.contains(write.getValue()) || !payload.known || !mask ||
                    (mask & ~scalar)) {
                    return false;
                }
                objectWrites.insert(write);
                result.leafWrites.push_back(write);
                const auto origin = objects.lookup(write.getObject());
                const auto key = keyOf(write.getKey());
                bool found = false;
                for (auto & field : fields) {
                    if (!step()) { return false; }
                    if (field.object == origin && field.key == key) {
                        field.payload = payload;
                        found = true;
                        break;
                    }
                }
                if (!found) { fields.push_back({origin, key, payload}); }
            } else if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
                if (compare.getKind() != ctjs::CompareKind::StrictEq ||
                    (!primitives.contains(compare.getLhs()) &&
                     !objects.contains(compare.getLhs())) ||
                    (!primitives.contains(compare.getRhs()) &&
                     !objects.contains(compare.getRhs()))) {
                    return false;
                }
                identities.insert(compare);
                primitives.insert(compare.getResult());
                alternatives.try_emplace(
                    compare.getResult(),
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>()));
            } else if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
                if (unary.getKind() != ctjs::UnaryKind::Not ||
                    !primitives.contains(unary.getOperand())) {
                    return false;
                }
                primitives.insert(unary.getResult());
                alternatives.try_emplace(
                    unary.getResult(),
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>()));
            } else if (auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
                if (!primitives.contains(truthy.getValue())) { return false; }
                flags.insert(truthy.getResult());
            } else if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                const bool hasElse = !branch.getElseRegion().empty();
                if (!flags.contains(branch.getCondition()) ||
                    !branch.getThenRegion().hasOneBlock() ||
                    branch.getThenRegion().front().getNumArguments() != 0 ||
                    (hasElse && (!branch.getElseRegion().hasOneBlock() ||
                                 branch.getElseRegion().front().getNumArguments() != 0)) ||
                    (!hasElse && branch.getNumResults() != 0)) {
                    return false;
                }
                auto thenYield = llvm::dyn_cast<mlir::scf::YieldOp>(
                    branch.getThenRegion().front().getTerminator());
                auto elseYield = hasElse ? llvm::dyn_cast<mlir::scf::YieldOp>(
                                               branch.getElseRegion().front().getTerminator())
                                         : mlir::scf::YieldOp{};
                if (!thenYield || thenYield.getNumOperands() != branch.getNumResults() ||
                    (hasElse &&
                     (!elseYield || elseYield.getNumOperands() != branch.getNumResults()))) {
                    return false;
                }
                for (const auto & entry : entries) {
                    (void)entry;
                    if (!step()) { return false; }
                }
                for (auto * observed : observations) {
                    (void)observed;
                    if (!step()) { return false; }
                }
                for (const auto & value : alternatives) {
                    (void)value;
                    if (!step()) { return false; }
                }
                for (const auto & field : fields) {
                    (void)field;
                    if (!step()) { return false; }
                }
                const auto incoming = entries;
                const auto incomingFields = fields;
                const auto incomingObservations = observations;
                const auto incomingAlternatives = alternatives;
                if (!learn(branch.getCondition(), true)) { return false; }
                if (!self(self, branch.getThenRegion().front(), depth + 1)) { return false; }
                auto thenEntries = std::move(entries);
                auto thenFields = std::move(fields);
                auto thenObservations = std::move(observations);
                auto thenAlternatives = std::move(alternatives);
                for (const auto & entry : incoming) {
                    (void)entry;
                    if (!step()) { return false; }
                }
                for (auto * observed : incomingObservations) {
                    (void)observed;
                    if (!step()) { return false; }
                }
                for (const auto & value : incomingAlternatives) {
                    (void)value;
                    if (!step()) { return false; }
                }
                for (const auto & field : incomingFields) {
                    (void)field;
                    if (!step()) { return false; }
                }
                entries = incoming;
                fields = incomingFields;
                observations = incomingObservations;
                alternatives = incomingAlternatives;
                if (!learn(branch.getCondition(), false)) { return false; }
                if (hasElse && !self(self, branch.getElseRegion().front(), depth + 1)) {
                    return false;
                }
                llvm::SmallVector<entry_fact> joined;
                for (auto left : thenEntries) {
                    if (!step()) { return false; }
                    for (const auto & right : entries) {
                        if (!step()) { return false; }
                        if (comparePrimitiveMapKeys(left.key, right.key) !=
                            PrimitiveMapKeyRelation::Same) {
                            continue;
                        }
                        left.payload = left.payload.joined(right.payload);
                        left.present &= right.present;
                        left.absent &= right.absent;
                        if (left.object != right.object) { left.object = {}; }
                        joined.push_back(left);
                        break;
                    }
                }
                entries = std::move(joined);
                llvm::SmallVector<field_fact> joinedFields;
                for (auto left : thenFields) {
                    if (!step()) { return false; }
                    for (const auto & right : fields) {
                        if (!step()) { return false; }
                        if (left.object != right.object || left.key != right.key) { continue; }
                        left.payload = left.payload.joined(right.payload);
                        joinedFields.push_back(left);
                        break;
                    }
                }
                fields = std::move(joinedFields);
                for (auto * observed : llvm::make_early_inc_range(observations)) {
                    if (!step()) { return false; }
                    if (!thenObservations.contains(observed)) { observations.erase(observed); }
                }
                for (unsigned index = 0; index < branch.getNumResults(); ++index) {
                    if (!step()) { return false; }
                    const auto left = thenYield.getOperand(index),
                               right = elseYield.getOperand(index);
                    if (!primitives.contains(left) || !primitives.contains(right)) { return false; }
                    auto value = branch.getResult(index);
                    primitives.insert(value);
                    const auto joinedValue =
                        thenAlternatives.lookup(left).joined(alternatives.lookup(right));
                    thenAlternatives[value] = joinedValue;
                    alternatives[value] = joinedValue;
                    const unsigned bound =
                        std::min(sizeBounds.lookup(left), sizeBounds.lookup(right));
                    if (bound) { sizeBounds[value] = bound; }
                }
                // Restore enclosing SSA facts after path-local refinements;
                // selected results retain the union of their actual yields.
                for (auto & value : llvm::make_early_inc_range(alternatives)) {
                    if (!step()) { return false; }
                    value.second = value.second.joined(thenAlternatives.lookup(value.first));
                }
            } else if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
                if (prepared || load.getIndex() != 0 || load.getClosure() != body.getArgument(2)) {
                    return false;
                }
                result.upvalues.push_back(load);
                upvalues.insert(load);
                maps.insert(load.getResult());
            } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                const auto key = keyOf(read.getKey());
                if (auto origin = objects.lookup(read.getObject())) {
                    if (!ordinaryKey(key)) { return false; }
                    PrimitiveAlternatives payload;
                    for (const auto & field : fields) {
                        if (!step()) { return false; }
                        if (field.object == origin && field.key == key) {
                            payload = field.payload;
                            break;
                        }
                    }
                    // Only a definitely initialized own field avoids prototype
                    // lookup. Saved scalar values keep their read-time facts;
                    // later writes through any alias update the allocation.
                    if (!payload.known) { return false; }
                    primitives.insert(read.getResult());
                    alternatives.try_emplace(read.getResult(), payload);
                    objectReads.insert(read);
                    result.leafReads.push_back(read);
                    continue;
                }
                if (!maps.contains(read.getObject()) ||
                    (key != "size" && key != "set" && key != "get" && key != "has" &&
                     key != "delete")) {
                    return false;
                }
                result.reads.push_back(read);
                reads.insert(read);
                if (key == "size") {
                    primitives.insert(read.getResult());
                    alternatives.try_emplace(
                        read.getResult(),
                        PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>()));
                    // Count only a pairwise-distinct subset of definite entries.
                    // Different SSA keys may denote the same runtime key. Saved
                    // bounds belong to this read, surviving later Map mutations.
                    llvm::SmallVector<mlir::Value> distinct;
                    for (const auto & entry : llvm::ArrayRef<entry_fact>(entries).take_front(
                             kMaxPrimitiveMapSizeCandidates)) {
                        if (!step()) { return false; }
                        if (!entry.present) { continue; }
                        bool disjoint = true;
                        for (mlir::Value previous : distinct) {
                            if (!step()) { return false; }
                            if (comparePrimitiveMapKeys(previous, entry.key, keyEvidence(previous),
                                                        keyEvidence(entry.key)) !=
                                PrimitiveMapKeyRelation::Distinct) {
                                disjoint = false;
                                break;
                            }
                        }
                        if (disjoint) { distinct.push_back(entry.key); }
                    }
                    sizeBounds[read.getResult()] = static_cast<unsigned>(distinct.size());
                }
            } else if (auto invoke = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                auto read = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (!read || !reads.contains(read) || read.getObject() != invoke.getReceiver()) {
                    return false;
                }
                const auto key = keyOf(read.getKey());
                if (key == "size" || invoke.getArgs().size() != (key == "set" ? 2u : 1u)) {
                    return false;
                }
                for (auto [index, argument] : llvm::enumerate(invoke.getArgs())) {
                    if (!step()) { return false; }
                    if (primitives.contains(argument)) { continue; }
                    if (key != "set" || index != 1 || !objects.contains(argument)) { return false; }
                    objectStores.insert(invoke);
                }
                result.calls.push_back(invoke);
                calls.insert(invoke);
                if (key == "set") {
                    maps.insert(invoke.getResult());
                    if (!mutate(invoke.getArgs()[0], false,
                                alternatives.lookup(invoke.getArgs()[1]),
                                objects.lookup(invoke.getArgs()[1]))) {
                        return false;
                    }
                } else {
                    if (key == "has" || key == "delete") {
                        primitives.insert(invoke.getResult());
                        alternatives.try_emplace(
                            invoke.getResult(),
                            PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>()));
                        if (key == "delete" && !mutate(invoke.getArgs()[0], true)) { return false; }
                        if (key == "has") { observations.insert(invoke); }
                    } else if (key == "get") {
                        if (primitiveContents) { primitives.insert(invoke.getResult()); }
                        for (const auto & entry : entries) {
                            if (!step()) { return false; }
                            if (comparePrimitiveMapKeys(entry.key, invoke.getArgs()[0]) ==
                                PrimitiveMapKeyRelation::Same) {
                                if (entry.absent) {
                                    primitives.insert(invoke.getResult());
                                    alternatives.try_emplace(
                                        invoke.getResult(),
                                        PrimitiveAlternatives::forTag(
                                            mlir::TypeID::get<ctjs::UndefinedAttr>()));
                                } else if (entry.present && entry.payload.known) {
                                    primitives.insert(invoke.getResult());
                                    alternatives.try_emplace(invoke.getResult(), entry.payload);
                                } else if (entry.present && entry.object) {
                                    objects.try_emplace(invoke.getResult(), entry.object);
                                }
                                break;
                            }
                        }
                    }
                }
            } else if (auto ret = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                if (depth != 0 || returned || !primitives.contains(ret.getValue())) {
                    return false;
                }
                returned = ret;
            } else if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
                if (depth == 0) { return false; }
                for (mlir::Value value : yield.getOperands()) {
                    if (!step() || !primitives.contains(value)) { return false; }
                }
            } else if (!llvm::isa<ctjs::RootOp>(operation) &&
                       (depth != 0 ||
                        !llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp>(operation))) {
                return false;
            }
        }
        return true;
    };
    if (!walk(walk, body, 0)) { return false; }
    if (!returned || result.reads.size() == firstRead ||
        (!prepared && result.upvalues.size() == firstUpvalue)) {
        return false;
    }
    for (mlir::BlockArgument argument : body.getArguments()) {
        if (argument.getArgNumber() >= offset) { continue; }
        if (prepared && argument.getArgNumber() == 3) { continue; }
        for (mlir::OpOperand & use : argument.getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                (!prepared && argument.getArgNumber() == 2 && upvalues.contains(use.getOwner()) &&
                 use.getOperandNumber() == 0)) {
                continue;
            }
            return false;
        }
    }
    for (mlir::Value alias : maps) {
        for (mlir::OpOperand & use : alias.getUses()) {
            if (!step()) { return false; }
            if (!dominance.dominates(alias, use.getOwner())) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                (reads.contains(use.getOwner()) && use.getOperandNumber() == 0) ||
                (calls.contains(use.getOwner()) && use.getOperandNumber() == 1)) {
                continue;
            }
            return false;
        }
    }
    // Scalar read-time facts, including an absent get's Undefined result,
    // cannot cross their source scope through malformed live SSA edits. The
    // same requirement protects primitive key evidence and branch conditions.
    for (const auto * values : {&primitives, &flags}) {
        for (mlir::Value value : *values) {
            for (mlir::OpOperand & use : value.getUses()) {
                if (!step() || !dominance.dominates(value, use.getOwner())) { return false; }
            }
        }
    }
    // A method-local object is a leaf owner, with the captured Map as its only
    // possible longer-lived owner. Check every use, including uses outside the
    // walked body. Neither native markers nor provider allocation tokens can
    // authorize a field, alias, escape or future invocation here.
    for (const auto & [object, origin] : objects) {
        (void)origin;
        for (mlir::OpOperand & use : object.getUses()) {
            if (!step()) { return false; }
            // Live analysis queries may follow edits before MLIR verification.
            // A then-only allocation/read cannot authorize an else or outer
            // use merely because its immutable origin was visited first.
            if (!dominance.dominates(object, use.getOwner())) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                (objectWrites.contains(use.getOwner()) && use.getOperandNumber() == 0) ||
                (objectReads.contains(use.getOwner()) && use.getOperandNumber() == 0) ||
                (identities.contains(use.getOwner()) && use.getOperandNumber() < 2) ||
                (objectStores.contains(use.getOwner()) && use.getOperandNumber() == 3)) {
                continue;
            }
            return false;
        }
    }
    for (std::size_t index = firstRead; index < result.reads.size(); ++index) {
        auto read = result.reads[index];
        if (!step()) { return false; }
        if (keyOf(read.getKey()) == "size") { continue; }
        for (mlir::OpOperand & use : read.getResult().getUses()) {
            if (!step()) { return false; }
            if (!dominance.dominates(read.getResult(), use.getOwner())) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                (calls.contains(use.getOwner()) && use.getOperandNumber() == 0)) {
                continue;
            }
            return false;
        }
    }
    // Unproved Map.get results remain potentially nullable/mixed. Even a
    // local finite result still needs the independent native Map schema and
    // presence analyses before a consuming formal can acquire a C++ carrier.
    returnAlternatives = alternatives.lookup(returned.getValue());
    return true;
}

} // namespace ctcompile::ctnative::host_detail
