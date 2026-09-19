#include "Analysis.h"
#include "CapturedMapBody/Walk.hpp"

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
                               PrimitiveAlternatives & returnAlternatives,
                               CapturedMapInvocation * invocation) {
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
    llvm::DenseSet<mlir::Value> stringSnapshotElements;
    llvm::DenseSet<mlir::Operation *> concatenations;
    llvm::DenseSet<mlir::Operation *> snapshotReads;
    llvm::DenseSet<mlir::Operation *> callbackReads, callbackCalls;
    for (const auto & callback : result.scalarCallbacks) {
        for (ctjs::LoadGlobalOp load : callback.loads) {
            if (!step()) { return false; }
            callbackReads.insert(load);
        }
        for (ctjs::GetPropertyOp read : callback.reads) {
            if (!step()) { return false; }
            callbackReads.insert(read);
        }
        for (mlir::Operation * call : callback.calls) {
            if (!step()) { return false; }
            callbackCalls.insert(call);
        }
    }
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
    llvm::DenseMap<mlir::Value, CapturedMapOrigin> maps;
    const CapturedMapOrigin capturedOrigin{
        invocation ? invocation->root : body.getArgument(prepared ? 3 : 2), nullptr};
    llvm::DenseMap<mlir::Value, mlir::Value> exactLeaves;
    llvm::DenseSet<mlir::Operation *> constructors, allocations, mapStores;
    // A saved read names its allocation, not the current Map entry. These SSA
    // origins never change when that entry is overwritten or deleted.
    llvm::DenseMap<mlir::Value, mlir::Value> objects;
    llvm::DenseSet<mlir::Operation *> objectWrites, objectReads, objectStores, identities;
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> alternatives;
    using field_fact = captured_map_detail::field_fact;
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
    using entry_fact = CapturedMapEntry;
    using map_state = CapturedMapState;
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
    llvm::DenseMap<CapturedMapOrigin, map_state> mapStates;
    if (invocation) {
        if (!chargeStates(invocation->states)) { return false; }
        mapStates = invocation->states;
    }
    mapStates.try_emplace(capturedOrigin);
    const auto actualKey = [&](mlir::Value value) {
        if (invocation) {
            if (auto actual = invocation->arguments.lookup(value)) { return actual; }
        }
        return value;
    };
    // A method-local SSA producer is not stable across calls. Explicit entry
    // inputs are stable origins, but different parameters may alias each other.
    const auto stableKey = [&](mlir::Value key) {
        return elementInput(key) || key.getDefiningOp<ctjs::ConstantOp>() ||
               (key.getDefiningOp() && key.getDefiningOp()->getParentOp() == entry);
    };
    const auto keyRelation = [&](mlir::Value left, mlir::Value right,
                                 PrimitiveMapKeyEvidence leftEvidence = {},
                                 PrimitiveMapKeyEvidence rightEvidence = {}) {
        left = actualKey(left);
        right = actualKey(right);
        const auto relation = comparePrimitiveMapKeys(left, right, leftEvidence, rightEvidence);
        if (!invocation || relation != PrimitiveMapKeyRelation::Unknown) { return relation; }
        if (elementInput(left) && elementInput(right)) {
            if (!step()) { return PrimitiveMapKeyRelation::Unknown; }
            const auto a = invocation->elementClasses.find(left);
            const auto b = invocation->elementClasses.find(right);
            if (a != invocation->elementClasses.end() && b != invocation->elementClasses.end()) {
                return a->second == b->second ? PrimitiveMapKeyRelation::Same
                                              : PrimitiveMapKeyRelation::Distinct;
            }
        }
        // Entry allocations identify this activation's actual objects. Different
        // loads, method-local allocation sites and carrier schemas do not.
        const auto entryObject = [&](mlir::Value value) {
            auto made = value.getDefiningOp<ctjs::CreateObjectOp>();
            return made && made->getParentOp() == entry &&
                   dominance.properlyDominates(made.getOperation(), invocation->call);
        };
        const auto primitiveKey = [](mlir::Value value, PrimitiveMapKeyEvidence evidence) {
            auto constant = value.getDefiningOp<ctjs::ConstantOp>();
            return evidence.tag.has_value() ||
                   (constant && ctjs::isPrimitiveAttr(constant.getValue()));
        };
        if ((entryObject(left) && (entryObject(right) || primitiveKey(right, rightEvidence))) ||
            (entryObject(right) && primitiveKey(left, leftEvidence))) {
            return PrimitiveMapKeyRelation::Distinct;
        }
        return relation;
    };
    // Mutable state belongs to a runtime origin, never a shared C++ schema.
    // Captured contents start unknown on every invocation; a fresh child starts
    // empty. Saved aliases continue to denote that child after outer mutation.
    llvm::DenseMap<mlir::Value, unsigned> sizeBounds;
    llvm::DenseMap<mlir::Value, unsigned> exactSizes;
    const auto primitiveTag = [&](mlir::Value key) -> std::optional<mlir::TypeID> {
        return alternatives.lookup(key).tag();
    };
    const auto keyEvidence = [&](mlir::Value key) {
        key = actualKey(key);
        const auto found = exactSizes.find(key);
        return PrimitiveMapKeyEvidence{primitiveTag(key), sizeBounds.lookup(key),
                                       found == exactSizes.end() ? std::nullopt
                                                                 : std::optional(found->second)};
    };
    using size_fact = captured_map_detail::size_fact;
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
                if (keyRelation(previous, entry.key, keyEvidence(previous),
                                keyEvidence(entry.key)) != PrimitiveMapKeyRelation::Distinct) {
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
                    if (keyRelation(previous, key, keyEvidence(previous), keyEvidence(key)) ==
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
                currentPath ? keyRelation(entry.key, key, keyEvidence(entry.key), keyEvidence(key))
                            : keyRelation(entry.key, key);
            if (relation == PrimitiveMapKeyRelation::Same) { return entry.absent; }
        }
        if (!complete) { return false; }
        for (mlir::Value written : possible) {
            if (!step()) { return std::nullopt; }
            // Cross-arm queries cannot use the other arm's filtered scalar
            // alternatives. Exact source constants/SSA equality are path-free.
            const auto relation =
                currentPath ? keyRelation(written, key, keyEvidence(written), keyEvidence(key))
                            : keyRelation(written, key);
            if (relation != PrimitiveMapKeyRelation::Distinct) { return false; }
        }
        return true;
    };
    const auto mutate = [&](map_state & state, mlir::Value key, bool erase,
                            PrimitiveAlternatives payload = {}, mlir::Value object = {},
                            CapturedMapOrigin child = {}) {
        key = actualKey(key);
        if (invocation && !stableKey(key)) { return false; }
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
                    keyRelation(entry.key, key, keyEvidence(entry.key), keyEvidence(key)) ==
                        PrimitiveMapKeyRelation::Same) {
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
            const auto relation = keyRelation(it->key, key, keyEvidence(it->key), keyEvidence(key));
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
                if (keyRelation(observedKey, key, keyEvidence(observedKey), keyEvidence(key)) !=
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
                    if (keyRelation(possible, key, keyEvidence(possible), keyEvidence(key)) !=
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
        auto key = actualKey(call.getArgs()[0]);
        for (auto & entry : state.entries) {
            if (!step()) { return false; }
            if (keyRelation(entry.key, key) == PrimitiveMapKeyRelation::Same) {
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
            if (invocation) {
                auto actual = invocation->arguments.lookup(parameter);
                if (actual && actual.getDefiningOp<ctjs::CreateObjectOp>()) {
                    exactLeaves[parameter] = actual;
                }
            }
            continue;
        }
        primitives.insert(parameter);
        alternatives.try_emplace(parameter,
                                 parameters.alternatives[parameter.getArgNumber() - offset]);
        if (invocation) {
            const auto actual = invocation->arguments.lookup(parameter);
            if (actual) {
                if (auto literal = actual.getDefiningOp<ctjs::ConstantOp>()) {
                    alternatives[parameter] = PrimitiveAlternatives::literal(literal.getValue());
                }
                alternatives[actual] = alternatives.lookup(parameter);
            }
        }
    }
    const auto invalidateAliases = [&](CapturedMapOrigin origin) {
        if (origin == capturedOrigin) { return true; }
        for (auto & [other, state] : mapStates) {
            if (!step()) { return false; }
            if (other == capturedOrigin || other == origin ||
                (origin.first.getDefiningOp<ctjs::ConstructOp>() &&
                 other.first.getDefiningOp<ctjs::ConstructOp>())) {
                continue;
            }
            // A returned owner may alias another child, including a child
            // published on only one branch. Fresh constructors alone are
            // disjoint. Keep saved identities, discard unproved mutable facts.
            state = {};
        }
        return true;
    };
    const auto knownTruth = [&](auto && self, mlir::Value value,
                                unsigned depth = 0) -> std::optional<bool> {
        if (depth == 32 || !step()) { return {}; }
        if (exactLeaves.contains(value)) { return true; }
        if (auto truthy = value.getDefiningOp<ctjs::TruthyOp>()) {
            return self(self, truthy.getValue(), depth + 1);
        }
        if (auto unary = value.getDefiningOp<ctjs::UnaryOp>();
            unary && unary.getKind() == ctjs::UnaryKind::Not) {
            const auto truth = self(self, unary.getOperand(), depth + 1);
            return truth ? std::optional(!*truth) : std::nullopt;
        }
        if (auto constant = value.getDefiningOp<mlir::arith::ConstantOp>();
            constant && constant.getType().isInteger(1)) {
            return !llvm::cast<mlir::IntegerAttr>(constant.getValue()).getValue().isZero();
        }
        if (auto compare = value.getDefiningOp<ctjs::CompareOp>();
            compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
            const auto left = actualKey(compare.getLhs()), right = actualKey(compare.getRhs());
            const auto lhs = primitiveMapSize(left, keyEvidence(left));
            const auto rhs = primitiveMapSize(right, keyEvidence(right));
            // These read-time facts describe finite nonnegative integers, so
            // strict equality needs neither coercion nor SameValueZero's NaN rule.
            if (lhs && rhs) { return *lhs == *rhs; }
            if ((lhs && *lhs < sizeBounds.lookup(right)) ||
                (rhs && *rhs < sizeBounds.lookup(left))) {
                return false;
            }
        }
        const auto fact = alternatives.lookup(value);
        if (fact.known && fact.truthy && !fact.falsy) { return true; }
        if (fact.known && fact.falsy && !fact.truthy) { return false; }
        return {};
    };
    ctjs::ReturnOp returned;
    ctjs::FrameEnterOp frame;
    llvm::DenseSet<mlir::Operation *> frameUses;
    bool frameExited = false;
    const auto knownTruthValue = [&](mlir::Value value) { return knownTruth(knownTruth, value); };
    captured_map_detail::Walk visitor{*this,
                                      body,
                                      prepared,
                                      primitiveContents,
                                      parameters,
                                      result,
                                      invocation,
                                      primitives,
                                      flags,
                                      leafValues,
                                      snapshotElements,
                                      stringSnapshotElements,
                                      concatenations,
                                      snapshotReads,
                                      callbackReads,
                                      callbackCalls,
                                      constructors,
                                      allocations,
                                      mapStores,
                                      objectWrites,
                                      objectReads,
                                      objectStores,
                                      identities,
                                      reads,
                                      calls,
                                      upvalues,
                                      frameUses,
                                      copies,
                                      maps,
                                      capturedOrigin,
                                      exactLeaves,
                                      objects,
                                      alternatives,
                                      fields,
                                      mapStates,
                                      sizeBounds,
                                      exactSizes,
                                      returned,
                                      frame,
                                      frameExited,
                                      prepareSnapshots,
                                      chargeStates,
                                      keyEvidence,
                                      sizeFacts,
                                      learn,
                                      invalidateAliases,
                                      knownTruthValue,
                                      keyRelation,
                                      absent,
                                      mutate};
    if (!visitor.walk(body, 0)) { return false; }
    if (!returned || result.reads.size() == firstRead ||
        (!prepared && result.upvalues.size() == firstUpvalue)) {
        return false;
    }
    if (invocation) {
        if (!chargeStates(mapStates)) { return false; }
        for (auto & [origin, state] : mapStates) {
            (void)origin;
            for (const auto & fact : state.entries) {
                if (!step() || !stableKey(fact.key)) { return false; }
            }
            for (mlir::Value key : state.possibleKeys) {
                if (!step() || !stableKey(key)) { return false; }
            }
            state.observations.clear();
        }
        invocation->states = std::move(mapStates);
        invocation->returnedLeaf = exactLeaves.lookup(returned.getValue());
        returnAlternatives = alternatives.lookup(returned.getValue());
        return true;
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
    // Independently closed String keys additionally permit checked Concat only;
    // nullable reads still format Undefined through the existing string lowering.
    for (mlir::Value element : snapshotElements) {
        for (mlir::OpOperand & use : element.getUses()) {
            if (!step() || !dominance.dominates(element, use.getOwner())) { return false; }
            if (!llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                !(identities.contains(use.getOwner()) && use.getOperandNumber() < 2) &&
                !(stringSnapshotElements.contains(element) &&
                  concatenations.contains(use.getOwner()) && use.getOperandNumber() < 2)) {
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
