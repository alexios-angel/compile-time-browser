#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Error.h"

namespace ctcompile::ctnative {

llvm::Error normalizeDOMElementGuards(mlir::ModuleOp candidate, const HostContract & contract,
                                      unsigned maxSteps) {
    const auto error = [](llvm::StringRef text) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), text);
    };
    unsigned remaining = maxSteps;
    const auto spend = [&] { return remaining && (--remaining, true); };
    // Charge the source census and fingerprint before inspecting the contract.
    const auto census = candidate.walk([&](mlir::Operation * operation) {
        for (unsigned i = 0; i <= operation->getNumOperands(); ++i) {
            if (!spend() || !spend()) { return mlir::WalkResult::interrupt(); }
        }
        return mlir::WalkResult::advance();
    });
    if (census.wasInterrupted()) { return error("DOM element guard work budget exhausted"); }
    if (hostContractFingerprint(candidate) != contract.moduleSha256 ||
        candidate->hasAttr("ctjs.skipped")) {
        return error("DOM element guard fingerprint mismatch or incomplete source");
    }
    auto entry = candidate.lookupSymbol<ctjs::FuncOp>(contract.entry);
    if ((contract.provider != HostContract::Provider::ctbrowserDOM &&
         contract.provider != HostContract::Provider::ctbrowserDOMSession) ||
        !entry || !entry.getBody().hasOneBlock() ||
        entry.getBody().front().getNumArguments() < ctjs::implicit_arguments ||
        contract.elementParameters.empty() ||
        contract.elementParameters.size() !=
            entry.getBody().front().getNumArguments() - ctjs::implicit_arguments) {
        return error("DOM element guard requires a complete element entry declaration");
    }
    llvm::DenseMap<mlir::Value, bool> truth;
    for (auto [position, index] : llvm::enumerate(contract.elementParameters)) {
        if (!spend()) { return error("DOM element guard work budget exhausted"); }
        if (position != index) {
            return error("DOM element guard requires ordered explicit element parameters");
        }
        truth[entry.getBody().front().getArgument(index + ctjs::implicit_arguments)] = true;
    }
    // Only exact input SSA identities seed truthiness. In particular, nullable
    // closest results, cells, joins and loop-carried values acquire no facts.
    llvm::SmallVector<std::pair<ctjs::UnaryOp, bool>> negations;
    llvm::SmallVector<std::pair<ctjs::TruthyOp, bool>> queries;
    llvm::SmallVector<std::pair<mlir::scf::IfOp, bool>> branches;
    const auto scan = entry.walk<mlir::WalkOrder::PostOrder>([&](mlir::Operation * operation) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        for (mlir::Value result : operation->getResults()) {
            for ([[maybe_unused]] mlir::OpOperand & use : result.getUses()) {
                if (!spend()) { return mlir::WalkResult::interrupt(); }
            }
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
            unary && unary.getKind() == ctjs::UnaryKind::Not) {
            if (auto found = truth.find(unary.getOperand()); found != truth.end()) {
                const bool value = !found->second;
                truth[unary.getResult()] = value;
                negations.emplace_back(unary, value);
            }
        }
        if (auto query = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
            if (auto found = truth.find(query.getValue()); found != truth.end()) {
                const bool value = found->second;
                truth[query.getResult()] = value;
                queries.emplace_back(query, value);
            }
        }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            if (auto found = truth.find(branch.getCondition()); found != truth.end()) {
                branches.emplace_back(branch, found->second);
            }
        }
        return mlir::WalkResult::advance();
    });
    if (scan.wasInterrupted()) { return error("DOM element guard work budget exhausted"); }
    for (auto [branch, selected] : branches) {
        auto & arm = selected ? branch.getThenRegion() : branch.getElseRegion();
        if (!arm.hasOneBlock() || arm.front().getNumArguments() || arm.front().empty()) {
            return error("DOM element guard lacks an exact selected arm");
        }
        auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(arm.front().back());
        if (!yield || yield.getOperandTypes() != branch.getResultTypes()) {
            return error("DOM element guard lost its value correspondence");
        }
    }
    // All work and replacements are known before mutation. The caller owns this
    // private clone and must still reprove the complete live DOM entry.
    for (auto [unary, value] : negations) {
        mlir::OpBuilder at(unary);
        auto constant = ctjs::ConstantOp::create(
            at, unary.getLoc(), ctjs::BooleanAttr::get(candidate.getContext(), value));
        unary.getResult().replaceAllUsesWith(constant.getResult());
        unary.erase();
    }
    for (auto [query, value] : queries) {
        mlir::OpBuilder at(query);
        auto constant = mlir::arith::ConstantIntOp::create(at, query.getLoc(), value, 1);
        query.getResult().replaceAllUsesWith(constant.getResult());
        query.erase();
    }
    for (auto [branch, selected] : branches) {
        auto & arm = selected ? branch.getThenRegion() : branch.getElseRegion();
        auto yield = llvm::cast<mlir::scf::YieldOp>(arm.front().back());
        for (auto [result, value] : llvm::zip(branch.getResults(), yield.getOperands())) {
            result.replaceAllUsesWith(value);
        }
        auto & contents = arm.front().getOperations();
        branch->getBlock()->getOperations().splice(branch->getIterator(), contents,
                                                   contents.begin(), yield->getIterator());
        branch.erase();
    }
    return llvm::Error::success();
}

