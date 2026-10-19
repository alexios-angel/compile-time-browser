//===- Presence.cpp - Map key presence proof --------------------*- C++ -*-===//
//
// A forward must-analysis over each function's structured control flow
// (scf.if, scf.while, scf.for) that proves which native Map `get` calls
// definitely find their key, and which scalar types Map keys, writes and
// reads have. NativeMap.cpp calls provePresence() once its own identity and
// use checks pass; the results become ctnative.map_* attributes.
//
//===----------------------------------------------------------------------===//
#include "Presence.h"

#include "../OwnedGlobalRoots.h"
#include "../PrimitiveMapKey.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/PrimitiveAlternatives.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>

namespace ctcompile::ctnative::map_detail {
namespace {

/// What is known about one key of one runtime Map instance.
struct fact {
    mlir::Value instance;
    mlir::Value key;
    /// The possible primitive kinds of the stored value.
    PrimitiveAlternatives payload;
    /// Whether the key is definitely present. `payload` stays valid whenever
    /// the key is present, independently of this flag: deletion cannot change
    /// a surviving value, so it clears only this flag.
    bool present = true;
    /// The SSA value that was stored (for a Map of Maps, the child Map).
    mlir::Value storedOrigin{};
    bool matches(const fact & other) const {
        return instance == other.instance &&
               comparePrimitiveMapKeys(key, other.key) == PrimitiveMapKeyRelation::Same;
    }
};

/// Keeps only the entries `other` records with the same value.
template <class Map> void keepAgreeing(Map & mine, const Map & other) {
    for (const auto & [key, value] : llvm::make_early_inc_range(mine)) {
        const auto found = other.find(key);
        if (found == other.end() || value != found->second) { mine.erase(key); }
    }
}

/// The facts known at one program point.
struct state {
    /// Keys of specific instances, with their presence and value kinds.
    llvm::SmallVector<fact> entries;
    /// `has` calls whose true result still implies presence. Each records the
    /// state at the call: an erase removes the implication, even though the
    /// SSA boolean can still be used by later conditions.
    llvm::DenseSet<mlir::Operation *> observations;
    /// Lower bounds on the Numbers that `size` reads returned. A read value
    /// never changes, so these survive later mutations. They come only from a
    /// checked `size` read on a known instance.
    llvm::DenseMap<mlir::Value, unsigned> sizeBounds{};
    /// Every key an instance may hold (an upper bound on its contents). It
    /// belongs to one runtime instance, never its schema family. A missing
    /// entry means unknown contents; an empty vector proves the Map is empty.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> possibleKeys{};
    /// The exact Number a `size` read (or a branch merging two) returned.
    llvm::DenseMap<mlir::Value, unsigned> exactSizes{};
    /// The current exact size of an instance. Unlike `exactSizes`, this changes
    /// with mutations and is kept only across mutations whose effect on the
    /// size is proved. Two branch arms with equal sizes keep it even when they
    /// share no definite key.
    llvm::DenseMap<mlir::Value, unsigned> currentSizes{};
    /// Primitive kinds of scalar values, including `get` results. A read value
    /// keeps its type after the Map entry it came from is overwritten or erased.
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> scalars{};
    /// Maps a definite `get` result to the value stored under that key. The
    /// alias outlives replacement or deletion of the outer entry; it never
    /// joins two instances of one schema.
    llvm::DenseMap<mlir::Value, mlir::Value> aliases{};

    PrimitiveMapKeyEvidence keyEvidence(mlir::Value value) const {
        const auto found = exactSizes.find(value);
        return {{},
                sizeBounds.lookup(value),
                found == exactSizes.end() ? std::nullopt : std::optional(found->second)};
    }

    /// How two keys compare under this state's size evidence.
    PrimitiveMapKeyRelation relate(mlir::Value left, mlir::Value right) const {
        return comparePrimitiveMapKeys(left, right, keyEvidence(left), keyEvidence(right));
    }

