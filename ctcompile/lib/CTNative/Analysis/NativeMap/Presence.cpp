//===- Presence.cpp - structured must-analysis for nested Map reads -------===//
#include "Presence.h"

#include "../PrimitiveAlternatives.h"
#include "../PrimitiveMapKey.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>

namespace ctcompile::ctnative::map_detail {
namespace {

llvm::StringRef actionOf(ctjs::CallOp call) {
    auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    if (!get) { return {}; }
    auto constant = get.getKey().getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return {}; }
    auto text = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
    return text ? text.getValue() : llvm::StringRef{};
}

// This analysis also runs from CTJS-only binding-time and partial-evaluation
// callers. Keep semantic tags independent of native dialect type storage.
enum class payloadKind {
    Unknown,
    Boolean,
    Number,
    String
};

struct fact {
    mlir::Value instance;
    mlir::Value key;
    PrimitiveAlternatives payload;
    // Payload is valid whenever present, independently of definite membership.
    // Deletion cannot change a surviving value, so it clears only this flag.
    bool present = true;
    mlir::Value storedOrigin{};
    bool matches(const fact & other) const {
        return instance == other.instance &&
               comparePrimitiveMapKeys(key, other.key) == PrimitiveMapKeyRelation::Same;
    }
};

struct state {
    llvm::SmallVector<fact> entries;
    // A has result is a snapshot. Erasure invalidates its implication even
    // though its SSA boolean remains available to later conditions.
    llvm::DenseSet<mlir::Operation *> observations;
    // Bounds on already-read SSA numbers survive known mutations. They are
    // acquired only from a checked size read with exact-instance presence.
    llvm::DenseMap<mlir::Value, unsigned> sizeBounds{};
    // A complete possible-key census belongs to one runtime instance, never
    // its schema family or an intersected entry list. Absence from this map
    // means unknown contents; an empty vector independently proves emptiness.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> possibleKeys{};
    llvm::DenseMap<mlir::Value, unsigned> exactSizes{};
    // Mutable cardinality of an actual runtime instance. Equal structural
    // arms can preserve it without sharing any definite key. Only independently
    // proved mutation effects preserve it; already-read SSA exactSizes remain
    // immutable.
    llvm::DenseMap<mlir::Value, unsigned> currentSizes{};
    // A scalar get result owns its value. Unlike membership and entry tags,
    // its type remains true after the source entry is overwritten or erased.
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> scalars{};
    // A definite get copies the stored owner. Its SSA alias outlives replacement
    // or deletion of the outer entry; this never unifies a schema's instances.
    llvm::DenseMap<mlir::Value, mlir::Value> aliases{};

    PrimitiveMapKeyEvidence keyEvidence(mlir::Value value) const {
        const auto found = exactSizes.find(value);
        return {{},
                sizeBounds.lookup(value),
                found == exactSizes.end() ? std::nullopt : std::optional(found->second)};
    }

    struct size_fact {
        unsigned lower = 0;
        std::optional<unsigned> exact;
    };
    size_fact sizeFacts(mlir::Value instance) const {
        if (const auto found = currentSizes.find(instance); found != currentSizes.end()) {
            return {found->second, found->second};
        }
        llvm::SmallVector<mlir::Value> distinct;
        unsigned candidates = 0;
        for (const fact & value : entries) {
            if (!value.present || value.instance != instance) { continue; }
            if (candidates++ == kMaxPrimitiveMapSizeCandidates) { break; }
            if (llvm::all_of(distinct, [&](mlir::Value previous) {
                    return comparePrimitiveMapKeys(previous, value.key, keyEvidence(previous),
                                                   keyEvidence(value.key)) ==
                           PrimitiveMapKeyRelation::Distinct;
                })) {
                distinct.push_back(value.key);
            }
        }
        size_fact result{static_cast<unsigned>(distinct.size()), std::nullopt};
        if (const auto found = possibleKeys.find(instance); found != possibleKeys.end()) {
            llvm::SmallVector<mlir::Value> possible;
            for (mlir::Value key : found->second) {
                if (llvm::none_of(possible, [&](mlir::Value previous) {
                        return comparePrimitiveMapKeys(previous, key, keyEvidence(previous),
                                                       keyEvidence(key)) ==
                               PrimitiveMapKeyRelation::Same;
                    })) {
                    possible.push_back(key);
                }
            }
            if (possible.size() == distinct.size()) { result.exact = result.lower; }
        }
        return result;
    }
    void rememberSizes() {
        for (const auto & [instance, possible] : possibleKeys) {
            if (const auto fact = sizeFacts(instance); fact.exact) {
                currentSizes[instance] = *fact.exact;
            }
        }
    }