llvm::Error normalizeDOMIteration(mlir::ModuleOp candidate, const HostContract & contract,
                                  unsigned maxSteps) {
    // Only the private preparation clone is changed. Its complete DOM proof
    // must still reject writes, reentry, escaped snapshots and unknown calls.
    unsigned remaining = maxSteps;
    const auto spend = [&] { return remaining && (--remaining, true); };
    const auto error = [](llvm::StringRef text) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), text);
    };
    llvm::SmallVector<ctjs::CallOp> opens;
    bool hasLoop = false;
    const auto census = candidate.walk([&](mlir::Operation * operation) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        if (llvm::isa<mlir::scf::WhileOp>(operation)) {
            auto owner = operation->getParentOfType<ctjs::FuncOp>();
            hasLoop |= owner && owner.getSymName() == contract.entry;
        }
        auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
        auto load =
            call ? call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
        if (load && load.getName() == "__ctbrowser_for_of_open") { opens.push_back(call); }
        return mlir::WalkResult::advance();
    });
    if (census.wasInterrupted()) { return error("DOM iteration work budget exhausted"); }
    if (opens.empty()) { return llvm::Error::success(); }
    for (llvm::StringRef name :
         {"Array", "__ctbrowser_for_of_open", "__ctbrowser_iter_next", "__ctbrowser_iter_close"}) {
        if (!llvm::is_contained(contract.initialIntrinsics, name)) {
            return error("DOM iteration requires original Array iterator and helper identities");
        }
    }
    const auto undefined = [](mlir::Value value) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue());
    };
    auto entry = candidate.lookupSymbol<ctjs::FuncOp>(contract.entry);
    // ponytail: conditional and sequential snapshots only. Loop-nested opens
    // need a proof of their changing prefix and scalar state.
    if (!entry || !hasLoop || !entry.getBody().hasOneBlock()) {
        return error("DOM iteration requires a structured source entry");
    }
    for (ctjs::CallOp open : opens) {
        auto * block = open->getBlock();
        while (block != &entry.getBody().front()) {
            if (!spend()) { return error("DOM iteration work budget exhausted"); }
            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(block->getParentOp());
            if (!branch || !block->getParent()->hasOneBlock() || block->getNumArguments()) {
                return error("DOM iteration requires sequential or conditional source iterators");
            }
            block = branch->getBlock();
        }
    }
    const auto normalize = [&](ctjs::CallOp open) -> llvm::Error {
        if (!undefined(open.getReceiver()) || open.getArgs().size() != 1) {
            return error("DOM iterator open requires one snapshot and an undefined receiver");
        }

        // Reuse the complete dataset/filter proof, rather than a second recursive
        // recognizer. Return the saved snapshot from an otherwise unchanged prefix.
        const auto chargeClone = candidate.walk([&](mlir::Operation * operation) {
            for (unsigned i = 0; i <= operation->getNumOperands(); ++i) {
                if (!spend()) { return mlir::WalkResult::interrupt(); }
            }
            return mlir::WalkResult::advance();
        });
        if (chargeClone.wasInterrupted()) { return error("DOM iteration work budget exhausted"); }
        mlir::IRMapping mapping;
        mlir::OwningOpRef<mlir::ModuleOp> prefix =
            llvm::cast<mlir::ModuleOp>(candidate->clone(mapping));
        auto copiedOpen = llvm::cast<ctjs::CallOp>(mapping.lookup(open.getOperation()));
        auto snapshot = copiedOpen.getArgs().front();
        auto snapshotCall = snapshot.getDefiningOp<ctjs::CallOp>();
        if (!snapshotCall) { return error("DOM iterator input is not a proved owning snapshot"); }
        auto * body = copiedOpen->getBlock();
        while (&body->back() != copiedOpen) {
            if (!spend()) { return error("DOM iteration work budget exhausted"); }
            body->back().erase();
        }
        copiedOpen.erase();
        // The prefix witnesses just the path reaching this open. Keep each
        // condition producer and dominating operation, but discard its suffix
        // and other arm only in this private proof. The final entry proof still
        // checks both original arms, including effects and joined scalar state.
        while (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(body->getParentOp())) {
            auto * parent = branch->getBlock();
            while (&parent->back() != branch) {
                if (!spend()) { return error("DOM iteration work budget exhausted"); }
                parent->back().erase();
            }
            if (!spend()) { return error("DOM iteration work budget exhausted"); }
            parent->getOperations().splice(branch->getIterator(), body->getOperations());
            branch.erase();
            body = parent;
        }
        ctjs::FrameEnterOp frame;
        for (mlir::Operation & operation : *body) {
            if (!spend()) { return error("DOM iteration work budget exhausted"); }
            if (auto entered = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) { frame = entered; }
            if (llvm::isa<ctjs::FrameExitOp>(operation)) { frame = {}; }
        }
        auto copiedLoad =
            llvm::cast<ctjs::LoadGlobalOp>(mapping.lookup(open.getCallee().getDefiningOp()));
        if (copiedLoad.getResult().use_empty()) { copiedLoad.erase(); }
        mlir::OpBuilder at(body, body->end());
        if (frame) { ctjs::FrameExitOp::create(at, open.getLoc(), frame.getContext()); }
        ctjs::ReturnOp::create(at, open.getLoc(), snapshot);
        HostContract prefixContract = contract;
        const auto chargeFingerprint = prefix->walk([&](mlir::Operation * operation) {
            for (unsigned i = 0; i <= operation->getNumOperands(); ++i) {
                if (!spend()) { return mlir::WalkResult::interrupt(); }
            }
            return mlir::WalkResult::advance();
        });
        if (chargeFingerprint.wasInterrupted()) {
            return error("DOM iteration work budget exhausted");
        }
        prefixContract.moduleSha256 = hostContractFingerprint(*prefix);
        const DOMEntryAnalysis proof(*prefix, prefixContract, remaining);
        if (!proof.proved()) {
            return error(("DOM iterator snapshot prefix: " + proof.reason()).str());
        }
        remaining -= proof.steps();
        const auto * edge = proof.call(snapshotCall);
        if (!edge || !edge->returnsStringVector()) {
            return error("DOM iterator input is not a proved owning String snapshot");
        }
        const mlir::Value originalSnapshot = open.getArgs().front();
        auto * iterationBody = open->getBlock();
        at.setInsertionPoint(open);
        for (mlir::OpOperand & use : open.getResult().getUses()) {
            (void)use;
            if (!spend()) { return error("DOM iteration work budget exhausted"); }
        }
        auto empty = ctjs::ConstantOp::create(at, open.getLoc(),
                                              ctjs::UndefinedAttr::get(candidate.getContext()));
        open.getResult().replaceAllUsesWith(empty.getResult());
        open.erase();

        // Select only exact constant branches. Unselected effects are unreachable
        // because original Array iteration on this dense, immutable snapshot cannot
        // use the protocol-record path. No user callback or browser code runs here.
        bool changed = true;
        while (changed) {
            changed = false;
            llvm::SmallVector<mlir::Operation *> operations;
            const auto scan =
                iterationBody->walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * op) {
                    if (!spend()) { return mlir::WalkResult::interrupt(); }
                    operations.push_back(op);
                    return mlir::WalkResult::advance();
                });
            if (scan.wasInterrupted()) { return error("DOM iteration work budget exhausted"); }
            // Restart after each mutation so no saved pointer can name an erased arm.
            for (mlir::Operation * operation : operations) {
                if (!spend()) { return error("DOM iteration work budget exhausted"); }
                for (mlir::Value result : operation->getResults()) {
                    for (mlir::OpOperand & use : result.getUses()) {
                        (void)use;
                        if (!spend()) { return error("DOM iteration work budget exhausted"); }
                    }
                }
                at.setInsertionPoint(operation);
                if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
                    if (compare.getKind() == ctjs::CompareKind::StrictEq &&
                        undefined(compare.getLhs()) && undefined(compare.getRhs())) {
                        auto value = ctjs::ConstantOp::create(
                            at, compare.getLoc(),
                            ctjs::BooleanAttr::get(candidate.getContext(),
                                                   compare.getKind() ==
                                                       ctjs::CompareKind::StrictEq));
                        compare.getResult().replaceAllUsesWith(value.getResult());
                        compare.erase();
                        changed = true;
                        break;
                    }
                }
                if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
                    auto constant = truth.getValue().getDefiningOp<ctjs::ConstantOp>();
                    auto boolean = constant ? llvm::dyn_cast<ctjs::BooleanAttr>(constant.getValue())
                                            : ctjs::BooleanAttr{};
                    if (boolean) {
                        auto value = mlir::arith::ConstantIntOp::create(at, truth.getLoc(),
                                                                        boolean.getValue(), 1);
                        truth.getResult().replaceAllUsesWith(value.getResult());
                        truth.erase();
                        changed = true;
                        break;
                    }
                }
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    auto constant = branch.getCondition().getDefiningOp<mlir::arith::ConstantOp>();
                    auto boolean = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                            : mlir::IntegerAttr{};
                    if (boolean && boolean.getType().isInteger(1)) {
                        auto & selected =
                            boolean.getInt() ? branch.getThenRegion() : branch.getElseRegion();
                        if (!selected.hasOneBlock() || selected.front().getNumArguments()) {
                            return error("DOM iterator branch lacks an exact selected arm");
                        }
                        auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(selected.front().back());
                        if (!yield || yield.getOperandTypes() != branch.getResultTypes()) {
                            return error("DOM iterator branch lost its value correspondence");
                        }
                        for (auto [result, value] :
                             llvm::zip(branch.getResults(), yield.getOperands())) {
                            result.replaceAllUsesWith(value);
                        }
                        auto & contents = selected.front().getOperations();
                        branch->getBlock()->getOperations().splice(branch->getIterator(), contents,
                                                                   contents.begin(),
                                                                   yield->getIterator());
                        branch.erase();
                        changed = true;
                        break;
                    }
                }
                if (auto iterable = llvm::dyn_cast<ctjs::IterableOp>(operation);
                    iterable && iterable->getOperand(0) == originalSnapshot) {
                    iterable.getResult().replaceAllUsesWith(originalSnapshot);
                    iterable.erase();
                    changed = true;
                    break;
                }
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                    auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                    if (load && load.getName() == "__ctbrowser_iter_close" &&
                        undefined(call.getReceiver()) && call.getArgs().size() == 2 &&
                        call.getArgs()[0] == empty.getResult()) {
                        auto constant = call.getArgs()[1].getDefiningOp<ctjs::ConstantOp>();
                        auto abrupt = constant
                                          ? llvm::dyn_cast<ctjs::BooleanAttr>(constant.getValue())
                                          : ctjs::BooleanAttr{};
                        if (!abrupt || abrupt.getValue()) {
                            return error("DOM iterator close requires normal completion");
                        }
                        call.getResult().replaceAllUsesWith(empty.getResult());
                        call.erase();
                        changed = true;
                        break;
                    }
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
                    load && load.getResult().use_empty() &&
                    (load.getName() == "__ctbrowser_for_of_open" ||
                     load.getName() == "__ctbrowser_iter_close")) {
                    load.erase();
                    changed = true;
                    break;
                }
            }
        }
        return llvm::Error::success();
    };
    // Source order matters: each prefix includes every earlier normalized loop,
    // so the same complete proof checks effects and snapshot state between opens.
    while (!opens.empty()) {
        if (auto failure = normalize(opens.front())) { return failure; }
        // Constant branch folding can erase another open in an unreachable arm.
        // Recollect live operations instead of retaining pointers into that arm.
        opens.clear();
        const auto scan = entry.walk([&](mlir::Operation * operation) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
            auto load =
                call ? call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
            if (load && load.getName() == "__ctbrowser_for_of_open") { opens.push_back(call); }
            return mlir::WalkResult::advance();
        });
        if (scan.wasInterrupted()) { return error("DOM iteration work budget exhausted"); }
    }
    return llvm::Error::success();
}

} // namespace ctcompile::ctnative