    struct size_fact {
        unsigned lower = 0;
        std::optional<unsigned> exact;
    };
    /// The size of `instance`: a lower bound from its distinct present keys,
    /// and an exact value when its possible keys are exactly those keys.
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
                    return relate(previous, value.key) == PrimitiveMapKeyRelation::Distinct;
                })) {
                distinct.push_back(value.key);
            }
        }
        size_fact result{static_cast<unsigned>(distinct.size()), std::nullopt};
        if (const auto found = possibleKeys.find(instance); found != possibleKeys.end()) {
            llvm::SmallVector<mlir::Value> possible;
            for (mlir::Value key : found->second) {
                if (llvm::none_of(possible, [&](mlir::Value previous) {
                        return relate(previous, key) == PrimitiveMapKeyRelation::Same;
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

    /// The exact size of `value.instance` after a `set` (or a `delete` when
    /// `erase`) of `value.key`, if it can be proved.
    std::optional<unsigned> sizeAfterMutation(fact value, bool erase) const {
        const auto size = currentSizes.find(value.instance);
        const auto possible = possibleKeys.find(value.instance);
        if (size == currentSizes.end() || possible == possibleKeys.end()) { return std::nullopt; }
        // Query the actual instance before the mutation. Its schema family only
        // bounds what it may alias and cannot prove this key's membership.
        const bool present = llvm::any_of(entries, [&](const fact & old) {
            return old.present && old.instance == value.instance &&
                   relate(old.key, value.key) == PrimitiveMapKeyRelation::Same;
        });
        if (present) {
            if (!erase) { return size->second; }
            return size->second ? std::optional(size->second - 1) : std::nullopt;
        }
        const bool absent = llvm::all_of(possible->second, [&](mlir::Value key) {
            return relate(key, value.key) == PrimitiveMapKeyRelation::Distinct;
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
    llvm::StringRef scalar(mlir::Value value) const { return mapScalarTag(alternatives(value)); }

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
    /// Merges two branch states: keeps only facts both agree on.
    void intersect(const state & other) {
        keepAgreeing(currentSizes, other.currentSizes);
        for (auto & [instance, possible] : llvm::make_early_inc_range(possibleKeys)) {
            const auto found = other.possibleKeys.find(instance);
            if (found == other.possibleKeys.end() ||
                possible.size() + found->second.size() > kMaxPrimitiveMapSizeCandidates) {
                possibleKeys.erase(instance);
            } else {
                possible.append(found->second);
            }
        }
        keepAgreeing(exactSizes, other.exactSizes);
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
        keepAgreeing(aliases, other.aliases);
    }
};

/// What a function (and everything it calls) may do to Maps, by family.
struct effects {
    /// Set by a call this analysis cannot see into; it erases every fact.
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

/// Forward must-analysis over each function's structured control flow,
/// proving which Map `get` calls definitely find their key. `proved` holds the
/// gets that do, `payloads` the primitive kinds they read, and `keys`/`writes`
/// the scalar key and value tags for lowering. `publications` records the
/// child entries present when a child Map is stored into a host-captured
/// global Map; `retainedEntries` lifts those to an invariant of every child of
/// that global.
struct presenceAnalysis {
    /// The Map method each proved call invokes ("get", "set", ...).
    llvm::DenseMap<mlir::Operation *, llvm::StringRef> actions;
    /// Per-function Map effects, including callees (see buildSummaries).
    llvm::DenseMap<mlir::Operation *, effects> summaries;
    llvm::DenseSet<mlir::Operation *> proved;
    llvm::DenseMap<mlir::Operation *, PrimitiveAlternatives> payloads;
    llvm::DenseMap<mlir::Operation *, llvm::StringRef> writes, keys;
    /// The `size` reads to track.
    llvm::DenseSet<mlir::Operation *> sizes;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<fact>> publications;
    llvm::DenseMap<mlir::Operation *, mlir::Value> storedOrigins;
    llvm::DenseMap<mlir::Operation *, PrimitiveAlternatives> storedPayloads;
    llvm::DenseMap<mlir::Value, fact> retainedEntries;
    llvm::function_ref<mlir::Value(mlir::Value)> familyOf;
    const llvm::DenseSet<mlir::Operation *> & snapshotCopies;
    llvm::function_ref<bool(mlir::Operation *)> hostTyped;
    /// Each read of a run-once static that holds a Map, mapped to the value
    /// the static's one write stored (see provePresence).
    const llvm::DenseMap<mlir::Value, mlir::Value> * staticOrigins = nullptr;

    presenceAnalysis(llvm::function_ref<mlir::Value(mlir::Value)> family,
                     const llvm::DenseSet<mlir::Operation *> & copies,
                     llvm::function_ref<bool(mlir::Operation *)> typed)
        : familyOf(family), snapshotCopies(copies), hostTyped(typed) {}
    bool effectFree(mlir::Operation * op) const { return hostTyped && hostTyped(op); }

    /// The runtime instance `value` is known to be. Only a `set` result (which
    /// returns its receiver) and a definite `get` of a known stored value prove
    /// identity; parameters and returns share only a schema.
    mlir::Value instanceOf(mlir::Value value, const state & current) const {
        while (true) {
            if (staticOrigins) {
                if (const auto stored = staticOrigins->lookup(value)) { value = stored; }
            }
            if (const auto origin = current.aliases.lookup(value)) { return origin; }
            auto call = value.getDefiningOp<ctjs::CallOp>();
            if (!call || actions.lookup(call) != "set") { break; }
            value = call.getReceiver();
        }
        return value;
    }
    /// The (instance, key) a Map method call addresses.
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

    /// Computes each function's Map effects, merged over everything it calls.
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
                    } else if (action.empty() && !effectFree(op)) {
                        out.unknown = true;
                    }
                } else if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
                    auto target = call.getTarget();
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

    /// Registers every `set` on a host-captured global Map, so write() can
    /// record the stored child's entries at that point.
    void collectPublications(const OwnedGlobalRoots * globals) {
        if (!globals || !globals->proved()) { return; }
        for (const auto & root : globals->roots()) {
            if (!root.capturedMap()) { continue; }
            const auto & captured = *root.capturedMap();
            auto allocation = captured.allocation;
            for (ctjs::CallOp call : captured.calls) {
                if (actions.lookup(call) == "set" &&
                    familyOf(call.getReceiver()) == familyOf(allocation.getResult())) {
                    publications.try_emplace(call);
                }
            }
        }
    }

    /// For each host-captured global Map of Maps, proves a key every child
    /// always holds: all child writes use one literal String key with one
    /// scalar type, nothing deletes from a child, and every child was stored
    /// with that key present. A `get` of a child then adds that entry.
    void proveRetainedEntries(const OwnedGlobalRoots * globals) {
        if (!globals || !globals->proved()) { return; }
        for (const auto & root : globals->roots()) {
            if (!root.capturedMap()) { continue; }
            const auto & captured = *root.capturedMap();
            if (captured.childMaps.empty()) { continue; }
            auto allocation = captured.allocation;
            const auto outer = familyOf(allocation.getResult());
            llvm::DenseSet<mlir::Value> children;
            for (ctjs::ConstructOp child : captured.childMaps) {
                children.insert(familyOf(child.getResult()));
            }
            fact invariant;
            bool valid = true;
            // ponytail: one literal String key with uniform scalar writes;
            // allow more keys only when a program needs them.
            for (ctjs::CallOp call : captured.calls) {
                if (!children.contains(familyOf(call.getReceiver()))) { continue; }
                const auto action = actions.lookup(call);
                if (action == "delete" || action == "clear" || action.empty()) {
                    valid = false;
                    break;
                }
                if (action != "set") { continue; }
                const auto payload = storedPayloads.lookup(call);
                if (!ctjs::constantAttr<ctjs::StringAttr>(call.getArgs()[0]) ||
                    mapScalarTag(payload).empty()) {
                    valid = false;
                    break;
                }
                if (!invariant.key) {
                    invariant = {{}, call.getArgs()[0], payload};
                } else if (comparePrimitiveMapKeys(invariant.key, call.getArgs()[0]) !=
                               PrimitiveMapKeyRelation::Same ||
                           !(invariant.payload == payload)) {
                    valid = false;
                    break;
                }
            }
            if (!valid || !invariant.key) { continue; }
            bool published = false;
            for (ctjs::CallOp call : captured.calls) {
                if (actions.lookup(call) != "set" || familyOf(call.getReceiver()) != outer) {
                    continue;
                }
                published = true;
                const auto origin = storedOrigins.lookup(call);
                auto child =
                    origin ? origin.getDefiningOp<ctjs::ConstructOp>() : ctjs::ConstructOp{};
                const auto found = publications.find(call);
                if (!child || !llvm::is_contained(captured.childMaps, child) ||
                    found == publications.end() ||
                    llvm::none_of(found->second, [&](const fact & value) {
                        return value.present && value.payload == invariant.payload &&
                               comparePrimitiveMapKeys(value.key, invariant.key) ==
                                   PrimitiveMapKeyRelation::Same;
                    })) {
                    valid = false;
                    break;
                }
            }
            // OwnedGlobalRoots only supplies the complete list of calls on this
            // global; every key and payload checked above was proved by this
            // analysis's own first pass, not taken from another analysis.
            if (valid && published) { retainedEntries[outer] = invariant; }
        }
    }

    /// Drops the facts `effect` may falsify, limited to Maps that may alias
    /// `instance` when it is given.
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

    /// Refines `current` for the arm where `condition` is `branch`: narrows a
    /// scalar's kinds and, on a true `has`, records the key as present.
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

    /// A mutation of `instance` leaves no saved size of any Map it may alias.
    void forgetSizes(state & current, mlir::Value instance) const {
        for (const auto & [other, exact] : llvm::make_early_inc_range(current.currentSizes)) {
            if (mayAlias(other, instance)) { current.currentSizes.erase(other); }
        }
    }

    /// Applies a `delete` call.
    void eraseKey(state & current, ctjs::CallOp call) const {
        const auto affected = entry(call, current);
        const auto nextSize = current.sizeAfterMutation(affected, true);
        forgetSizes(current, affected.instance);
        // Only this instance definitely lost the key. Other Maps in its schema
        // family may alias it, so their possible keys stay as they are while
        // mayErase below clears their definite membership. Deletion never
        // adds keys, even when key equality is unproved.
        if (auto found = current.possibleKeys.find(affected.instance);
            found != current.possibleKeys.end()) {
            llvm::erase_if(found->second, [&](mlir::Value key) {
                return current.relate(key, affected.key) == PrimitiveMapKeyRelation::Same;
            });
        }
        const auto mayErase = [&](fact value) {
            // A schema family may contain several runtime instances; without a
            // proof that they differ, any of them may be this Map.
            return mayAlias(value.instance, affected.instance) &&
                   current.relate(value.key, affected.key) != PrimitiveMapKeyRelation::Distinct;
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

    /// Records a `set`: the written instance's entry for this key takes the
    /// value's scalar type exactly; every other entry that may alias it widens
    /// to include that type. Value types come only from literals, parameters
    /// and earlier proved reads, never from Map schema inference.
    void write(state & current, ctjs::CallOp call) {
        fact added = entry(call, current);
        if (auto found = publications.find(call); found != publications.end()) {
            found->second.clear();
            const auto child = instanceOf(call.getArgs()[1], current);
            for (const fact & value : current.entries) {
                if (value.instance == child) { found->second.push_back(value); }
            }
        }
        const auto nextSize = current.sizeAfterMutation(added, false);
        forgetSizes(current, added.instance);
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
        storedPayloads[call] = added.payload;
        storedOrigins[call] = added.storedOrigin;
        for (fact & previous : current.entries) {
            if (!mayAlias(previous.instance, added.instance)) { continue; }
            const auto relation = current.relate(previous.key, added.key);
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
        // The possible-key list may have hit its limit and been dropped while
        // adding this key; keep a size only while that list still exists.
        if (nextSize && current.possibleKeys.contains(added.instance)) {
            current.currentSizes[added.instance] = *nextSize;
        }
    }

    /// Runs a single-block region; anything else forgets every fact.
    void region(mlir::Region & body, state & current) {
        if (body.empty()) { return; }
        if (!body.hasOneBlock()) {
            current = {};
            return;
        }
        for (mlir::Operation & op : body.front()) { operation(&op, current); }
    }

    /// The transfer function for one operation.
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
            // Keep the kinds proved for each result, so a later test can refine
            // the selected value; both arms' effects were already applied.
            for (unsigned index = 0; index < branch.getNumResults(); ++index) {
                const auto left = branch.thenYield().getOperand(index),
                           right = branch.elseYield().getOperand(index);
                const auto exact = primitiveMapSize(left, thenState.keyEvidence(left));
                if (exact && exact == primitiveMapSize(right, elseState.keyEvidence(right))) {
                    thenState.exactSizes[branch.getResult(index)] = *exact;
                    elseState.exactSizes[branch.getResult(index)] = *exact;
                }
                const auto joined =
                    thenState.alternatives(left).joined(elseState.alternatives(right));
                if (joined.known) {
                    thenState.scalars[branch.getResult(index)] = joined;
                    elseState.scalars[branch.getResult(index)] = joined;
                }
            }
            // Compute each arm's size before merging key sets: the union of
            // possible keys and intersection of definite keys cannot recover
            // equal sizes when different keys survive on each arm.
            thenState.rememberSizes();
            elseState.rememberSizes();
            thenState.intersect(elseState);
            current = std::move(thenState);
            return;
        }
        if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(op)) {
            // No incoming fact or earlier `has` is assumed to hold across a
            // back edge, so each iteration starts empty. Tests and sets inside
            // the loop still prove its own reads, including the first lookup
            // in a while (m.has(k)) body.
            state before;
            region(loop.getBefore(), before);
            learn(loop.getConditionOp().getCondition(), true, before);
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
            // A host call proved effect-free keeps the facts, provided no Map
            // operation sits inside its nested regions (callbacks).
            if (effectFree(op) && !op->walk([&](mlir::Operation * inner) {
                                         return actions.contains(inner) || sizes.contains(inner)
                                                    ? mlir::WalkResult::interrupt()
                                                    : mlir::WalkResult::advance();
                                     }).wasInterrupted()) {
                return;
            }
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
            if (nativeMapKeyAction(action) && !call.getArgs().empty()) {
                const auto kind = current.scalar(call.getArgs()[0]);
                if (!kind.empty()) { keys[op] = kind; }
            }
            if (action == "set") {
                const auto kind = current.scalar(call.getArgs()[1]);
                if (!kind.empty()) { writes[op] = kind; }
                write(current, call);
            } else if (action == "has") {
                current.scalars[call.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
                current.observations.insert(op);
            } else if (action == "get") {
                const auto wanted = entry(call, current);
                for (fact & value : current.entries) {
                    if (!value.present || value.instance != wanted.instance ||
                        current.relate(value.key, wanted.key) != PrimitiveMapKeyRelation::Same) {
                        continue;
                    }
                    proved.insert(op);
                    // A present lookup returns the entry's actual value. A
                    // repeated lookup returns the same alias only while the
                    // entry survives every mutation in between.
                    if (!value.storedOrigin) { value.storedOrigin = call.getResult(); }
                    current.aliases[call.getResult()] = value.storedOrigin;
                    const auto origin = value.storedOrigin;
                    if (value.payload.known) {
                        payloads[op] = value.payload;
                        current.scalars[call.getResult()] = value.payload;
                    }
                    if (auto retained = retainedEntries.find(familyOf(call.getReceiver()));
                        retained != retainedEntries.end()) {
                        auto childEntry = retained->second;
                        childEntry.instance = origin;
                        current.add(childEntry);
                    }
                    break;
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
            } else if (action.empty() && !effectFree(op)) {
                current = {};
            }
        } else if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            auto target = call.getTarget();
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
                          const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & parameters,
                          const OwnedGlobalRoots * globals,
                          llvm::function_ref<bool(mlir::Operation *)> hostTyped,
                          const llvm::DenseMap<mlir::Value, mlir::Value> * staticOrigins) {
    if (calls.empty()) { return {}; }
    presenceAnalysis analysis(familyOf, snapshotCopies, hostTyped);
    analysis.staticOrigins = staticOrigins;
    for (ctjs::GetPropertyOp size : sizes) { analysis.sizes.insert(size); }
    for (ctjs::CallOp call : calls) { analysis.actions[call] = mapMethodName(call); }
    analysis.buildSummaries(module);
    analysis.collectPublications(globals);
    const auto run = [&] {
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
    };
    run();
    analysis.proveRetainedEntries(globals);
    if (!analysis.retainedEntries.empty()) {
        analysis.proved.clear();
        analysis.payloads.clear();
        analysis.writes.clear();
        analysis.keys.clear();
        run();
    }
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
        llvm::StringRef tag = mapScalarTag(alternatives);
        if ((mask & Primitive::String) != 0 &&
            (mask & (Primitive::Null | Primitive::Undefined)) != 0 &&
            (mask & ~(Primitive::String | Primitive::Null | Primitive::Undefined)) == 0) {
            tag = "nullable_string";
        }
        if (!tag.empty()) {
            read->setAttr(kNativeMapReadType, mlir::StringAttr::get(read.getContext(), tag));
            // Also mark the read present: dead-code inference may later remove
            // an alternative from the final schema, and a read with one proved
            // type still has definite membership.
            read->setAttr(kNativeMapPresent, mlir::UnitAttr::get(read.getContext()));
        }
    }
    for (const auto & [call, tag] : analysis.keys) {
        call->setAttr(kNativeMapKeyType, mlir::StringAttr::get(call->getContext(), tag));
    }
    for (const auto & [write, tag] : analysis.writes) {
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