    std::optional<unsigned> sizeAfterMutation(fact value, bool erase) const {
        const auto size = currentSizes.find(value.instance);
        const auto possible = possibleKeys.find(value.instance);
        if (size == currentSizes.end() || possible == possibleKeys.end()) { return std::nullopt; }
        // Query the actual instance before mutation. The schema family is
        // only an alias upper bound and cannot prove this key's membership.
        const bool present = llvm::any_of(entries, [&](const fact & old) {
            return old.present && old.instance == value.instance &&
                   comparePrimitiveMapKeys(old.key, value.key, keyEvidence(old.key),
                                           keyEvidence(value.key)) == PrimitiveMapKeyRelation::Same;
        });
        if (present) {
            if (!erase) { return size->second; }
            return size->second ? std::optional(size->second - 1) : std::nullopt;
        }
        const bool absent = llvm::all_of(possible->second, [&](mlir::Value key) {
            return comparePrimitiveMapKeys(key, value.key, keyEvidence(key),
                                           keyEvidence(value.key)) ==
                   PrimitiveMapKeyRelation::Distinct;
        });
        if (!absent) { return std::nullopt; }
        if (erase) { return size->second; }
        return size->second < kMaxPrimitiveMapSizeCandidates ? std::optional(size->second + 1)
                                                             : std::nullopt;
    }

    PrimitiveAlternatives alternatives(mlir::Value value) const {
        if (auto found = scalars.find(value); found != scalars.end()) { return found->second; }
        if (auto constant = value.getDefiningOp<ctjs::ConstantOp>()) {
            return PrimitiveAlternatives::literal(constant.getValue());
        }
        return {};
    }
    payloadKind scalar(mlir::Value value) const {
        const auto tag = alternatives(value).tag();
        if (tag == mlir::TypeID::get<ctjs::BooleanAttr>()) { return payloadKind::Boolean; }
        if (tag == mlir::TypeID::get<ctjs::NumberAttr>()) { return payloadKind::Number; }
        if (tag == mlir::TypeID::get<ctjs::StringAttr>()) { return payloadKind::String; }
        return payloadKind::Unknown;
    }

