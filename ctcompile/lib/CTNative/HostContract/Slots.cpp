//===- Slots.cpp - Publication slot ordering --------------------*- C++ -*-===//
//
// Implements the execution-order queries (active, anchor, before) and
// analyzer::slot, which pairs every read of a host root's published field with
// the write it sees.
//
//===----------------------------------------------------------------------===//

#include "Analysis.h"

#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::ctnative::host_detail {

/// False when `operation` sits inside an scf.if arm whose condition is known
/// and selects the other arm. True otherwise, including when out of budget,
/// so callers keep checking the operation.
bool analyzer::active(mlir::Operation * operation) {
    for (auto * child = operation; child->getParentOp() != nullptr; child = child->getParentOp()) {
        if (!step()) { return true; }
        auto branch = llvm::dyn_cast<mlir::scf::IfOp>(child->getParentOp());
        if (!branch) { continue; }
        const auto selected = truth(branch.getCondition());
        if (selected && child->getParentRegion() != &branch->getRegion(*selected ? 0u : 1u)) {
            return false;
        }
    }
    return true;
}

/// The entry-block operation that contains `operation`, found by climbing out
/// of scf.if arms whose condition is known to select them. Null when an arm
/// is not known to run or the function's entry block is not reached.
mlir::Operation * analyzer::anchor(mlir::Operation * operation) {
    while (!llvm::isa<ctjs::FuncOp>(operation->getParentOp())) {
        if (!step()) { return nullptr; }
        auto branch = llvm::dyn_cast_or_null<mlir::scf::IfOp>(operation->getParentOp());
        if (!branch) { return nullptr; }
        auto selected = truth(branch.getCondition());
        if (!selected || operation->getParentRegion() != &branch->getRegion(*selected ? 0u : 1u)) {
            return nullptr;
        }
        operation = branch;
    }
    auto function = llvm::cast<ctjs::FuncOp>(operation->getParentOp());
    return operation->getBlock() == &function.getBody().front() ? operation : nullptr;
}

/// Whether `first` runs before `second` on every path. Within one function
/// this is dominance, directly or between their anchors. Across functions it
/// replaces an operation in a helper by the helper's unique call site.
/// Returns false when unsure.
bool analyzer::before(mlir::Operation * first, mlir::Operation * second) {
    llvm::DenseSet<mlir::Operation *> visited;
    for (;;) {
        if (!first || !second || !step() || !visited.insert(first).second) { return false; }
        auto from = first->getParentOfType<ctjs::FuncOp>();
        auto to = second->getParentOfType<ctjs::FuncOp>();
        if (!from || !to) { return false; }
        if (from == to) {
            if (dominance.properlyDominates(first, second)) { return true; }
            auto * firstAnchor = anchor(first);
            auto * secondAnchor = anchor(second);
            return firstAnchor && secondAnchor && firstAnchor != secondAnchor &&
                   dominance.properlyDominates(firstAnchor, secondAnchor);
        }
        // Only a helper with exactly one statically visible caller is
        // followed: its call site stands in for it. Several calls or recursion
        // would need slot state per invocation, rather than treating
        // allocations from different invocations as the same.
        if (from != entry && callers[from].size() == 1 && anchor(first)) {
            auto call = callers[from].front();
            if (!exactCall(call)) { return false; }
            first = call;
            continue;
        }
        if (to != entry && callers[to].size() == 1) {
            auto call = callers[to].front();
            if (!exactCall(call)) { return false; }
            second = call;
            visited.erase(first);
            continue;
        }
        return false;
    }
}

