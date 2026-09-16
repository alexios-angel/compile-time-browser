#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/Support/Error.h"

namespace ctcompile::ctnative {

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
    // ponytail: one original top-level snapshot iterator. Nested/multiple
    // iterators need separate prefix and scalar-state proofs.
    if (!entry || !hasLoop || opens.size() != 1 ||
        opens.front()->getBlock() != &entry.getBody().front()) {
        return error("DOM iteration requires one top-level source iterator");
    }
    auto open = opens.front();
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
    auto & body = *copiedOpen->getBlock();
    ctjs::FrameEnterOp frame;
    for (mlir::Operation & operation : body) {
        if (!spend()) { return error("DOM iteration work budget exhausted"); }
        if (&operation == copiedOpen) { break; }
        if (auto entered = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) { frame = entered; }
        if (llvm::isa<ctjs::FrameExitOp>(operation)) { frame = {}; }
    }
    while (&body.back() != copiedOpen) {
        if (!spend()) { return error("DOM iteration work budget exhausted"); }
        body.back().erase();
    }
    copiedOpen.erase();
    auto copiedLoad =
        llvm::cast<ctjs::LoadGlobalOp>(mapping.lookup(open.getCallee().getDefiningOp()));
    if (copiedLoad.getResult().use_empty()) { copiedLoad.erase(); }
    mlir::OpBuilder at(&body, body.end());
    if (frame) { ctjs::FrameExitOp::create(at, open.getLoc(), frame.getContext()); }
    ctjs::ReturnOp::create(at, open.getLoc(), snapshot);
    HostContract prefixContract = contract;
    const auto chargeFingerprint = prefix->walk([&](mlir::Operation * operation) {
        for (unsigned i = 0; i <= operation->getNumOperands(); ++i) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
        }
        return mlir::WalkResult::advance();
    });
    if (chargeFingerprint.wasInterrupted()) { return error("DOM iteration work budget exhausted"); }
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
        const auto scan = entry.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * op) {
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
                                               compare.getKind() == ctjs::CompareKind::StrictEq));
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
                    branch->getBlock()->getOperations().splice(
                        branch->getIterator(), contents, contents.begin(), yield->getIterator());
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
                    auto abrupt = constant ? llvm::dyn_cast<ctjs::BooleanAttr>(constant.getValue())
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
}

} // namespace ctcompile::ctnative