    bool contains(fact value) const {
        return llvm::any_of(entries, [&](const fact & old) { return old.matches(value); });
    }
    void add(fact value) {
        for (fact & old : entries) {
            if (!old.matches(value)) { continue; }
            old.present |= value.present;
            return;
        }
        entries.push_back(value);
    }
    void intersect(const state & other) {
        for (const auto & [instance, exact] : llvm::make_early_inc_range(currentSizes)) {
            const auto found = other.currentSizes.find(instance);
            if (found == other.currentSizes.end() || exact != found->second) {
                currentSizes.erase(instance);
            }
        }
        for (auto & [instance, possible] : llvm::make_early_inc_range(possibleKeys)) {
            const auto found = other.possibleKeys.find(instance);
            if (found == other.possibleKeys.end() ||
                possible.size() + found->second.size() > kMaxPrimitiveMapSizeCandidates) {
                possibleKeys.erase(instance);
            } else {
                possible.append(found->second);
            }
        }
        for (const auto & [value, exact] : llvm::make_early_inc_range(exactSizes)) {
            const auto found = other.exactSizes.find(value);
            if (found == other.exactSizes.end() || exact != found->second) {
                exactSizes.erase(value);
            }
        }
        llvm::erase_if(entries, [&](const fact & value) { return !other.contains(value); });
        for (fact & value : entries) {
            const auto found = llvm::find_if(
                other.entries, [&](const fact & candidate) { return value.matches(candidate); });
            value.payload = value.payload.joined(found->payload);
            value.present &= found->present;
            if (value.storedOrigin != found->storedOrigin) { value.storedOrigin = {}; }
        }
        for (mlir::Operation * op : llvm::make_early_inc_range(observations)) {
            if (!other.observations.contains(op)) { observations.erase(op); }
        }
        for (auto & bound : llvm::make_early_inc_range(sizeBounds)) {
            const unsigned otherBound = other.sizeBounds.lookup(bound.first);
            if (otherBound == 0) {
                sizeBounds.erase(bound.first);
            } else {
                bound.second = std::min(bound.second, otherBound);
            }
        }
        for (auto & [value, kind] : llvm::make_early_inc_range(scalars)) {
            kind = kind.joined(other.alternatives(value));
            if (!kind.known) { scalars.erase(value); }
        }
        for (const auto & [value, origin] : llvm::make_early_inc_range(aliases)) {
            if (other.aliases.lookup(value) != origin) { aliases.erase(value); }
        }
    }
};

struct effects {
    bool unknown = false;
    llvm::DenseSet<mlir::Value> erased;
    llvm::DenseSet<mlir::Value> written;

    bool merge(const effects & other) {
        bool changed = !unknown && other.unknown;
        unknown |= other.unknown;
        for (mlir::Value family : other.erased) { changed |= erased.insert(family).second; }
        for (mlir::Value family : other.written) { changed |= written.insert(family).second; }
        return changed;
    }
};

struct presenceAnalysis {
    llvm::DenseMap<mlir::Operation *, llvm::StringRef> actions;
    llvm::DenseMap<mlir::Operation *, effects> summaries;
    llvm::DenseSet<mlir::Operation *> proved;
    llvm::DenseMap<mlir::Operation *, PrimitiveAlternatives> payloads;
    llvm::DenseMap<mlir::Operation *, payloadKind> writes, keys;
    llvm::DenseSet<mlir::Operation *> sizes;
    llvm::function_ref<mlir::Value(mlir::Value)> familyOf;
    const llvm::DenseSet<mlir::Operation *> & snapshotCopies;

    presenceAnalysis(llvm::function_ref<mlir::Value(mlir::Value)> family,
                     const llvm::DenseSet<mlir::Operation *> & copies)
        : familyOf(family), snapshotCopies(copies) {}

    // Only fluent set and a definite get of an exact stored origin prove
    // runtime identity. Closed parameter/return edges share only a schema.
    mlir::Value instanceOf(mlir::Value value, const state & current) const {
        while (true) {
            if (const auto origin = current.aliases.lookup(value)) { return origin; }
            auto call = value.getDefiningOp<ctjs::CallOp>();
            if (!call || actions.lookup(call) != "set") { break; }
            value = call.getReceiver();
        }
        return value;
    }
    fact entry(ctjs::CallOp call, const state & current) const {
        return {instanceOf(call.getReceiver(), current), call.getArgs()[0], {}};
    }
    bool mayAlias(mlir::Value left, mlir::Value right) const {
        // The caller checked every constructor; two fresh sites are distinct
        // within this path. Parameters and returned values retain may-alias.
        if (left != right && left.getDefiningOp<ctjs::ConstructOp>() &&
            right.getDefiningOp<ctjs::ConstructOp>()) {
            return false;
        }
        return familyOf(left) == familyOf(right);
    }