/// Checks the publication slot `key` of the contract root object bound to
/// `request.binding`: the root must be a fresh object initialized once by the
/// entry, every alias use must be modeled, and each read of the slot must
/// have one latest write that runs before it. Fills `edges` with those
/// write-to-read pairs; `reason` holds the first refusal, if any.
HostSlotReport analyzer::slot(const HostRootRequest & request, llvm::StringRef key) {
    HostSlotReport result;
    result.binding = request.binding;
    result.property = key.str();
    const auto reject = [&](llvm::StringRef why) {
        if (result.reason.empty()) { result.reason = why.str(); }
    };
    const auto & stores = globals[request.binding];
    if (stores.size() != 1) {
        reject("root binding requires exactly one source initialization");
        return result;
    }
    auto initialization = stores.front();
    result.owner = initialization.getValue().getDefiningOp<ctjs::CreateObjectOp>();
    if (!result.owner || initialization->getParentOfType<ctjs::FuncOp>() != entry ||
        result.owner->getParentOfType<ctjs::FuncOp>() != entry || !anchor(initialization)) {
        reject("root must be a fresh object initialized unconditionally by the script entry");
        return result;
    }
    if (!ctjs::ordinaryKey(key)) {
        reject("publication requires an ordinary constant own-data key");
    }

    llvm::SmallVector<mlir::Value> aliases;
    module.walk([&](mlir::Operation * operation) {
        if (!active(operation)) { return; }
        for (mlir::Value value : operation->getResults()) {
            if (object(value) == result.owner) { aliases.push_back(value); }
        }
        for (mlir::Region & region : operation->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument argument : block.getArguments()) {
                    if (object(argument) == result.owner) { aliases.push_back(argument); }
                }
            }
        }
    });
    llvm::DenseSet<mlir::Operation *> seenReads, seenWrites;
    for (mlir::Value value : aliases) {
        if (auto load = value.getDefiningOp<ctjs::LoadGlobalOp>()) {
            const auto & definitions = globals[load.getName()];
            if (definitions.size() != 1 || !before(definitions.front(), load)) {
                reject("root or alias load is not dominated by its binding initialization");
            }
        }
        for (mlir::OpOperand & use : value.getUses()) {
            auto * operation = use.getOwner();
            if (!active(operation)) { continue; }
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                read && use.getOperandNumber() == 0) {
                if (!ctjs::ordinaryKey(ctjs::constantKey(read.getKey()))) {
                    reject("dynamic or prototype root read");
                }
                if (ctjs::constantKey(read.getKey()) == key && seenReads.insert(read).second) {
                    result.reads.push_back(read);
                }
                continue;
            }
            if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation);
                write && use.getOperandNumber() == 0) {
                if (!ctjs::ordinaryKey(ctjs::constantKey(write.getKey()))) {
                    reject("dynamic or prototype root write");
                }
                if (ctjs::constantKey(write.getKey()) == key && seenWrites.insert(write).second) {
                    result.writes.push_back(write);
                }
                continue;
            }
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation);
                store && globals[store.getName()].size() == 1) {
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
                call && use.getOperandNumber() >= 3 && exactCall(call)) {
                continue;
            }
            if (llvm::isa<ctjs::RootOp>(operation) || ctjs::nonCoercing(operation)) { continue; }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
                auto branch = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp());
                if (branch && object(branch.getResult(use.getOperandNumber())) == result.owner) {
                    continue;
                }
            }
            if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation);
                returned && returned->getParentOfType<ctjs::FuncOp>() != entry) {
                continue;
            }
            reject(("root escapes or has an unsupported use through `" +
                    operation->getName().getStringRef() + "`")
                       .str());
        }
    }
    if (result.writes.empty()) { reject("publication slot has no visible source write"); }
    if (result.reads.empty()) { reject("publication slot has no visible source read"); }
    for (ctjs::GetPropertyOp read : result.reads) {
        ctjs::SetPropertyOp latest;
        for (ctjs::SetPropertyOp write : result.writes) {
            if (before(write, read)) {
                if (!latest || before(latest, write)) {
                    latest = write;
                } else if (!before(write, latest)) {
                    reject("publication writes have ambiguous order");
                }
            } else if (!before(read, write)) {
                reject("publication write/read order requires unsupported control or call context");
            }
        }
        if (latest) {
            result.edges.push_back({latest, read});
        } else {
            reject("publication read has no dominating own-data initialization");
        }
    }
    return result;
}

} // namespace ctcompile::ctnative::host_detail
