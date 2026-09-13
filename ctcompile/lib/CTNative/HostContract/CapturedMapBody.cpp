#include "Analysis.h"

#include "../Analysis/NativeMap/SnapshotCopies.h"
#include "../Analysis/PrimitiveAlternatives.h"
#include "../Analysis/PrimitiveMapKey.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

#include <algorithm>
#include <cmath>

namespace ctcompile::ctnative::host_detail {

bool analyzer::capturedMapBody(ctjs::FuncOp function, bool prepared, bool primitiveContents,
                               const HostMethodParameters & parameters, HostCapturedMap & result,
                               PrimitiveAlternatives & returnAlternatives) {
    // This is an effects and ownership proof, not an evaluation of the first
    // invocation. The immutable slot always denotes this Map; its contents
    // may change at every call. A complete body census closes all writes over
    // primitives, fresh leaf objects and fresh child Maps. Child Maps cannot
    // retain Maps. Mixed child results may return owning values, without
    // primitive or field authority; no accessor can execute.
    // The native identity, field and Map analyses still independently prove
    // their carriers.
    auto & body = function.getBody().front();
    const auto firstRead = result.reads.size();
    const auto firstUpvalue = result.upvalues.size();
    llvm::DenseSet<mlir::Value> primitives, flags;
    // Closed scalar/leaf alternatives permit non-coercing observations, never
    // an own-field receiver or a primitive category without separate evidence.
    llvm::DenseSet<mlir::Value> leafValues;
    llvm::DenseSet<mlir::Value> snapshotElements;
    llvm::DenseSet<mlir::Operation *> snapshotReads;
    std::optional<map_detail::snapshotCopies> copies;
    const auto prepareSnapshots = [&] {
        if (copies) { return true; }
        if (!llvm::is_contained(contract.initialIntrinsics, "Array") || !globals["Array"].empty()) {
            return false;
        }
        llvm::DenseSet<mlir::Operation *> candidates;
        const auto census =
            function.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * operation) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                if (!call) { return mlir::WalkResult::advance(); }
                auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (read && ctjs::constantKey(read.getKey()) == "keys") { candidates.insert(call); }
                return mlir::WalkResult::advance();
            });
        map_detail::snapshotCopies found;
        if (census.wasInterrupted() ||
            !map_detail::collectSnapshotCopies(function, candidates, found, [&] {
                 return step();
             }).empty()) {
            return false;
        }
        // Candidates have only an iterator timing proof. The walk below must
        // still close every receiver/effect before this family can be published.
        copies = std::move(found);
        return true;
    };
    llvm::DenseMap<mlir::Value, mlir::Value> maps;
    const auto capturedOrigin = body.getArgument(prepared ? 3 : 2);
    llvm::DenseSet<mlir::Operation *> constructors, allocations, mapStores;
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
    // Capture loads and each receiver's fluent returns preserve its origin.
    // Captured contents start unknown each invocation. A set preserves presence
    // even when its key may alias an earlier entry. In that case the payload
    // retains the union of both independently proved primitive alternatives.
    // Presence alone never supplies payload evidence. A possibly aliasing delete
    // removes definite membership, but cannot change a surviving payload. Its
    // alternatives stay valid whenever present; an exact set replaces them.
    // Definite absence is independent of possible absence: an exact deletion
    // or clear establishes it. A possibly aliasing write invalidates absence,
    // whereas another deletion cannot make an absent key present.
    // This local contents fact is independent of the family's return worklist
    // and publishes alternatives only after the entire body/use proof completes.
    struct entry_fact {
        mlir::Value key;
        PrimitiveAlternatives payload;
        bool present = true;
        mlir::Value object;
        bool absent = false;
        mlir::Value map{};
    };
    struct map_state {
        llvm::SmallVector<entry_fact> entries;
        bool completeKeys = false;
        llvm::SmallVector<mlir::Value> possibleKeys;
        std::optional<unsigned> currentSize;
        llvm::DenseSet<mlir::Operation *> observations;
    };
    llvm::DenseMap<mlir::Value, map_state> mapStates;
    mapStates.try_emplace(capturedOrigin);
    // Mutable state belongs to a runtime origin, never a shared C++ schema.
    // Captured contents start unknown on every invocation; a fresh child starts
    // empty. Saved aliases continue to denote that child after outer mutation.
    llvm::DenseMap<mlir::Value, unsigned> sizeBounds;
    llvm::DenseMap<mlir::Value, unsigned> exactSizes;
    const auto primitiveTag = [&](mlir::Value key) -> std::optional<mlir::TypeID> {
        return alternatives.lookup(key).tag();
    };
    const auto keyEvidence = [&](mlir::Value key) {
        const auto found = exactSizes.find(key);
        return PrimitiveMapKeyEvidence{primitiveTag(key), sizeBounds.lookup(key),
                                       found == exactSizes.end() ? std::nullopt
                                                                 : std::optional(found->second)};
    };
    struct size_fact {
        unsigned lower = 0;
        std::optional<unsigned> exact;
    };
    const auto sizeFacts = [&](const map_state & state) -> std::optional<size_fact> {
        if (!step()) { return std::nullopt; }
        if (state.currentSize) { return size_fact{*state.currentSize, state.currentSize}; }
        llvm::SmallVector<mlir::Value> distinct;
        for (const auto & entry :
             llvm::ArrayRef<entry_fact>(state.entries).take_front(kMaxPrimitiveMapSizeCandidates)) {
            if (!step()) { return std::nullopt; }
            if (!entry.present) { continue; }
            bool disjoint = true;
            for (mlir::Value previous : distinct) {
                if (!step()) { return std::nullopt; }
                if (comparePrimitiveMapKeys(previous, entry.key, keyEvidence(previous),
                                            keyEvidence(entry.key)) !=
                    PrimitiveMapKeyRelation::Distinct) {
                    disjoint = false;
                    break;
                }
            }
            if (disjoint) { distinct.push_back(entry.key); }
        }
        size_fact fact{static_cast<unsigned>(distinct.size()), std::nullopt};
        // Deduplicate only proved equal keys in the complete upper bound.
        // Unknown equality may overcount, never undercount. Neither an empty
        // intersection nor a census discarded at its limit proves a size.
        if (state.completeKeys && state.possibleKeys.size() <= kMaxPrimitiveMapSizeCandidates) {
            llvm::SmallVector<mlir::Value> possible;
            for (mlir::Value key : state.possibleKeys) {
                if (!step()) { return std::nullopt; }
                bool same = false;
                for (mlir::Value previous : possible) {
                    if (!step()) { return std::nullopt; }
                    if (comparePrimitiveMapKeys(previous, key, keyEvidence(previous),
                                                keyEvidence(key)) ==
                        PrimitiveMapKeyRelation::Same) {
                        same = true;
                        break;
                    }
                }
                if (!same) { possible.push_back(key); }
            }
            if (possible.size() == distinct.size()) { fact.exact = fact.lower; }
        }
        return fact;
    };
    const auto absent = [&](mlir::Value key, llvm::ArrayRef<entry_fact> facts, bool complete,
                            llvm::ArrayRef<mlir::Value> possible,
                            bool currentPath = true) -> std::optional<bool> {
        for (const auto & entry : facts) {
            if (!step()) { return std::nullopt; }
            const auto relation =
                currentPath ? comparePrimitiveMapKeys(entry.key, key, keyEvidence(entry.key),
                                                      keyEvidence(key))
                            : comparePrimitiveMapKeys(entry.key, key);
            if (relation == PrimitiveMapKeyRelation::Same) { return entry.absent; }
        }
        if (!complete) { return false; }
        for (mlir::Value written : possible) {
            if (!step()) { return std::nullopt; }
            // Cross-arm queries cannot use the other arm's filtered scalar
            // alternatives. Exact source constants/SSA equality are path-free.
            const auto relation =
                currentPath
                    ? comparePrimitiveMapKeys(written, key, keyEvidence(written), keyEvidence(key))
                    : comparePrimitiveMapKeys(written, key);
            if (relation != PrimitiveMapKeyRelation::Distinct) { return false; }
        }
        return true;
    };
    const auto mutate = [&](map_state & state, mlir::Value key, bool erase,
                            PrimitiveAlternatives payload = {}, mlir::Value object = {},
                            mlir::Value child = {}) {
        auto nextSize = state.currentSize;
        state.currentSize.reset();
        // Read membership before changing the entries. A possible overwrite
        // versus insertion (or successful versus unsuccessful deletion) has
        // no exact size effect. Do not bypass the candidate limit by keeping
        // an independent size after the complete census exceeds its budget.
        if (nextSize && state.completeKeys &&
            state.possibleKeys.size() <= kMaxPrimitiveMapSizeCandidates) {
            bool present = false;
            for (const auto & entry : state.entries) {
                if (!step()) { return false; }
                if (entry.present &&
                    comparePrimitiveMapKeys(entry.key, key, keyEvidence(entry.key),
                                            keyEvidence(key)) == PrimitiveMapKeyRelation::Same) {
                    present = true;
                    break;
                }
            }
            const auto missing = absent(key, state.entries, state.completeKeys, state.possibleKeys);
            if (!missing) { return false; }
            if (present) {
                if (erase) {
                    if (*nextSize == 0) {
                        nextSize.reset();
                    } else {
                        --*nextSize;
                    }
                }
            } else if (*missing) {
                if (!erase) {
                    if (*nextSize == kMaxPrimitiveMapSizeCandidates) {
                        nextSize.reset();
                    } else {
                        ++*nextSize;
                    }
                }
            } else {
                nextSize.reset();
            }
        } else {
            nextSize.reset();
        }
        bool hasExactAbsence = false;
        for (auto it = state.entries.begin(); it != state.entries.end();) {
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
                it = state.entries.erase(it);
            } else {
                // The old payload survives if these runtime keys differ; the
                // new one wins if they compare equal. Unknown joined with any
                // finite set remains unknown, including after further writes.
                it->payload = it->payload.joined(payload);
                if (it->object != object) { it->object = {}; }
                if (it->map != child) { it->map = {}; }
                it->absent = false;
                ++it;
            }
        }
        if (erase) {
            for (auto * observed : llvm::make_early_inc_range(state.observations)) {
                if (!step()) { return false; }
                auto call = llvm::cast<ctjs::CallOp>(observed);
                auto observedKey = call.getArgs()[0];
                if (comparePrimitiveMapKeys(observedKey, key, keyEvidence(observedKey),
                                            keyEvidence(key)) !=
                    PrimitiveMapKeyRelation::Distinct) {
                    state.observations.erase(observed);
                }
            }
        }
        if (erase) {
            // Deletion cannot introduce a key. Keep the complete upper bound
            // through this mutation, removing only keys proved equal to the
            // erased key. A possibly equal key may survive and must remain in
            // the census even though it lost definite presence above.
            // An oversized census cannot recover an exact size by pruning
            // after its limit was exceeded. Retain that conservative upper
            // bound for absence queries until a real clear starts a new one.
            if (state.possibleKeys.size() <= kMaxPrimitiveMapSizeCandidates) {
                size_t retained = 0;
                for (mlir::Value possible : state.possibleKeys) {
                    if (!step()) { return false; }
                    if (comparePrimitiveMapKeys(possible, key, keyEvidence(possible),
                                                keyEvidence(key)) !=
                        PrimitiveMapKeyRelation::Same) {
                        state.possibleKeys[retained++] = possible;
                    }
                }
                state.possibleKeys.resize(retained);
            }
            // The invocation's initial contents are unknown, but this exact
            // key is now absent even if no earlier local set mentioned it.
            // Keep earlier payload evidence whenever present: a branch join
            // may still reach that payload from its nondeleting arm.
            if (!hasExactAbsence) { state.entries.push_back({key, {}, false, {}, true}); }
        } else {
            state.entries.push_back({key, payload, true, object, false, child});
            if (state.completeKeys) {
                if (!step()) { return false; }
                state.possibleKeys.push_back(key);
            }
        }
        if (state.possibleKeys.size() <= kMaxPrimitiveMapSizeCandidates) {
            state.currentSize = nextSize;
        }
        return true;
    };
    const auto learn = [&](mlir::Value condition, bool branch) {
        while (true) {
            if (auto truthy = condition.getDefiningOp<ctjs::TruthyOp>()) {
                if (!step()) { return false; }
                condition = truthy.getValue();
            } else if (auto unary = condition.getDefiningOp<ctjs::UnaryOp>();
                       unary && unary.getKind() == ctjs::UnaryKind::Not) {
                if (!step()) { return false; }
                branch = !branch;
                condition = unary.getOperand();
            } else {
                break;
            }
        }
        if (auto found = alternatives.find(condition); found != alternatives.end()) {
            found->second = found->second.filtered(branch);
        }
        if (auto compare = condition.getDefiningOp<ctjs::CompareOp>();
            compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
            for (unsigned index = 0; index < 2; ++index) {
                if (!step()) { return false; }
                auto literal = compare->getOperand(index).getDefiningOp<ctjs::ConstantOp>();
                if (!literal ||
                    !llvm::isa<ctjs::NullAttr, ctjs::UndefinedAttr>(literal.getValue())) {
                    continue;
                }
                if (auto found = alternatives.find(compare->getOperand(1 - index));
                    found != alternatives.end()) {
                    const auto mask = PrimitiveAlternatives::literal(literal.getValue()).falsy;
                    found->second.truthy &= branch ? mask : ~mask;
                    found->second.falsy &= branch ? mask : ~mask;
                }
            }
        }
        auto call = condition.getDefiningOp<ctjs::CallOp>();
        if (!branch || !call || !maps.contains(call.getReceiver())) { return true; }
        auto & state = mapStates[maps.lookup(call.getReceiver())];
        if (!state.observations.contains(call)) { return true; }
        // Only a live, checked has on this captured Map supplies membership.
        // It never creates a payload tag or selects away the other arm.
        auto key = call.getArgs()[0];
        for (auto & entry : state.entries) {
            if (!step()) { return false; }
            if (comparePrimitiveMapKeys(entry.key, key) == PrimitiveMapKeyRelation::Same) {
                entry.present = true;
                entry.absent = false;
                return true;
            }
        }
        state.entries.push_back({key, {}, true, {}});
        return true;
    };
    llvm::DenseSet<mlir::Operation *> reads, calls, upvalues;
    if (prepared) { maps.try_emplace(body.getArgument(3), capturedOrigin); }
    const unsigned offset = prepared ? 4u : 3u;
    if (parameters.function != function ||
        parameters.alternatives.size() != body.getNumArguments() - offset) {
        return false;
    }
    for (mlir::BlockArgument parameter : body.getArguments().drop_front(offset)) {
        if (!step()) { return false; }
        if (llvm::is_contained(parameters.objectKeys, parameter)) {
            leafValues.insert(parameter);
            continue;
        }
        primitives.insert(parameter);
        alternatives.try_emplace(parameter,
                                 parameters.alternatives[parameter.getArgNumber() - offset]);
    }
    const auto chargeStates = [&](const auto & states) {
        for (const auto & item : states) {
            if (!step()) { return false; }
            const auto & state = item.second;
            for (const auto & entry : state.entries) {
                (void)entry;
                if (!step()) { return false; }
            }
            for (auto * observed : state.observations) {
                (void)observed;
                if (!step()) { return false; }
            }
            for (mlir::Value key : state.possibleKeys) {
                (void)key;
                if (!step()) { return false; }
            }
        }
        return true;
    };
    const auto invalidateAliases = [&](mlir::Value origin) {
        if (origin == capturedOrigin) { return true; }
        for (auto & [other, state] : mapStates) {
            if (!step()) { return false; }
            if (other == capturedOrigin || other == origin ||
                (origin.getDefiningOp<ctjs::ConstructOp>() &&
                 other.getDefiningOp<ctjs::ConstructOp>())) {
                continue;
            }
            // A returned owner may alias another child, including a child
            // published on only one branch. Fresh constructors alone are
            // disjoint. Keep saved identities, discard unproved mutable facts.
            state = {};
        }
        return true;
    };
    ctjs::ReturnOp returned;
    ctjs::FrameEnterOp frame;
    llvm::DenseSet<mlir::Operation *> frameUses;
    bool frameExited = false;
    // SSA scalar facts and the complete use census are immutable across paths.
    // Mutable contents are copied for each arm and intersected only after both
    // arms finish. An observed startup condition never selects a future path.
    const auto walk = [&](auto && self, mlir::Block & block, unsigned depth) -> bool {
        if (depth > 32 || !step()) { return false; }
        for (mlir::Operation & operation : block) {
            if (!step()) { return false; }
            // An exited entry frame can only flow through the scalar return
            // joins. No later operation may use it or perform another effect.
            if (frameExited && !llvm::isa<ctjs::ReturnOp, mlir::scf::YieldOp>(operation)) {
                return false;
            }
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
            } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
                if (load.getName() == "Array") {
                    if (!prepareSnapshots() || !copies->builtins.contains(load)) { return false; }
                    result.snapshotOperations.push_back(load);
                    continue;
                }
                if (load.getName() != "Map" || !globals["Map"].empty()) { return false; }
                constructors.insert(load);
            } else if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(operation)) {
                auto constructor = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                if (!constructor || !constructors.contains(constructor) ||
                    made.getNewTarget() != made.getCallee() || !made.getArgs().empty() ||
                    !dominance.dominates(made.getCallee(), made)) {
                    return false;
                }
                maps.try_emplace(made.getResult(), made.getResult());
                auto & state = mapStates[made.getResult()];
                state.completeKeys = true;
                state.currentSize = 0;
                allocations.insert(made);
                result.childMaps.push_back(made);
            } else if (auto made = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
                objects.try_emplace(made.getResult(), made.getResult());
                result.leafObjects.push_back(made);
            } else if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                const auto payload = alternatives.lookup(write.getValue());
                constexpr unsigned scalar =
                    PrimitiveAlternatives::Number | PrimitiveAlternatives::Boolean |
                    PrimitiveAlternatives::String | PrimitiveAlternatives::Null |
                    PrimitiveAlternatives::Undefined;
                const unsigned mask = payload.truthy | payload.falsy;
                if (!objects.contains(write.getObject()) ||
                    !ctjs::ordinaryKey(ctjs::constantKey(write.getKey())) ||
                    !primitives.contains(write.getValue()) || !payload.known || !mask ||
                    (mask & ~scalar)) {
                    return false;
                }
                objectWrites.insert(write);
                result.leafWrites.push_back(write);
                const auto origin = objects.lookup(write.getObject());
                const auto key = ctjs::constantKey(write.getKey());
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
                     !objects.contains(compare.getLhs()) &&
                     !leafValues.contains(compare.getLhs()) &&
                     !snapshotElements.contains(compare.getLhs())) ||
                    (!primitives.contains(compare.getRhs()) &&
                     !objects.contains(compare.getRhs()) &&
                     !leafValues.contains(compare.getRhs()) &&
                     !snapshotElements.contains(compare.getRhs()))) {
                    return false;
                }
                identities.insert(compare);
                primitives.insert(compare.getResult());
                alternatives.try_emplace(
                    compare.getResult(),
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>()));
            } else if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
                if (unary.getKind() != ctjs::UnaryKind::Not ||
                    (!primitives.contains(unary.getOperand()) &&
                     !leafValues.contains(unary.getOperand()))) {
                    return false;
                }
                primitives.insert(unary.getResult());
                alternatives.try_emplace(
                    unary.getResult(),
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>()));
            } else if (auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
                if (!primitives.contains(truthy.getValue()) &&
                    !leafValues.contains(truthy.getValue())) {
                    return false;
                }
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
                if (!chargeStates(mapStates)) { return false; }
                for (const auto & value : alternatives) {
                    (void)value;
                    if (!step()) { return false; }
                }
                for (const auto & field : fields) {
                    (void)field;
                    if (!step()) { return false; }
                }
                const auto incoming = mapStates;
                const auto incomingFields = fields;
                const auto incomingAlternatives = alternatives;
                const bool incomingExit = frameExited;
                if (!learn(branch.getCondition(), true)) { return false; }
                if (!self(self, branch.getThenRegion().front(), depth + 1)) { return false; }
                const bool thenExit = frameExited;
                auto thenStates = std::move(mapStates);
                auto thenFields = std::move(fields);
                auto thenAlternatives = std::move(alternatives);
                if (!chargeStates(incoming)) { return false; }
                for (const auto & value : incomingAlternatives) {
                    (void)value;
                    if (!step()) { return false; }
                }
                for (const auto & field : incomingFields) {
                    (void)field;
                    if (!step()) { return false; }
                }
                // Resolve then-arm cardinality while its scalar facts are live.
                alternatives = thenAlternatives;
                for (auto & item : thenStates) {
                    const auto fact = sizeFacts(item.second);
                    if (!fact) { return false; }
                    item.second.currentSize = fact->exact;
                }
                mapStates = incoming;
                fields = incomingFields;
                alternatives = incomingAlternatives;
                frameExited = incomingExit;
                if (!learn(branch.getCondition(), false)) { return false; }
                if (hasElse && !self(self, branch.getElseRegion().front(), depth + 1)) {
                    return false;
                }
                if (frameExited != thenExit) { return false; }
                for (const auto & item : thenStates) {
                    if (!step()) { return false; }
                    mapStates.try_emplace(item.first);
                }
                for (auto & item : mapStates) {
                    if (!step()) { return false; }
                    auto & state = item.second;
                    const auto & then = thenStates[item.first];
                    const auto elseSize = sizeFacts(state);
                    if (!elseSize) { return false; }
                    state.currentSize =
                        then.currentSize == elseSize->exact ? then.currentSize : std::nullopt;
                    llvm::SmallVector<entry_fact> joined;
                    for (auto left : then.entries) {
                        if (!step()) { return false; }
                        bool matched = false;
                        for (const auto & right : state.entries) {
                            if (!step()) { return false; }
                            if (comparePrimitiveMapKeys(left.key, right.key) !=
                                PrimitiveMapKeyRelation::Same) {
                                continue;
                            }
                            left.payload = left.payload.joined(right.payload);
                            left.present &= right.present;
                            left.absent &= right.absent;
                            if (left.object != right.object) { left.object = {}; }
                            if (left.map != right.map) { left.map = {}; }
                            joined.push_back(left);
                            matched = true;
                            break;
                        }
                        if (!matched) {
                            const auto missing = absent(left.key, state.entries, state.completeKeys,
                                                        state.possibleKeys, false);
                            if (!missing) { return false; }
                            if (*missing) {
                                left.present = false;
                                joined.push_back(left);
                            }
                        }
                    }
                    // A key first mentioned on the other arm may still be absent
                    // on both paths when this arm cleared the entire Map.
                    for (auto right : state.entries) {
                        if (!step()) { return false; }
                        bool matched = false;
                        for (const auto & left : then.entries) {
                            if (!step()) { return false; }
                            if (comparePrimitiveMapKeys(left.key, right.key) ==
                                PrimitiveMapKeyRelation::Same) {
                                matched = true;
                                break;
                            }
                        }
                        if (matched) { continue; }
                        const auto missing = absent(right.key, then.entries, then.completeKeys,
                                                    then.possibleKeys, false);
                        if (!missing) { return false; }
                        if (*missing) {
                            right.present = false;
                            joined.push_back(right);
                        }
                    }
                    state.entries = std::move(joined);
                    state.completeKeys &= then.completeKeys;
                    if (state.completeKeys) {
                        for (mlir::Value key : then.possibleKeys) {
                            if (!step()) { return false; }
                            state.possibleKeys.push_back(key);
                        }
                    } else {
                        state.possibleKeys.clear();
                    }
                    for (auto * observed : llvm::make_early_inc_range(state.observations)) {
                        if (!step()) { return false; }
                        if (!then.observations.contains(observed)) {
                            state.observations.erase(observed);
                        }
                    }
                }
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
                for (unsigned index = 0; index < branch.getNumResults(); ++index) {
                    if (!step()) { return false; }
                    const auto left = thenYield.getOperand(index),
                               right = elseYield.getOperand(index);
                    if ((!primitives.contains(left) && !leafValues.contains(left)) ||
                        (!primitives.contains(right) && !leafValues.contains(right))) {
                        return false;
                    }
                    auto value = branch.getResult(index);
                    if (leafValues.contains(left) || leafValues.contains(right)) {
                        leafValues.insert(value);
                        continue;
                    }
                    primitives.insert(value);
                    const auto joinedValue =
                        thenAlternatives.lookup(left).joined(alternatives.lookup(right));
                    thenAlternatives[value] = joinedValue;
                    alternatives[value] = joinedValue;
                    const unsigned bound =
                        std::min(sizeBounds.lookup(left), sizeBounds.lookup(right));
                    if (bound) { sizeBounds[value] = bound; }
                    const auto exact = primitiveMapSize(left, keyEvidence(left));
                    if (exact && exact == primitiveMapSize(right, keyEvidence(right))) {
                        exactSizes[value] = *exact;
                    }
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
                maps.try_emplace(load.getResult(), capturedOrigin);
            } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                const auto key = ctjs::constantKey(read.getKey());
                if (copies && copies->builtins.contains(read)) {
                    result.snapshotOperations.push_back(read);
                    continue;
                }
                if (copies && copies->calls.contains(read.getObject().getDefiningOp())) {
                    if (key == "length") {
                        primitives.insert(read.getResult());
                        alternatives.try_emplace(
                            read.getResult(),
                            PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>()));
                    } else {
                        auto literal = read.getKey().getDefiningOp<ctjs::ConstantOp>();
                        auto number = literal ? llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue())
                                              : ctjs::NumberAttr{};
                        if (!number || !std::isfinite(number.getDouble()) ||
                            number.getDouble() < 0 || number.getDouble() >= 4294967295.0 ||
                            std::floor(number.getDouble()) != number.getDouble()) {
                            return false;
                        }
                        snapshotElements.insert(read.getResult());
                    }
                    snapshotReads.insert(read);
                    result.snapshotOperations.push_back(read);
                    continue;
                }
                if (auto origin = objects.lookup(read.getObject())) {
                    if (!ctjs::ordinaryKey(key)) { return false; }
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
                     key != "delete" && key != "clear" && key != "keys")) {
                    return false;
                }
                if (key == "keys" && !prepareSnapshots()) { return false; }
                result.reads.push_back(read);
                reads.insert(read);
                if (key == "size") {
                    primitives.insert(read.getResult());
                    alternatives.try_emplace(
                        read.getResult(),
                        PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>()));
                    // Capture this position's cardinality, including an equal
                    // structural join, independently of all later mutations.
                    const auto fact = sizeFacts(mapStates[maps.lookup(read.getObject())]);
                    if (!fact) { return false; }
                    sizeBounds[read.getResult()] = fact->lower;
                    if (fact->exact) { exactSizes[read.getResult()] = *fact->exact; }
                }
            } else if (auto invoke = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                if (copies && copies->calls.contains(invoke)) {
                    auto iterator = invoke.getArgs().front().getDefiningOp<ctjs::CallOp>();
                    if (!iterator || !calls.contains(iterator)) { return false; }
                    result.snapshotOperations.push_back(invoke);
                    continue;
                }
                auto read = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (!read || !reads.contains(read) || read.getObject() != invoke.getReceiver()) {
                    return false;
                }
                const auto key = ctjs::constantKey(read.getKey());
                const unsigned arity =
                    key == "set" ? 2u : (key == "clear" || key == "keys" ? 0u : 1u);
                if (key == "size" || invoke.getArgs().size() != arity) { return false; }
                const auto origin = maps.lookup(invoke.getReceiver());
                if (!origin) { return false; }
                if ((key == "set" || key == "delete" || key == "clear") &&
                    !invalidateAliases(origin)) {
                    return false;
                }
                auto & state = mapStates[origin];
                for (auto [index, argument] : llvm::enumerate(invoke.getArgs())) {
                    if (!step()) { return false; }
                    if (primitives.contains(argument)) { continue; }
                    if (llvm::is_contained(parameters.objectKeys, argument) &&
                        (index == 0 || (key == "set" && index == 1))) {
                        continue;
                    }
                    // ponytail: one nested level; deeper retention needs a checked
                    // acyclic ownership graph before child Maps can retain Maps.
                    if (key == "set" && index == 1 && origin == capturedOrigin &&
                        maps.contains(argument) && maps.lookup(argument) != capturedOrigin) {
                        mapStores.insert(invoke);
                        continue;
                    }
                    if (key != "set" || index != 1 || !objects.contains(argument)) { return false; }
                    objectStores.insert(invoke);
                }
                result.calls.push_back(invoke);
                calls.insert(invoke);
                if (key == "keys") { continue; }
                if (key == "set") {
                    maps.try_emplace(invoke.getResult(), origin);
                    if (!mutate(state, invoke.getArgs()[0], false,
                                alternatives.lookup(invoke.getArgs()[1]),
                                objects.lookup(invoke.getArgs()[1]),
                                maps.lookup(invoke.getArgs()[1]))) {
                        return false;
                    }
                } else if (key == "clear") {
                    // Saved SSA reads retain their old payload or object; only
                    // the mutable Map state and live has observations change.
                    for (auto & entry : state.entries) {
                        if (!step()) { return false; }
                        entry.present = false;
                        entry.absent = true;
                    }
                    for (auto * observed : state.observations) {
                        (void)observed;
                        if (!step()) { return false; }
                    }
                    for (mlir::Value written : state.possibleKeys) {
                        (void)written;
                        if (!step()) { return false; }
                    }
                    state.observations.clear();
                    state.possibleKeys.clear();
                    state.completeKeys = true;
                    state.currentSize = 0;
                    primitives.insert(invoke.getResult());
                    alternatives.try_emplace(
                        invoke.getResult(),
                        PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::UndefinedAttr>()));
                } else {
                    if (key == "has" || key == "delete") {
                        primitives.insert(invoke.getResult());
                        alternatives.try_emplace(
                            invoke.getResult(),
                            PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>()));
                        if (key == "delete" && !mutate(state, invoke.getArgs()[0], true)) {
                            return false;
                        }
                        if (key == "has") { state.observations.insert(invoke); }
                    } else if (key == "get") {
                        if (primitiveContents && origin == capturedOrigin) {
                            primitives.insert(invoke.getResult());
                        }
                        const auto missing = absent(invoke.getArgs()[0], state.entries,
                                                    state.completeKeys, state.possibleKeys);
                        if (!missing) { return false; }
                        if (*missing) {
                            primitives.insert(invoke.getResult());
                            alternatives.try_emplace(invoke.getResult(),
                                                     PrimitiveAlternatives::forTag(
                                                         mlir::TypeID::get<ctjs::UndefinedAttr>()));
                            continue;
                        }
                        for (auto & entry : state.entries) {
                            if (!step()) { return false; }
                            if (comparePrimitiveMapKeys(entry.key, invoke.getArgs()[0],
                                                        keyEvidence(entry.key),
                                                        keyEvidence(invoke.getArgs()[0])) ==
                                PrimitiveMapKeyRelation::Same) {
                                if (entry.present && entry.payload.known) {
                                    primitives.insert(invoke.getResult());
                                    alternatives.try_emplace(invoke.getResult(), entry.payload);
                                } else if (entry.present && entry.map) {
                                    maps.try_emplace(invoke.getResult(), entry.map);
                                    result.returnedChildMaps.push_back(invoke.getResult());
                                } else if (entry.present && entry.object) {
                                    objects.try_emplace(invoke.getResult(), entry.object);
                                } else if (entry.present && origin == capturedOrigin &&
                                           result.childMapContents) {
                                    // The actual returned owner is an SSA origin,
                                    // not a constructor from an earlier call. Only
                                    // independently proved family invariants can
                                    // describe its unknown prior contents.
                                    entry.map = invoke.getResult();
                                    maps.try_emplace(invoke.getResult(), invoke.getResult());
                                    result.returnedChildMaps.push_back(invoke.getResult());
                                }
                                break;
                            }
                        }
                        if (origin != capturedOrigin && result.childScalarContents.tag()) {
                            primitives.insert(invoke.getResult());
                            // A local exact write/absence takes precedence. The family
                            // category alone never establishes that this key exists.
                            alternatives.try_emplace(
                                invoke.getResult(),
                                result.childScalarContents.joined(PrimitiveAlternatives::forTag(
                                    mlir::TypeID::get<ctjs::UndefinedAttr>())));
                        }
                        if (origin != capturedOrigin && result.childLeafContents &&
                            !primitives.contains(invoke.getResult()) &&
                            !objects.contains(invoke.getResult())) {
                            leafValues.insert(invoke.getResult());
                        }
                        if (maps.lookup(invoke.getResult()) == invoke.getResult()) {
                            auto & child = mapStates[invoke.getResult()];
                            for (const auto & entry : result.childEntries) {
                                if (!step()) { return false; }
                                child.entries.push_back({entry.key, entry.alternatives, true, {}});
                            }
                        }
                    }
                }
            } else if (auto ret = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                if (depth != 0 || returned || &operation != &block.back() ||
                    (frame && !frameExited) ||
                    (!primitives.contains(ret.getValue()) &&
                     !leafValues.contains(ret.getValue()))) {
                    return false;
                }
                returned = ret;
            } else if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
                if (depth == 0 || &operation != &block.back()) { return false; }
                for (mlir::Value value : yield.getOperands()) {
                    if (!step() || (!primitives.contains(value) && !leafValues.contains(value))) {
                        return false;
                    }
                }
            } else if (auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
                const auto count = enter->getAttrOfType<mlir::IntegerAttr>("reg_count");
                if (depth != 0 || frame || enter->getNumOperands() != 0 ||
                    enter->getNumResults() != 1 ||
                    !llvm::isa<ctjs::ContextType>(enter->getResult(0).getType()) || !count ||
                    !count.getType().isInteger(32) || count.getInt() < 0) {
                    return false;
                }
                frame = enter;
            } else if (auto exit = llvm::dyn_cast<ctjs::FrameExitOp>(operation)) {
                if (!frame || exit->getNumOperands() != 1 || exit->getNumResults() != 0 ||
                    exit.getContext() != frame.getContext()) {
                    return false;
                }
                frameExited = true;
                frameUses.insert(exit);
            } else if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation)) {
                if (!frame || root->getNumOperands() != 2 || root->getNumResults() != 0 ||
                    !llvm::isa<ctjs::ValueType>(root->getOperand(1).getType()) ||
                    root.getContext() != frame.getContext()) {
                    return false;
                }
                frameUses.insert(root);
            } else {
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
    if (frame) {
        for (mlir::OpOperand & use : frame.getContext().getUses()) {
            if (!step() || use.getOperandNumber() != 0 || !frameUses.contains(use.getOwner()) ||
                !dominance.dominates(frame.getContext(), use.getOwner())) {
                return false;
            }
        }
    }
    for (mlir::BlockArgument parameter : parameters.objectKeys) {
        for (mlir::OpOperand & use : parameter.getUses()) {
            if (!step() || !dominance.dominates(parameter, use.getOwner())) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (identities.contains(use.getOwner()) ||
                llvm::isa<ctjs::TruthyOp, ctjs::UnaryOp, mlir::scf::YieldOp>(use.getOwner())) {
                continue;
            }
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                             : ctjs::GetPropertyOp{};
            if (!read || !calls.contains(call) ||
                (use.getOperandNumber() != 2 &&
                 (ctjs::constantKey(read.getKey()) != "set" || use.getOperandNumber() != 3))) {
                return false;
            }
        }
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
    for (const auto & [alias, origin] : maps) {
        for (mlir::OpOperand & use : alias.getUses()) {
            if (!step()) { return false; }
            if (!dominance.dominates(alias, use.getOwner())) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                (reads.contains(use.getOwner()) && use.getOperandNumber() == 0) ||
                (calls.contains(use.getOwner()) && use.getOperandNumber() == 1) ||
                (origin != capturedOrigin && mapStores.contains(use.getOwner()) &&
                 use.getOperandNumber() == 3)) {
                continue;
            }
            return false;
        }
    }
    for (mlir::Operation * operation : constructors) {
        auto constructor = llvm::cast<ctjs::LoadGlobalOp>(operation);
        for (mlir::OpOperand & use : constructor.getResult().getUses()) {
            if (!step() || !dominance.dominates(constructor.getResult(), use.getOwner())) {
                return false;
            }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto made = llvm::dyn_cast<ctjs::ConstructOp>(use.getOwner());
            if (!made || !allocations.contains(made) ||
                (use.getOperandNumber() != 0 && use.getOperandNumber() != 1)) {
                return false;
            }
        }
    }
    // Scalar read-time facts, including an absent get's Undefined result,
    // cannot cross their source scope through malformed live SSA edits. The
    // same requirement protects primitive key evidence and branch conditions.
    for (const auto * values : {&primitives, &flags, &leafValues}) {
        for (mlir::Value value : *values) {
            for (mlir::OpOperand & use : value.getUses()) {
                if (!step() || !dominance.dominates(value, use.getOwner())) { return false; }
            }
        }
    }
    // A method-local object is a leaf owner, retained only by checked Maps.
    // Check every use, including uses outside the
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
        if (ctjs::constantKey(read.getKey()) == "size") { continue; }
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
    if (copies) {
        // The timing helper permits GC roots, but even bookkeeping cannot
        // carry a then-local iterator into a sibling region or function.
        for (mlir::Operation * operation : calls) {
            auto call = llvm::cast<ctjs::CallOp>(operation);
            auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!step()) { return false; }
            if (ctjs::constantKey(read.getKey()) != "keys") { continue; }
            for (mlir::OpOperand & use : call.getResult().getUses()) {
                if (!step() || !dominance.dominates(call.getResult(), use.getOwner())) {
                    return false;
                }
            }
        }
        for (mlir::Operation * operation : result.snapshotOperations) {
            if (!step()) { return false; }
            if (operation->getParentOfType<ctjs::FuncOp>() != function) { continue; }
            for (mlir::Value operand : operation->getOperands()) {
                if (!step() || !dominance.dominates(operand, operation)) { return false; }
            }
        }
        for (mlir::Operation * copy : copies->calls) {
            for (mlir::OpOperand & use : copy->getResult(0).getUses()) {
                if (!step() || !dominance.dominates(copy->getResult(0), use.getOwner())) {
                    return false;
                }
                if (!llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                    !(use.getOperandNumber() == 0 && snapshotReads.contains(use.getOwner()))) {
                    return false;
                }
            }
        }
    }
    // A key may itself be a caller object. Equality observes identity without
    // granting a primitive category, field receiver, return or storage edge.
    for (mlir::Value element : snapshotElements) {
        for (mlir::OpOperand & use : element.getUses()) {
            if (!step() || !dominance.dominates(element, use.getOwner())) { return false; }
            if (!llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                !(identities.contains(use.getOwner()) && use.getOperandNumber() < 2)) {
                return false;
            }
        }
    }
    // Unproved Map.get results remain potentially nullable/mixed. Even a
    // local finite result still needs the independent native Map schema and
    // presence analyses before a consuming formal can acquire a C++ carrier.
    returnAlternatives = alternatives.lookup(returned.getValue());
    return true;
}

} // namespace ctcompile::ctnative::host_detail