    void buildSummaries(mlir::ModuleOp module) {
        llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::FuncOp>> callees;
        module.walk([&](ctjs::FuncOp fn) {
            effects & out = summaries[fn];
            fn.getBody().walk([&](mlir::Operation * op) {
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                    if (snapshotCopies.contains(op)) { return; }
                    const auto action = actions.lookup(op);
                    if (action == "delete" || action == "clear") {
                        out.erased.insert(familyOf(call.getReceiver()));
                    } else if (action == "set") {
                        out.written.insert(familyOf(call.getReceiver()));
                    } else if (action.empty()) {
                        out.unknown = true;
                    }
                } else if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
                    auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                        call, call.getCalleeAttr());
                    if (!target || target.getBody().empty()) {
                        out.unknown = true;
                    } else {
                        callees[fn].push_back(target);
                    }
                }
            });
        });
        // Recursion and mutually recursive helpers must reach the same fixed
        // point: an erase in any reachable callee invalidates the caller.
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto & [fn, called] : callees) {
                for (ctjs::FuncOp target : called) {
                    changed |= summaries[fn].merge(summaries[target]);
                }
            }
        }
    }

    void invalidate(state & current, const effects & effect, mlir::Value instance = {}) const {
        if (effect.unknown) {
            current = {};
            return;
        }
        const auto affected = [&](mlir::Value value, const auto & families) {
            return families.contains(familyOf(value)) && (!instance || mayAlias(instance, value));
        };
        for (const auto & [instance, exact] : llvm::make_early_inc_range(current.currentSizes)) {
            if (affected(instance, effect.written) || affected(instance, effect.erased)) {
                current.currentSizes.erase(instance);
            }
        }
        for (const auto & [instance, possible] : llvm::make_early_inc_range(current.possibleKeys)) {
            if (affected(instance, effect.written) ||
                (!possible.empty() && affected(instance, effect.erased))) {
                current.possibleKeys.erase(instance);
            }
        }
        llvm::erase_if(current.entries,
                       [&](const fact & value) { return affected(value.instance, effect.erased); });
        for (fact & value : current.entries) {
            if (affected(value.instance, effect.written)) {
                value.payload = {};
                value.storedOrigin = {};
            }
        }
        for (mlir::Operation * op : llvm::make_early_inc_range(current.observations)) {
            auto call = llvm::cast<ctjs::CallOp>(op);
            if (affected(instanceOf(call.getReceiver(), current), effect.erased)) {
                current.observations.erase(op);
            }
        }
    }

    void learn(mlir::Value condition, bool branch, state & current) const {
        bool inverted = false;
        while (true) {
            if (auto truthy = condition.getDefiningOp<ctjs::TruthyOp>()) {
                condition = truthy.getValue();
            } else if (auto unary = condition.getDefiningOp<ctjs::UnaryOp>();
                       unary && unary.getKind() == ctjs::UnaryKind::Not) {
                inverted = !inverted;
                condition = unary.getOperand();
            } else {
                break;
            }
        }
        auto alternatives = current.alternatives(condition);
        if (alternatives.known) {
            current.scalars[condition] = alternatives.filtered(branch != inverted);
        }
        auto call = condition.getDefiningOp<ctjs::CallOp>();
        if (call && actions.lookup(call) == "has" && branch != inverted &&
            current.observations.contains(call)) {
            current.add(entry(call, current));
        }
    }

    void eraseKey(state & current, ctjs::CallOp call) const {
        const auto affected = entry(call, current);
        const auto nextSize = current.sizeAfterMutation(affected, true);
        for (const auto & [instance, exact] : llvm::make_early_inc_range(current.currentSizes)) {
            if (mayAlias(instance, affected.instance)) { current.currentSizes.erase(instance); }
        }
        // Only this runtime instance definitely lost the erased key. Other
        // Maps in its schema family may alias it, so their upper bounds remain
        // unchanged while mayErase below invalidates definite membership.
        // Deletion never adds keys, including when equality is unproved.
        if (auto found = current.possibleKeys.find(affected.instance);
            found != current.possibleKeys.end()) {
            llvm::erase_if(found->second, [&](mlir::Value key) {
                return comparePrimitiveMapKeys(key, affected.key, current.keyEvidence(key),
                                               current.keyEvidence(affected.key)) ==
                       PrimitiveMapKeyRelation::Same;
            });
        }
        const auto mayErase = [&](fact value) {
            // A schema family may contain several runtime instances. Without
            // an independent disjointness proof, any of them may be this Map.
            return mayAlias(value.instance, affected.instance) &&
                   comparePrimitiveMapKeys(value.key, affected.key, current.keyEvidence(value.key),
                                           current.keyEvidence(affected.key)) !=
                       PrimitiveMapKeyRelation::Distinct;
        };
        for (fact & value : current.entries) {
            if (mayErase(value)) { value.present = false; }
        }
        for (mlir::Operation * op : llvm::make_early_inc_range(current.observations)) {
            if (mayErase(entry(llvm::cast<ctjs::CallOp>(op), current))) {
                current.observations.erase(op);
            }
        }
        if (nextSize) { current.currentSizes[affected.instance] = *nextSize; }
    }

    // Literal, parameter and saved scalar alternatives do not depend on Map
    // schema inference or prior invocations. Keep a finite nullable payload
    // independently of the broader storage union. Every possible alias joins
    // its alternatives; an unproved write clears them. has supplies no tag.
    void write(state & current, ctjs::CallOp call) const {
        fact added = entry(call, current);
        const auto nextSize = current.sizeAfterMutation(added, false);
        for (const auto & [instance, exact] : llvm::make_early_inc_range(current.currentSizes)) {
            if (mayAlias(instance, added.instance)) { current.currentSizes.erase(instance); }
        }
        for (auto & [instance, possible] : llvm::make_early_inc_range(current.possibleKeys)) {
            if (!mayAlias(instance, added.instance)) { continue; }
            if (instance == added.instance && possible.size() < kMaxPrimitiveMapSizeCandidates) {
                possible.push_back(added.key);
            } else {
                current.possibleKeys.erase(instance);
            }
        }
        added.payload = current.alternatives(call.getArgs()[1]).categories();
        added.storedOrigin = instanceOf(call.getArgs()[1], current);
        for (fact & previous : current.entries) {
            if (!mayAlias(previous.instance, added.instance)) { continue; }
            const auto relation =
                comparePrimitiveMapKeys(previous.key, added.key, current.keyEvidence(previous.key),
                                        current.keyEvidence(added.key));
            if (relation == PrimitiveMapKeyRelation::Distinct) { continue; }
            if (previous.instance == added.instance && relation == PrimitiveMapKeyRelation::Same) {
                previous.payload = added.payload;
                previous.present = true;
                previous.storedOrigin = added.storedOrigin;
            } else {
                previous.payload = previous.payload.joined(added.payload);
                if (previous.storedOrigin != added.storedOrigin) { previous.storedOrigin = {}; }
            }
        }
        current.add(added);
        // The census may have reached its limit while adding this key. Never
        // turn the saved cardinality into a way around that bounded proof.
        if (nextSize && current.possibleKeys.contains(added.instance)) {
            current.currentSizes[added.instance] = *nextSize;
        }
    }

    void region(mlir::Region & body, state & current) {
        if (body.empty()) { return; }
        if (!body.hasOneBlock()) {
            current = {};
            return;
        }
        for (mlir::Operation & op : body.front()) { operation(&op, current); }
    }

    void operation(mlir::Operation * op, state & current) {
        if (sizes.contains(op)) {
            auto read = llvm::cast<ctjs::GetPropertyOp>(op);
            const auto instance = instanceOf(read.getObject(), current);
            const auto fact = current.sizeFacts(instance);
            current.sizeBounds[read.getResult()] = fact.lower;
            if (fact.exact) { current.exactSizes[read.getResult()] = *fact.exact; }
            current.scalars[read.getResult()] =
                PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::NumberAttr>());
            return;
        }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
            state thenState = current;
            state elseState = current;
            learn(branch.getCondition(), true, thenState);
            learn(branch.getCondition(), false, elseState);
            region(branch.getThenRegion(), thenState);
            region(branch.getElseRegion(), elseState);
            // Retain independently proved truthy/falsy alternatives. A later
            // test can refine the exact selected SSA value, while all effects
            // in both structural arms are still visited.
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
                    const auto left = thenYield.getOperand(index),
                               right = elseYield.getOperand(index);
                    const auto exact = primitiveMapSize(left, thenState.keyEvidence(left));
                    if (exact && exact == primitiveMapSize(right, elseState.keyEvidence(right))) {
                        thenState.exactSizes[branch.getResult(index)] = *exact;
                        elseState.exactSizes[branch.getResult(index)] = *exact;
                    }
                    const auto joined =
                        thenState.alternatives(thenYield.getOperand(index))
                            .joined(elseState.alternatives(elseYield.getOperand(index)));
                    if (joined.known) {
                        thenState.scalars[branch.getResult(index)] = joined;
                        elseState.scalars[branch.getResult(index)] = joined;
                    }
                }
            }
            // Derive each arm's cardinality before merging key sets. The
            // union of possible keys and intersection of definite keys cannot
            // recover equal sizes when different keys survive on each arm.
            thenState.rememberSizes();
            elseState.rememberSizes();
            thenState.intersect(elseState);
            current = std::move(thenState);
            return;
        }
        if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(op)) {
            // No incoming fact or cached has is assumed invariant across a
            // back edge. Fresh tests and sets in each iteration still prove
            // local reads, including a while(has(...)) body's first lookup.
            state before;
            region(loop.getBefore(), before);
            if (!loop.getBefore().empty()) {
                if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(
                        loop.getBefore().front().getTerminator())) {
                    learn(condition.getCondition(), true, before);
                }
            }
            region(loop.getAfter(), before);
            current = {};
            return;
        }
        if (llvm::isa<mlir::scf::ForOp>(op)) {
            state iteration;
            region(op->getRegion(0), iteration);
            current = {};
            return;
        }
        if (op->getNumRegions() != 0) {
            for (mlir::Region & nested : op->getRegions()) {
                state isolated;
                region(nested, isolated);
            }
            current = {};
            return;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
            if (snapshotCopies.contains(op)) { return; }
            const auto action = actions.lookup(op);
            if ((action == "set" || action == "get" || action == "has" || action == "delete") &&
                !call.getArgs().empty()) {
                const auto kind = current.scalar(call.getArgs()[0]);
                if (kind != payloadKind::Unknown) { keys[op] = kind; }
            }
            if (action == "set") {
                const auto kind = current.scalar(call.getArgs()[1]);
                if (kind != payloadKind::Unknown) { writes[op] = kind; }
                write(current, call);
            } else if (action == "has") {
                current.scalars[call.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
                current.observations.insert(op);
            } else if (action == "get") {
                const auto wanted = entry(call, current);
                for (const fact & value : current.entries) {
                    if (!value.present || value.instance != wanted.instance ||
                        comparePrimitiveMapKeys(
                            value.key, wanted.key, current.keyEvidence(value.key),
                            current.keyEvidence(wanted.key)) != PrimitiveMapKeyRelation::Same) {
                        continue;
                    }
                    proved.insert(op);
                    if (value.storedOrigin) {
                        current.aliases[call.getResult()] = value.storedOrigin;
                    }
                    if (value.payload.known) {
                        payloads[op] = value.payload;
                        current.scalars[call.getResult()] = value.payload;
                    }
                }
            } else if (action == "delete") {
                current.scalars[call.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
                eraseKey(current, call);
            } else if (action == "clear") {
                effects erase;
                erase.erased.insert(familyOf(call.getReceiver()));
                const auto instance = instanceOf(call.getReceiver(), current);
                invalidate(current, erase, instance);
                current.possibleKeys[instance] = {};
                current.currentSizes[instance] = 0;
            } else if (action.empty()) {
                current = {};
            }
        } else if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                call, call.getCalleeAttr());
            const auto summary = summaries.find(target);
            if (summary == summaries.end()) {
                current = {};
            } else {
                invalidate(current, summary->second);
            }
        }
    }
};

} // namespace

