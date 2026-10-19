//===- Lengths.cpp - Alias NodeList copies to the original ------*- C++ -*-===//
//
// normalizeDOMSnapshotLengths: rewrites `[].concat(...nodes)` copies of a
// querySelectorAll result (the spread loop the importer emits) so that the
// copy's `length` and index reads go straight to the original NodeList, with
// the length capped as the VM caps it. It refuses unless the copy is exact and
// only read, works on a clone, and replaces the candidate only after
// DOMEntryAnalysis accepts the result. All work is charged to `maxSteps`.
//
//===----------------------------------------------------------------------===//

#include "Bounds.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTNative/Analysis/WorkBudget.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Error.h"

namespace ctcompile::ctnative {

llvm::Error normalizeDOMSnapshotLengths(mlir::ModuleOp candidate, const HostContract & contract,
                                        unsigned maxSteps, mlir::IRMapping * mapping,
                                        unsigned * workSteps) {
    WorkBudget budget{maxSteps};
    const llvm::scope_exit recordWork([&] {
        if (workSteps) { *workSteps = static_cast<unsigned>(maxSteps - budget.remaining); }
    });
    const auto error = [&](llvm::StringRef text) {
        return llvm::createStringError(budget.remaining ? text
                                                        : "DOM snapshot work budget exhausted");
    };
    const auto spend = [&] { return budget.spend(); };
    const auto charge = [&](mlir::ModuleOp module) {
        return budget.spendWalk(module, [](mlir::Operation * operation) {
            return 2 * (uint64_t(1) + operation->getNumOperands());
        });
    };
    if (!charge(candidate)) { return error("DOM snapshot work budget exhausted"); }
    if (hostContractFingerprint(candidate) != contract.moduleSha256) {
        return error("DOM snapshot fingerprint mismatch");
    }
    bool hasSpread = false;
    candidate.walk([&](ctjs::CallSpreadOp) { hasSpread = true; });
    if (!hasSpread) { return llvm::Error::success(); }
    bool array = false, element = false;
    for (const auto & name : contract.initialIntrinsics) {
        if (!spend()) { return error("DOM snapshot work budget exhausted"); }
        array |= name == "Array";
        element |= name == "Element";
    }
    if (!array || !element) {
        return error("DOM snapshot length requires original Array and Element identities");
    }
    if (!charge(candidate)) { return error("DOM snapshot work budget exhausted"); }
    mlir::IRMapping localMapping;
    mlir::OwningOpRef<mlir::ModuleOp> changed =
        llvm::cast<mlir::ModuleOp>(candidate->clone(mapping ? *mapping : localMapping));
    mlir::DominanceInfo dominance(*changed);
    llvm::SmallVector<ctjs::CallSpreadOp> spreads;
    changed->walk([&](ctjs::CallSpreadOp call) { spreads.push_back(call); });
    llvm::SmallVector<ctjs::CallOp> snapshots;
    const auto usesOnly = [&](mlir::Value value, llvm::ArrayRef<mlir::Operation *> allowed) {
        for (mlir::OpOperand & use : value.getUses()) {
            if (!spend()) { return false; }
            if (!llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                !llvm::is_contained(allowed, use.getOwner())) {
                return false;
            }
        }
        return true;
    };
    const auto numberIs = [](mlir::Value value, double expected) {
        auto number = ctjs::constantAttr<ctjs::NumberAttr>(value);
        return number && number.getDouble() == expected;
    };
    const auto erase = [](mlir::Operation * operation) {
        for (mlir::Operation * user : llvm::make_early_inc_range(operation->getUsers())) {
            // All surviving uses were separately proved shadow-frame bookkeeping.
            assert(llvm::isa<ctjs::RootOp>(user));
            user->erase();
        }
        operation->erase();
    };
    // Match each spread call against the importer's shape: a fresh empty array
    // receiver calling `concat`, with an argument array filled by one
    // zero-based while loop that appends nodes[i] for i < nodes.length.
    for (ctjs::CallSpreadOp call : spreads) {
        if (!spend()) { return error("DOM snapshot work budget exhausted"); }
        auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        auto receiver = call.getReceiver().getDefiningOp<ctjs::CreateArrayOp>();
        auto arguments = call.getArgs().getDefiningOp<ctjs::CreateArrayOp>();
        if (!method || !receiver || !arguments || receiver == arguments ||
            !receiver.getElements().empty() || !arguments.getElements().empty() ||
            method.getObject() != receiver.getResult() ||
            ctjs::constantKey(method.getKey()) != "concat" ||
            receiver->getBlock() != call->getBlock() || arguments->getBlock() != call->getBlock() ||
            !usesOnly(receiver, {method, call}) || !usesOnly(method, {call})) {
            return error("DOM snapshot length requires a fresh empty concat receiver");
        }
        ctjs::AppendOp append;
        for (mlir::Operation * user : arguments->getUsers()) {
            if (!spend()) { return error("DOM snapshot work budget exhausted"); }
            if (llvm::isa<ctjs::RootOp>(user) || user == call) { continue; }
            auto put = llvm::dyn_cast<ctjs::AppendOp>(user);
            if (!put || append || put.getArray() != arguments.getResult()) {
                return error("DOM snapshot arguments escape their sole spread append");
            }
            append = put;
        }
        auto loop = append ? llvm::dyn_cast<mlir::scf::WhileOp>(append->getParentOp())
                           : mlir::scf::WhileOp{};
        if (!loop || loop->getBlock() != call->getBlock() || !loop.getBefore().hasOneBlock() ||
            !loop.getAfter().hasOneBlock() || loop.getInits().size() != 1 ||
            loop.getNumResults() != 1 || !numberIs(loop.getInits()[0], 0) ||
            !usesOnly(loop.getResult(0), {}) ||
            !dominance.properlyDominates(arguments.getOperation(), loop.getOperation()) ||
            !dominance.properlyDominates(loop.getOperation(), call.getOperation())) {
            return error("DOM snapshot requires one confined zero-based spread loop");
        }
        auto & before = loop.getBefore().front();
        auto & after = loop.getAfter().front();
        if (before.getNumArguments() != 1 || after.getNumArguments() != 1 ||
            before.getOperations().size() != 3 || after.getOperations().size() != 4) {
            return error("DOM snapshot spread loop contains additional effects");
        }
        auto [compare, read] = matchIndexCopyLoop(loop, append);
        auto length =
            compare ? compare.getRhs().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
        auto iterable =
            length ? length.getObject().getDefiningOp<ctjs::IterableOp>() : ctjs::IterableOp{};
        auto snapshot =
            iterable ? iterable.getSource().getDefiningOp<ctjs::CallOp>() : ctjs::CallOp{};
        if (!compare || !length || ctjs::constantKey(length.getKey()) != "length" || !iterable ||
            !snapshot || read.getObject() != iterable.getResult() ||
            iterable->getBlock() != call->getBlock() || length->getBlock() != call->getBlock() ||
            !dominance.properlyDominates(snapshot.getOperation(), iterable.getOperation()) ||
            !dominance.properlyDominates(iterable.getOperation(), length.getOperation()) ||
            !dominance.properlyDominates(length.getOperation(), loop.getOperation()) ||
            !usesOnly(iterable, {length, read}) || !usesOnly(length, {compare})) {
            return error("DOM snapshot spread must copy every original indexed slot exactly once");
        }
        llvm::SmallVector<ctjs::GetPropertyOp> observations, indices;
        for (mlir::Operation * user : call->getUsers()) {
            if (!spend()) { return error("DOM snapshot work budget exhausted"); }
            if (llvm::isa<ctjs::RootOp>(user)) { continue; }
            auto observation = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
            if (!observation || observation.getObject() != call.getResult()) {
                return error("DOM spread/concat result permits only length and bounded indices");
            }
            if (ctjs::constantKey(observation.getKey()) == "length") {
                observations.push_back(observation);
                continue;
            }
            // The copy may be shorter than the NodeList it now aliases, so an
            // index read needs its own guard against the copy's length; a guard
            // on nodes.length is not enough. DOMEntryAnalysis, run below,
            // checks that the index advances.
            if (!hasOwnSnapshotBound(observation, call.getResult(), spend)) {
                return error("DOM copied index requires its own length guard");
            }
            indices.push_back(observation);
        }
        if (observations.empty()) { return error("DOM snapshot length is not observed"); }
        snapshots.push_back(snapshot);
        mlir::OpBuilder at(call);
        // The copy's length is min(nodes.length, 2^24), the VM's slot cap.
        const auto bounded =
            cappedLength(at, call.getLoc(), call.getType(), snapshot.getResult(), length.getKey());
        for (ctjs::GetPropertyOp observation : observations) {
            observation.getResult().replaceAllUsesWith(bounded);
            observation.erase();
        }
        for (ctjs::GetPropertyOp index : indices) {
            index.getObjectMutable().assign(snapshot.getResult());
        }
        for (mlir::Operation * dead :
             {call.getOperation(), loop.getOperation(), length.getOperation(),
              iterable.getOperation(), arguments.getOperation(), method.getOperation(),
              receiver.getOperation()}) {
            erase(dead);
        }
        dominance.invalidate();
    }
    if (!charge(*changed)) { return error("DOM snapshot work budget exhausted"); }
    auto transformed = contract;
    transformed.moduleSha256 = hostContractFingerprint(*changed);
    const DOMEntryAnalysis proof(*changed, transformed, static_cast<unsigned>(budget.remaining));
    if (!proof.proved()) { return error(proof.reason()); }
    budget.remaining -= proof.steps();
    for (ctjs::CallOp snapshot : snapshots) {
        if (!spend()) { return error("DOM snapshot work budget exhausted"); }
        const auto * edge = proof.call(snapshot);
        if (!edge || !edge->returnsElementVector()) {
            return error("DOM snapshot spread requires an original querySelectorAll result");
        }
    }
    candidate->setAttrs((*changed)->getAttrs());
    candidate.getBodyRegion().takeBody(changed->getBodyRegion());
    return llvm::Error::success();
}

} // namespace ctcompile::ctnative
