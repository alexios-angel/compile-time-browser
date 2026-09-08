//===- Presence.cpp - structured must-analysis for nested Map reads -------===//
#include "Presence.h"

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

struct fact {
    mlir::Value instance;
    mlir::Value key;
    bool matches(const fact & other) const {
        return instance == other.instance &&
               comparePrimitiveMapKeys(key, other.key) == PrimitiveMapKeyRelation::Same;
    }
};

struct state {
    llvm::SmallVector<fact> present;
    // A has result is a snapshot. Erasure invalidates its implication even
    // though its SSA boolean remains available to later conditions.
    llvm::DenseSet<mlir::Operation *> observations;
    // Bounds on already-read SSA numbers survive known mutations. They are
    // acquired only from a checked size read with exact-instance presence.
    llvm::DenseMap<mlir::Value, unsigned> sizeBounds{};

    bool contains(fact value) const {
        return llvm::any_of(present, [&](const fact & old) { return old.matches(value); });
    }
    void add(fact value) {
        if (!contains(value)) { present.push_back(value); }
    }
    void intersect(const state & other) {
        llvm::erase_if(present, [&](const fact & value) { return !other.contains(value); });
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
    }
};

struct effects {
    bool unknown = false;
    llvm::DenseSet<mlir::Value> erased;

    bool merge(const effects & other) {
        bool changed = !unknown && other.unknown;
        unknown |= other.unknown;
        for (mlir::Value family : other.erased) { changed |= erased.insert(family).second; }
        return changed;
    }
};

struct presenceAnalysis {
    llvm::DenseMap<mlir::Operation *, llvm::StringRef> actions;
    llvm::DenseMap<mlir::Operation *, effects> summaries;
    llvm::DenseSet<mlir::Operation *> proved;
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
        return {instanceOf(call.getReceiver()), call.getArgs()[0]};
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
        llvm::erase_if(current.present, [&](const fact & value) {
            return effect.erased.contains(familyOf(value.instance));
        });
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
        llvm::erase_if(current.present, mayErase);
        for (mlir::Operation * op : llvm::make_early_inc_range(current.observations)) {
            if (mayErase(entry(llvm::cast<ctjs::CallOp>(op)))) { current.observations.erase(op); }
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
            const auto instance = instanceOf(read.getObject());
            llvm::SmallVector<mlir::Value> distinct;
            unsigned candidates = 0;
            for (const fact & value : current.present) {
                if (value.instance != instance) { continue; }
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
            return;
        }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
            state thenState = current;
            state elseState = current;
            learn(branch.getCondition(), true, thenState);
            learn(branch.getCondition(), false, elseState);
            region(branch.getThenRegion(), thenState);
            region(branch.getElseRegion(), elseState);
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
                current.add(entry(call));
            } else if (action == "has") {
                current.observations.insert(op);
            } else if (action == "get") {
                if (current.contains(entry(call))) { proved.insert(op); }
            } else if (action == "delete") {
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
                          const llvm::DenseSet<mlir::Operation *> & snapshotCopies,
                          llvm::function_ref<mlir::Value(mlir::Value)> familyOf) {
    if (reads.empty() && optionalReads.empty()) { return {}; }
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
    for (ctjs::CallOp read : optionalReads) {
        if (analysis.proved.contains(read)) {
            read->setAttr(kNativeMapPresent, mlir::UnitAttr::get(read.getContext()));
        }
    }
    return {};
}

} // namespace ctcompile::ctnative::map_detail