std::string provePresence(mlir::ModuleOp module, llvm::ArrayRef<ctjs::CallOp> calls,
                          llvm::ArrayRef<ctjs::GetPropertyOp> sizes,
                          llvm::ArrayRef<ctjs::CallOp> reads,
                          llvm::ArrayRef<ctjs::CallOp> optionalReads,
                          llvm::ArrayRef<ctjs::CallOp> typedReads,
                          const llvm::DenseSet<mlir::Operation *> & snapshotCopies,
                          llvm::function_ref<mlir::Value(mlir::Value)> familyOf,
                          const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & parameters) {
    if (calls.empty()) { return {}; }
    presenceAnalysis analysis(familyOf, snapshotCopies);
    for (ctjs::GetPropertyOp size : sizes) { analysis.sizes.insert(size); }
    for (ctjs::CallOp call : calls) { analysis.actions[call] = actionOf(call); }
    analysis.buildSummaries(module);
    module.walk([&](ctjs::FuncOp fn) {
        state initial;
        if (!fn.getBody().empty()) {
            for (mlir::BlockArgument argument : fn.getBody().front().getArguments()) {
                const auto found = parameters.find(argument);
                if (found != parameters.end()) { initial.scalars[argument] = found->second; }
            }
        }
        analysis.region(fn.getBody(), initial);
    });
    for (ctjs::CallOp read : reads) {
        if (!analysis.proved.contains(read)) {
            return "nested native Map get requires presence on every reaching path for the same "
                   "instance and key; has observations must survive intervening effects";
        }
    }
    for (ctjs::CallOp read : reads) {
        read->setAttr(kNativeMapPresent, mlir::UnitAttr::get(read.getContext()));
    }
    for (ctjs::CallOp read : typedReads) {
        const auto alternatives = analysis.payloads.lookup(read);
        if (!alternatives.known) { continue; }
        const auto mask = alternatives.truthy | alternatives.falsy;
        using Primitive = PrimitiveAlternatives;
        llvm::StringRef tag;
        if (mask == Primitive::Boolean) { tag = "bool"; }
        if (mask == Primitive::Number) { tag = "number"; }
        if (mask == Primitive::String) { tag = "string"; }
        if ((mask & Primitive::String) != 0 &&
            (mask & (Primitive::Null | Primitive::Undefined)) != 0 &&
            (mask & ~(Primitive::String | Primitive::Null | Primitive::Undefined)) == 0) {
            tag = "nullable_string";
        }
        if (!tag.empty()) {
            read->setAttr(kNativeMapReadType, mlir::StringAttr::get(read.getContext(), tag));
            // Dead-code inference may remove an alternative from the final
            // schema. Its homogeneous read still has definite membership.
            read->setAttr(kNativeMapPresent, mlir::UnitAttr::get(read.getContext()));
        }
    }
    for (const auto & [call, kind] : analysis.keys) {
        const auto tag = kind == payloadKind::Boolean  ? "bool"
                         : kind == payloadKind::Number ? "number"
                                                       : "string";
        call->setAttr(kNativeMapKeyType, mlir::StringAttr::get(call->getContext(), tag));
    }
    for (const auto & [write, kind] : analysis.writes) {
        const auto tag = kind == payloadKind::Boolean  ? "bool"
                         : kind == payloadKind::Number ? "number"
                                                       : "string";
        write->setAttr(kNativeMapWriteType, mlir::StringAttr::get(write->getContext(), tag));
    }
    for (ctjs::CallOp read : optionalReads) {
        if (analysis.proved.contains(read)) {
            read->setAttr(kNativeMapPresent, mlir::UnitAttr::get(read.getContext()));
        }
    }
    return {};
}

} // namespace ctcompile::ctnative::map_detail
