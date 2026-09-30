#include "Analysis.h"
#include "CapturedMapBody/Walk.hpp"

#include "../Analysis/NativeMap/SnapshotCopies.h"
#include "../Analysis/PrimitiveMapKey.h"
#include "ctcompile/CTNative/Analysis/PrimitiveAlternatives.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

#include <algorithm>
#include <cmath>

namespace ctcompile::ctnative::host_detail {

bool analyzer::capturedMapBody(ctjs::FuncOp function, bool prepared, bool primitiveContents,
                               const HostMethodParameters & parameters, HostCapturedMap & result,
                               PrimitiveAlternatives & returnAlternatives,
                               CapturedMapInvocation * invocation) {
    return captured_map_detail::Walk(*this, function, prepared, primitiveContents, parameters,
                                     result, returnAlternatives, invocation)
        .run();
}

} // namespace ctcompile::ctnative::host_detail

namespace ctcompile::ctnative::host_detail::captured_map_detail {

bool Walk::prepareSnapshots() {
    if (copies) { return true; }
    if (!llvm::is_contained(owner.contract.initialIntrinsics, "Array") ||
        !owner.globals["Array"].empty()) {
        return false;
    }
    llvm::DenseSet<mlir::Operation *> candidates;
    const auto census = function.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * operation) {
        if (!owner.step()) { return mlir::WalkResult::interrupt(); }
        auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
        if (!call) { return mlir::WalkResult::advance(); }
        auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (read && ctjs::constantKey(read.getKey()) == "keys") { candidates.insert(call); }
        return mlir::WalkResult::advance();
    });
    map_detail::snapshotCopies found;
    if (census.wasInterrupted() ||
        !map_detail::collectSnapshotCopies(function, candidates, found, [&] {
             return owner.step();
         }).empty()) {
        return false;
    }
    // Candidates have only an iterator timing proof. The walk below must
    // still close every receiver/effect before this family can be published.
    copies = std::move(found);
    return true;
}

bool Walk::chargeStates(const llvm::DenseMap<CapturedMapOrigin, CapturedMapState> & states) {
    uint64_t cost = states.size();
    for (const auto & item : states) {
        const auto & state = item.second;
        cost += state.entries.size() + state.observations.size() + state.possibleKeys.size();
    }
    return owner.spend(cost);
}

mlir::Value Walk::actualKey(mlir::Value value) {
    if (invocation) {
        if (auto actual = invocation->arguments.lookup(value)) { return actual; }
    }
    return value;
}

// A method-local SSA producer is not stable across calls. Explicit entry
// inputs are stable origins, but different parameters may alias each other.
bool Walk::stableKey(mlir::Value key) {
    return owner.elementInput(key) || key.getDefiningOp<ctjs::ConstantOp>() ||
           (key.getDefiningOp() && key.getDefiningOp()->getParentOp() == owner.entry);
}

PrimitiveMapKeyRelation Walk::keyRelation(mlir::Value left, mlir::Value right,
                                          PrimitiveMapKeyEvidence leftEvidence,
                                          PrimitiveMapKeyEvidence rightEvidence) {
    left = actualKey(left);
    right = actualKey(right);
    const auto relation = comparePrimitiveMapKeys(left, right, leftEvidence, rightEvidence);
    if (!invocation || relation != PrimitiveMapKeyRelation::Unknown) { return relation; }
    if (owner.elementInput(left) && owner.elementInput(right)) {
        if (!owner.step()) { return PrimitiveMapKeyRelation::Unknown; }
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
        return made && made->getParentOp() == owner.entry &&
               owner.dominance.properlyDominates(made.getOperation(), invocation->call);
    };
    const auto primitiveKey = [](mlir::Value value, PrimitiveMapKeyEvidence evidence) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return evidence.tag.has_value() || (constant && ctjs::isPrimitiveAttr(constant.getValue()));
    };
    if ((entryObject(left) && (entryObject(right) || primitiveKey(right, rightEvidence))) ||
        (entryObject(right) && primitiveKey(left, leftEvidence))) {
        return PrimitiveMapKeyRelation::Distinct;
    }
    return relation;
}

PrimitiveMapKeyEvidence Walk::keyEvidence(mlir::Value key) {
    key = actualKey(key);
    const auto found = exactSizes.find(key);
    return PrimitiveMapKeyEvidence{alternatives.lookup(key).tag(), sizeBounds.lookup(key),
                                   found == exactSizes.end() ? std::nullopt
                                                             : std::optional(found->second)};
}

