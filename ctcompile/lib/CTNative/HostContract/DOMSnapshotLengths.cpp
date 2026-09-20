#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"

#include <bit>

namespace ctcompile::ctnative {

llvm::Error normalizeDOMSnapshotLengths(mlir::ModuleOp candidate, const HostContract & contract,
                                        unsigned maxSteps) {
    unsigned remaining = maxSteps;
    const auto error = [&](llvm::StringRef text) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       remaining ? text : "DOM snapshot work budget exhausted");
    };
    const auto spend = [&] { return remaining && (--remaining, true); };
    const auto charge = [&](mlir::ModuleOp module) {
        return !module
                    .walk([&](mlir::Operation * operation) {
                        for (unsigned i = 0; i <= operation->getNumOperands(); ++i) {
                            if (!spend() || !spend()) { return mlir::WalkResult::interrupt(); }
                        }
                        return mlir::WalkResult::advance();
                    })
                    .wasInterrupted();
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
    mlir::OwningOpRef<mlir::ModuleOp> changed(candidate.clone());
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
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        auto number =
            constant ? llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue()) : ctjs::NumberAttr{};
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
        auto compare = llvm::dyn_cast<ctjs::CompareOp>(before.front());
        auto truth = llvm::dyn_cast<ctjs::TruthyOp>(before.front().getNextNode());
        auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(before.back());
        auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(after.front());
        auto advance = llvm::dyn_cast<ctjs::BinaryStaticOp>(append->getNextNode());
        auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(after.back());
        auto length =
            compare ? compare.getRhs().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
        auto iterable =
            length ? length.getObject().getDefiningOp<ctjs::IterableOp>() : ctjs::IterableOp{};
        auto snapshot =
            iterable ? iterable.getSource().getDefiningOp<ctjs::CallOp>() : ctjs::CallOp{};
        if (!compare || compare.getKind() != ctjs::CompareKind::Lt ||
            compare.getLhs() != before.getArgument(0) || !truth ||
            truth.getValue() != compare.getResult() || !condition ||
            condition.getCondition() != truth.getResult() || condition.getArgs().size() != 1 ||
            condition.getArgs()[0] != before.getArgument(0) || !read ||
            read->getNextNode() != append || read.getKey() != after.getArgument(0) ||
            append.getElement() != read.getResult() || !advance ||
            advance.getKind() != ctjs::BinaryKind::Add ||
            advance.getLhs() != after.getArgument(0) || !numberIs(advance.getRhs(), 1) || !yield ||
            yield.getNumOperands() != 1 || yield.getOperand(0) != advance.getResult() || !length ||
            ctjs::constantKey(length.getKey()) != "length" || !iterable || !snapshot ||
            read.getObject() != iterable.getResult() || iterable->getBlock() != call->getBlock() ||
            length->getBlock() != call->getBlock() ||
            !dominance.properlyDominates(snapshot.getOperation(), iterable.getOperation()) ||
            !dominance.properlyDominates(iterable.getOperation(), length.getOperation()) ||
            !dominance.properlyDominates(length.getOperation(), loop.getOperation()) ||
            !usesOnly(iterable, {length, read}) || !usesOnly(length, {compare})) {
            return error("DOM snapshot spread must copy every original indexed slot exactly once");
        }
        llvm::SmallVector<ctjs::GetPropertyOp> observations;
        for (mlir::Operation * user : call->getUsers()) {
            if (!spend()) { return error("DOM snapshot work budget exhausted"); }
            if (llvm::isa<ctjs::RootOp>(user)) { continue; }
            auto observation = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
            if (!observation || observation.getObject() != call.getResult() ||
                ctjs::constantKey(observation.getKey()) != "length") {
                return error("DOM spread/concat result permits only length observations");
            }
            observations.push_back(observation);
        }
        if (observations.empty()) { return error("DOM snapshot length is not observed"); }
        snapshots.push_back(snapshot);
        mlir::OpBuilder at(call);
        const auto where = call.getLoc();
        auto size = ctjs::GetPropertyOp::create(at, where, call.getType(), snapshot.getResult(),
                                                length.getKey());
        // The VM materializes at most 2^24 proxy slots. Shell indices above
        // 1,000,000 produce undefined, which still contributes one concat slot.
        // No value, identity or hook observation survived the preceding proof.
        auto cap = ctjs::ConstantOp::create(
            at, where,
            ctjs::NumberAttr::get(candidate.getContext(),
                                  std::bit_cast<uint64_t>(double(1U << 24))));
        auto less = ctjs::CompareOp::create(at, where, call.getType(), ctjs::CompareKind::Lt,
                                            size.getResult(), cap.getResult());
        auto test = ctjs::TruthyOp::create(at, where, at.getI1Type(), less.getResult());
        auto bounded = mlir::scf::IfOp::create(at, where, call->getResultTypes(), test.getResult());
        for (auto [region, value] :
             llvm::zip(bounded->getRegions(), llvm::ArrayRef<mlir::Value>{size, cap})) {
            auto & arm = region.emplaceBlock();
            mlir::OpBuilder nested(&arm, arm.begin());
            mlir::scf::YieldOp::create(nested, where, value);
        }
        for (ctjs::GetPropertyOp observation : observations) {
            observation.getResult().replaceAllUsesWith(bounded.getResult(0));
            observation.erase();
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
    const DOMEntryAnalysis proof(*changed, transformed, remaining);
    if (!proof.proved()) { return error(proof.reason()); }
    remaining -= proof.steps();
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
