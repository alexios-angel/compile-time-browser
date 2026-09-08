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
    payloadKind payload = payloadKind::Unknown;
    // Payload is valid whenever present, independently of definite membership.
    // Deletion cannot change a surviving value, so it clears only this flag.
    bool present = true;
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
    // A scalar get result owns its value. Unlike membership and entry tags,
    // its type remains true after the source entry is overwritten or erased.
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> scalars{};

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
        llvm::erase_if(entries, [&](const fact & value) { return !other.contains(value); });
        for (fact & value : entries) {
            const auto found = llvm::find_if(
                other.entries, [&](const fact & candidate) { return value.matches(candidate); });
            if (value.payload != found->payload) { value.payload = {}; }
            value.present &= found->present;
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
    llvm::DenseMap<mlir::Operation *, payloadKind> payloads, writes;
    llvm::DenseSet<mlir::Operation *> sizes;
    llvm::function_ref<mlir::Value(mlir::Value)> familyOf;
    const llvm::DenseSet<mlir::Operation *> & snapshotCopies;

    presenceAnalysis(llvm::function_ref<mlir::Value(mlir::Value)> family,
                     const llvm::DenseSet<mlir::Operation *> & copies)
        : familyOf(family), snapshotCopies(copies) {}

    // set is the only alias edge that proves runtime identity. Closed
    // parameter/return edges merely share a schema and cannot be used here.
    mlir::Value instanceOf(mlir::Value value) const {
        while (auto call = value.getDefiningOp<ctjs::CallOp>()) {
            if (actions.lookup(call) != "set") { break; }
            value = call.getReceiver();
        }
        return value;
    }
    fact entry(ctjs::CallOp call) const {
        return {instanceOf(call.getReceiver()), call.getArgs()[0], {}};
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

    void invalidate(state & current, const effects & effect) const {
        if (effect.unknown) {
            current = {};
            return;
        }
        llvm::erase_if(current.entries, [&](const fact & value) {
            return effect.erased.contains(familyOf(value.instance));
        });
        for (fact & value : current.entries) {
            if (effect.written.contains(familyOf(value.instance))) { value.payload = {}; }
        }
        for (mlir::Operation * op : llvm::make_early_inc_range(current.observations)) {
            auto call = llvm::cast<ctjs::CallOp>(op);
            if (effect.erased.contains(familyOf(call.getReceiver()))) {
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
            current.add(entry(call));
        }
    }

    void eraseKey(state & current, ctjs::CallOp call) const {
        const auto affected = entry(call);
        const auto mayErase = [&](fact value) {
            // A schema family may contain several runtime instances. Without
            // an independent disjointness proof, any of them may be this Map.
            return familyOf(value.instance) == familyOf(affected.instance) &&
                   comparePrimitiveMapKeys(value.key, affected.key,
                                           {{}, current.sizeBounds.lookup(value.key)},
                                           {{}, current.sizeBounds.lookup(affected.key)}) !=
                       PrimitiveMapKeyRelation::Distinct;
        };
        for (fact & value : current.entries) {
            if (mayErase(value)) { value.present = false; }
        }
        for (mlir::Operation * op : llvm::make_early_inc_range(current.observations)) {
            if (mayErase(entry(llvm::cast<ctjs::CallOp>(op)))) { current.observations.erase(op); }
        }
    }

    // Literal and saved scalar tags do not depend on Map schema inference or
    // prior invocations. Every write still invalidates possible aliases; an
    // unproved payload clears their type evidence. has never supplies a tag.
    void write(state & current, ctjs::CallOp call) const {
        fact added = entry(call);
        added.payload = current.scalar(call.getArgs()[1]);
        for (fact & previous : current.entries) {
            if (familyOf(previous.instance) != familyOf(added.instance)) { continue; }
            const auto relation = comparePrimitiveMapKeys(
                previous.key, added.key, {{}, current.sizeBounds.lookup(previous.key)},
                {{}, current.sizeBounds.lookup(added.key)});
            if (relation == PrimitiveMapKeyRelation::Distinct) { continue; }
            if (previous.instance == added.instance && relation == PrimitiveMapKeyRelation::Same) {
                previous.payload = added.payload;
                previous.present = true;
            } else if (previous.payload != added.payload) {
                previous.payload = {};
            }
        }
        current.add(added);
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
            const auto instance = instanceOf(read.getObject());
            llvm::SmallVector<mlir::Value> distinct;
            unsigned candidates = 0;
            for (const fact & value : current.entries) {
                if (!value.present || value.instance != instance) { continue; }
                if (candidates++ == kMaxPrimitiveMapSizeCandidates) { break; }
                if (llvm::all_of(distinct, [&](mlir::Value previous) {
                        return comparePrimitiveMapKeys(
                                   previous, value.key, {{}, current.sizeBounds.lookup(previous)},
                                   {{}, current.sizeBounds.lookup(value.key)}) ==
                               PrimitiveMapKeyRelation::Distinct;
                    })) {
                    distinct.push_back(value.key);
                }
            }
            current.sizeBounds[read.getResult()] = static_cast<unsigned>(distinct.size());
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
                    const auto joined =
                        thenState.alternatives(thenYield.getOperand(index))
                            .joined(elseState.alternatives(elseYield.getOperand(index)));
                    if (joined.known) {
                        thenState.scalars[branch.getResult(index)] = joined;
                        elseState.scalars[branch.getResult(index)] = joined;
                    }
                }
            }
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
            if (action == "set") {
                const auto kind = current.scalar(call.getArgs()[1]);
                if (kind != payloadKind::Unknown) { writes[op] = kind; }
                write(current, call);
            } else if (action == "has") {
                current.scalars[call.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
                current.observations.insert(op);
            } else if (action == "get") {
                const auto wanted = entry(call);
                for (const fact & value : current.entries) {
                    if (!value.present || !value.matches(wanted)) { continue; }
                    proved.insert(op);
                    if (value.payload != payloadKind::Unknown) {
                        payloads[op] = value.payload;
                        const auto tag = value.payload == payloadKind::Boolean
                                             ? mlir::TypeID::get<ctjs::BooleanAttr>()
                                         : value.payload == payloadKind::Number
                                             ? mlir::TypeID::get<ctjs::NumberAttr>()
                                             : mlir::TypeID::get<ctjs::StringAttr>();
                        current.scalars[call.getResult()] = PrimitiveAlternatives::forTag(tag);
                    }
                }
            } else if (action == "delete") {
                current.scalars[call.getResult()] =
                    PrimitiveAlternatives::forTag(mlir::TypeID::get<ctjs::BooleanAttr>());
                eraseKey(current, call);
            } else if (action == "clear") {
                effects erase;
                erase.erased.insert(familyOf(call.getReceiver()));
                invalidate(current, erase);
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
                          llvm::function_ref<mlir::Value(mlir::Value)> familyOf) {
    if (calls.empty()) { return {}; }
    presenceAnalysis analysis(familyOf, snapshotCopies);
    for (ctjs::GetPropertyOp size : sizes) { analysis.sizes.insert(size); }
    for (ctjs::CallOp call : calls) { analysis.actions[call] = actionOf(call); }
    analysis.buildSummaries(module);
    module.walk([&](ctjs::FuncOp fn) {
        state initial;
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
        if (auto kind = analysis.payloads.lookup(read); kind != payloadKind::Unknown) {
            const auto tag = kind == payloadKind::Boolean  ? "bool"
                             : kind == payloadKind::Number ? "number"
                                                           : "string";
            read->setAttr(kNativeMapReadType, mlir::StringAttr::get(read.getContext(), tag));
            // Dead-code inference may remove an alternative from the final
            // schema. Its homogeneous read still has definite membership.
            read->setAttr(kNativeMapPresent, mlir::UnitAttr::get(read.getContext()));
        }
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