std::optional<size_fact> Walk::sizeFacts(const CapturedMapState & state) {
    if (!owner.step()) { return std::nullopt; }
    if (state.currentSize) { return size_fact{*state.currentSize, state.currentSize}; }
    llvm::SmallVector<mlir::Value> distinct;
    for (const auto & entry :
         llvm::ArrayRef<entry_fact>(state.entries).take_front(kMaxPrimitiveMapSizeCandidates)) {
        if (!owner.step()) { return std::nullopt; }
        if (!entry.present) { continue; }
        bool disjoint = true;
        for (mlir::Value previous : distinct) {
            if (!owner.step()) { return std::nullopt; }
            if (pathRelation(previous, entry.key) != PrimitiveMapKeyRelation::Distinct) {
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
            if (!owner.step()) { return std::nullopt; }
            bool same = false;
            for (mlir::Value previous : possible) {
                if (!owner.step()) { return std::nullopt; }
                if (pathRelation(previous, key) == PrimitiveMapKeyRelation::Same) {
                    same = true;
                    break;
                }
            }
            if (!same) { possible.push_back(key); }
        }
        if (possible.size() == distinct.size()) { fact.exact = fact.lower; }
    }
    return fact;
}

std::optional<bool> Walk::absent(mlir::Value key, llvm::ArrayRef<CapturedMapEntry> facts,
                                 bool complete, llvm::ArrayRef<mlir::Value> possible,
                                 bool currentPath) {
    for (const auto & entry : facts) {
        if (!owner.step()) { return std::nullopt; }
        const auto relation =
            currentPath ? pathRelation(entry.key, key) : keyRelation(entry.key, key);
        if (relation == PrimitiveMapKeyRelation::Same) { return entry.absent; }
    }
    if (!complete) { return false; }
    for (mlir::Value written : possible) {
        if (!owner.step()) { return std::nullopt; }
        // Cross-arm queries cannot use the other arm's filtered scalar
        // alternatives. Exact source constants/SSA equality are path-free.
        const auto relation = currentPath ? pathRelation(written, key) : keyRelation(written, key);
        if (relation != PrimitiveMapKeyRelation::Distinct) { return false; }
    }
    return true;
}

bool Walk::mutate(CapturedMapState & state, mlir::Value key, bool erase,
                  PrimitiveAlternatives payload, mlir::Value object, CapturedMapOrigin child) {
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
            if (!owner.step()) { return false; }
            if (entry.present && pathRelation(entry.key, key) == PrimitiveMapKeyRelation::Same) {
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
        if (!owner.step()) { return false; }
        const auto relation = pathRelation(it->key, key);
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
            if (!owner.step()) { return false; }
            auto call = llvm::cast<ctjs::CallOp>(observed);
            auto observedKey = call.getArgs()[0];
            if (pathRelation(observedKey, key) != PrimitiveMapKeyRelation::Distinct) {
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
                if (!owner.step()) { return false; }
                if (pathRelation(possible, key) != PrimitiveMapKeyRelation::Same) {
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
            if (!owner.step()) { return false; }
            state.possibleKeys.push_back(key);
        }
    }
    if (state.possibleKeys.size() <= kMaxPrimitiveMapSizeCandidates) {
        state.currentSize = nextSize;
    }
    return true;
}

bool Walk::learn(mlir::Value condition, bool branch) {
    while (true) {
        if (auto truthy = condition.getDefiningOp<ctjs::TruthyOp>()) {
            if (!owner.step()) { return false; }
            condition = truthy.getValue();
        } else if (auto unary = condition.getDefiningOp<ctjs::UnaryOp>();
                   unary && unary.getKind() == ctjs::UnaryKind::Not) {
            if (!owner.step()) { return false; }
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
            if (!owner.step()) { return false; }
            auto literal = compare->getOperand(index).getDefiningOp<ctjs::ConstantOp>();
            if (!literal || !llvm::isa<ctjs::NullAttr, ctjs::UndefinedAttr>(literal.getValue())) {
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
        if (!owner.step()) { return false; }
        if (keyRelation(entry.key, key) == PrimitiveMapKeyRelation::Same) {
            entry.present = true;
            entry.absent = false;
            return true;
        }
    }
    state.entries.push_back({key, {}, true, {}});
    return true;
}

bool Walk::invalidateAliases(CapturedMapOrigin origin) {
    if (origin == capturedOrigin) { return true; }
    for (auto & [other, state] : mapStates) {
        if (!owner.step()) { return false; }
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
}

std::optional<bool> Walk::knownTruth(mlir::Value value, unsigned depth) {
    if (depth == 32 || !owner.step()) { return {}; }
    if (exactLeaves.contains(value)) { return true; }
    if (auto truthy = value.getDefiningOp<ctjs::TruthyOp>()) {
        return knownTruth(truthy.getValue(), depth + 1);
    }
    if (auto unary = value.getDefiningOp<ctjs::UnaryOp>();
        unary && unary.getKind() == ctjs::UnaryKind::Not) {
        const auto truth = knownTruth(unary.getOperand(), depth + 1);
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
        if ((lhs && *lhs < sizeBounds.lookup(right)) || (rhs && *rhs < sizeBounds.lookup(left))) {
            return false;
        }
    }
    const auto fact = alternatives.lookup(value);
    if (fact.known && fact.truthy && !fact.falsy) { return true; }
    if (fact.known && fact.falsy && !fact.truthy) { return false; }
    return {};
}

bool Walk::closedUses(mlir::Value value, llvm::function_ref<bool(mlir::OpOperand &)> allowed) {
    for (mlir::OpOperand & use : value.getUses()) {
        if (!owner.step() || !owner.dominance.dominates(value, use.getOwner()) || !allowed(use)) {
            return false;
        }
    }
    return true;
}

bool Walk::run() {
    // This is an effects and ownership proof, not an evaluation of the first
    // invocation. The immutable slot always denotes this Map; its contents
    // may change at every call. A complete body census closes all writes over
    // primitives, fresh leaf objects and fresh child Maps. Child Maps cannot
    // retain Maps. Mixed child results may return owning values, without
    // primitive or field authority; no accessor can execute.
    // The native identity, field and Map analyses still independently prove
    // their carriers.
    uint64_t callbackOperations = 0;
    for (const auto & callback : result.scalarCallbacks) {
        callbackOperations += callback.loads.size() + callback.reads.size() + callback.calls.size();
    }
    if (!owner.spend(callbackOperations)) { return false; }
    for (const auto & callback : result.scalarCallbacks) {
        callbackReads.insert(callback.loads.begin(), callback.loads.end());
        callbackReads.insert(callback.reads.begin(), callback.reads.end());
        callbackCalls.insert(callback.calls.begin(), callback.calls.end());
    }
    if (invocation) {
        if (!chargeStates(invocation->states)) { return false; }
        mapStates = invocation->states;
    }
    mapStates.try_emplace(capturedOrigin);
    if (prepared) { maps.try_emplace(body.getArgument(3), capturedOrigin); }
    const unsigned offset = prepared ? 4u : 3u;
    if (parameters.function != function ||
        parameters.alternatives.size() != body.getNumArguments() - offset) {
        return false;
    }
    for (mlir::BlockArgument parameter : body.getArguments().drop_front(offset)) {
        if (!owner.step()) { return false; }
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
        scalar(parameter, parameters.alternatives[parameter.getArgNumber() - offset]);
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
    if (!walk(body, 0)) { return false; }
    if (!returned || result.reads.size() == firstRead ||
        (!prepared && result.upvalues.size() == firstUpvalue)) {
        return false;
    }
    if (invocation) {
        if (!chargeStates(mapStates)) { return false; }
        for (auto & [origin, state] : mapStates) {
            (void)origin;
            for (const auto & fact : state.entries) {
                if (!owner.step() || !stableKey(fact.key)) { return false; }
            }
            for (mlir::Value key : state.possibleKeys) {
                if (!owner.step() || !stableKey(key)) { return false; }
            }
            state.observations.clear();
        }
        invocation->states = std::move(mapStates);
        invocation->returnedLeaf = exactLeaves.lookup(returned.getValue());
        returnAlternatives = alternatives.lookup(returned.getValue());
        return true;
    }
    const auto anyUse = [](mlir::OpOperand &) { return true; };
    if (frame && !closedUses(frame.getContext(), [&](mlir::OpOperand & use) {
            return use.getOperandNumber() == 0 && frameUses.contains(use.getOwner());
        })) {
        return false;
    }
    for (mlir::BlockArgument parameter : parameters.objectKeys) {
        if (!closedUses(parameter, [&](mlir::OpOperand & use) {
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { return true; }
                if (identities.contains(use.getOwner()) ||
                    llvm::isa<ctjs::TruthyOp, ctjs::UnaryOp, mlir::scf::YieldOp>(use.getOwner())) {
                    return true;
                }
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                 : ctjs::GetPropertyOp{};
                return read && calls.contains(call) &&
                       (use.getOperandNumber() == 2 ||
                        (ctjs::constantKey(read.getKey()) == "set" && use.getOperandNumber() == 3));
            })) {
            return false;
        }
    }
    for (mlir::BlockArgument argument : body.getArguments()) {
        if (argument.getArgNumber() >= offset) { continue; }
        if (prepared && argument.getArgNumber() == 3) { continue; }
        for (mlir::OpOperand & use : argument.getUses()) {
            if (!owner.step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                (!prepared && argument.getArgNumber() == 2 && upvalues.contains(use.getOwner()) &&
                 use.getOperandNumber() == 0)) {
                continue;
            }
            return false;
        }
    }
    for (const auto & [alias, origin] : maps) {
        if (!closedUses(alias, [&](mlir::OpOperand & use) {
                return llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                       (reads.contains(use.getOwner()) && use.getOperandNumber() == 0) ||
                       (calls.contains(use.getOwner()) && use.getOperandNumber() == 1) ||
                       (origin != capturedOrigin && mapStores.contains(use.getOwner()) &&
                        use.getOperandNumber() == 3);
            })) {
            return false;
        }
    }
    for (mlir::Operation * operation : constructors) {
        if (!closedUses(llvm::cast<ctjs::LoadGlobalOp>(operation).getResult(),
                        [&](mlir::OpOperand & use) {
                            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { return true; }
                            auto made = llvm::dyn_cast<ctjs::ConstructOp>(use.getOwner());
                            return made && allocations.contains(made) &&
                                   (use.getOperandNumber() == 0 || use.getOperandNumber() == 1);
                        })) {
            return false;
        }
    }
    // Scalar read-time facts, including an absent get's Undefined result,
    // cannot cross their source scope through malformed live SSA edits. The
    // same requirement protects primitive key evidence and branch conditions.
    for (const auto * values : {&primitives, &flags, &leafValues}) {
        for (mlir::Value value : *values) {
            if (!closedUses(value, anyUse)) { return false; }
        }
    }
    // A method-local object is a leaf owner, retained only by checked Maps.
    // Check every use, including uses outside the
    // walked body. Neither native markers nor provider allocation tokens can
    // authorize a field, alias, escape or future invocation here.
    // Live analysis queries may follow edits before MLIR verification.
    // A then-only allocation/read cannot authorize an else or outer
    // use merely because its immutable origin was visited first.
    for (const auto & [object, origin] : objects) {
        (void)origin;
        if (!closedUses(object, [&](mlir::OpOperand & use) {
                return llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                       (objectWrites.contains(use.getOwner()) && use.getOperandNumber() == 0) ||
                       (objectReads.contains(use.getOwner()) && use.getOperandNumber() == 0) ||
                       (identities.contains(use.getOwner()) && use.getOperandNumber() < 2) ||
                       (objectStores.contains(use.getOwner()) && use.getOperandNumber() == 3);
            })) {
            return false;
        }
    }
    for (std::size_t index = firstRead; index < result.reads.size(); ++index) {
        auto read = result.reads[index];
        if (!owner.step()) { return false; }
        if (ctjs::constantKey(read.getKey()) == "size") { continue; }
        if (!closedUses(read.getResult(), [&](mlir::OpOperand & use) {
                return llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                       (calls.contains(use.getOwner()) && use.getOperandNumber() == 0);
            })) {
            return false;
        }
    }
    if (copies) {
        // The timing helper permits GC roots, but even bookkeeping cannot
        // carry a then-local iterator into a sibling region or function.
        for (mlir::Operation * operation : calls) {
            auto call = llvm::cast<ctjs::CallOp>(operation);
            auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!owner.step()) { return false; }
            if (ctjs::constantKey(read.getKey()) != "keys") { continue; }
            if (!closedUses(call.getResult(), anyUse)) { return false; }
        }
        for (mlir::Operation * operation : result.snapshotOperations) {
            if (!owner.step()) { return false; }
            if (operation->getParentOfType<ctjs::FuncOp>() != function) { continue; }
            for (mlir::Value operand : operation->getOperands()) {
                if (!owner.step() || !owner.dominance.dominates(operand, operation)) {
                    return false;
                }
            }
        }
        for (mlir::Operation * copy : copies->calls) {
            if (!closedUses(copy->getResult(0), [&](mlir::OpOperand & use) {
                    return llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                           (use.getOperandNumber() == 0 && snapshotReads.contains(use.getOwner()));
                })) {
                return false;
            }
        }
    }
    // A key may itself be a caller object. Equality observes identity without
    // granting a primitive category, field receiver, return or storage edge.
    // Independently closed String keys additionally permit checked Concat only;
    // nullable reads still format Undefined through the existing string lowering.
    for (mlir::Value element : snapshotElements) {
        if (!closedUses(element, [&](mlir::OpOperand & use) {
                return llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                       (identities.contains(use.getOwner()) && use.getOperandNumber() < 2) ||
                       (stringSnapshotElements.contains(element) &&
                        concatenations.contains(use.getOwner()) && use.getOperandNumber() < 2);
            })) {
            return false;
        }
    }
    // Unproved Map.get results remain potentially nullable/mixed. Even a
    // local finite result still needs the independent native Map schema and
    // presence analyses before a consuming formal can acquire a C++ carrier.
    returnAlternatives = alternatives.lookup(returned.getValue());
    return true;
}

} // namespace ctcompile::ctnative::host_detail::captured_map_detail
