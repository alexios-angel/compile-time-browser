#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/DenseSet.h"

namespace ctcompile::ctnative {

llvm::Expected<bool> normalizeDOMIteratorClose(mlir::ModuleOp candidate,
                                               const HostContract & contract, unsigned maxSteps) {
    unsigned remaining = maxSteps;
    const auto spend = [&](uint64_t cost = 1) {
        if (cost > remaining) { return false; }
        remaining -= static_cast<unsigned>(cost);
        return true;
    };
    const auto refuse = [](llvm::StringRef reason) -> llvm::Expected<bool> {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), reason);
    };
    bool replacedClose = false;
    const auto counted = candidate.walk([&](mlir::Operation * operation) {
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
            replacedClose |= store.getName() == "__ctbrowser_iter_close";
        }
        uint64_t cost = uint64_t(1) + operation->getNumOperands() + operation->getNumResults() +
                        operation->getAttrs().size();
        for (auto & region : operation->getRegions()) {
            for (auto & block : region) { cost += uint64_t(1) + block.getNumArguments(); }
        }
        return spend(cost) ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
    });
    const unsigned size = maxSteps - remaining;
    // Reserve fingerprinting, the disposable clone, verification and rewrite.
    if (counted.wasInterrupted() || !spend(uint64_t(size) * 6)) {
        return refuse("DOM iterator close normalization work budget exhausted");
    }
    auto entry = candidate.lookupSymbol<ctjs::FuncOp>(contract.entry);
    if (!entry || entry.getBody().empty()) { return false; }
    bool hasClose = false, hasHandler = false;
    entry.walk([&](mlir::Operation * operation) {
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            hasClose |= load.getName() == "__ctbrowser_iter_close";
        }
        hasHandler |= llvm::isa<ctjs::PushHandlerOp>(operation);
    });
    if (!hasClose || !hasHandler) { return false; }
    if ((contract.provider != HostContract::Provider::ctbrowserDOM &&
         contract.provider != HostContract::Provider::ctbrowserDOMSession) ||
        !llvm::is_contained(contract.initialIntrinsics, "__ctbrowser_iter_close") ||
        replacedClose || candidate->hasAttr("ctjs.skipped") || entry->hasAttr("ctjs.skipped") ||
        contract.moduleSha256 != hostContractFingerprint(candidate)) {
        return refuse("DOM iterator close requires its fingerprinted initial provider binding");
    }
    if (mlir::failed(mlir::verify(entry))) {
        return refuse("DOM iterator close requires a well-formed entry");
    }
    mlir::OwningOpRef<ctjs::FuncOp> copy(llvm::cast<ctjs::FuncOp>(entry->clone()));
    ctjs::PushHandlerOp push;
    ctjs::CatchLandOp landing;
    unsigned pushes = 0, landings = 0;
    for (auto & block : copy->getBody()) {
        for (auto & operation : block) {
            if (!spend()) {
                return refuse("DOM iterator close normalization work budget exhausted");
            }
            if (operation.getNumRegions()) {
                return refuse("DOM iterator close requires the original handler CFG");
            }
            if (auto found = llvm::dyn_cast<ctjs::PushHandlerOp>(operation)) {
                push = found;
                ++pushes;
            }
            if (auto found = llvm::dyn_cast<ctjs::CatchLandOp>(operation)) {
                landing = found;
                ++landings;
            }
        }
    }
    if (pushes != 1 || landings != 1 || push.getHandler() != landing->getBlock() ||
        push.getBody() == push.getHandler() || !push.getBodyOperands().empty() ||
        !push.getHandlerOperands().empty() || !landing->use_empty()) {
        return refuse("DOM iterator close requires one unobserved suppression landing");
    }
    auto * caught = landing->getBlock();
    auto caughtExit = llvm::dyn_cast<mlir::cf::BranchOp>(caught->back());
    if (caught->getNumArguments() || caught->getOperations().size() != 2 ||
        &caught->front() != landing || !caughtExit || !caughtExit.getDestOperands().empty()) {
        return refuse("DOM iterator close catch must only resume the saved throw");
    }
    auto * joined = caughtExit.getDest();
    auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(joined->back());
    mlir::DominanceInfo dominance(*copy);
    if (joined->getNumArguments() || !llvm::hasSingleElement(*joined) || !thrown ||
        !dominance.dominates(thrown.getValue(), push)) {
        return refuse("DOM iterator close must retain its preceding saved throw value");
    }
    llvm::SmallVector<mlir::Block *> normal;
    llvm::DenseSet<mlir::Block *> protectedBlocks;
    llvm::SmallVector<mlir::Operation *> setup;
    ctjs::CallOp close;
    ctjs::PopHandlerOp pop;
    auto * previous = push->getBlock();
    // ponytail: one argument-free straight close chain; retain register-vector
    // handlers until their state correspondence has a separate proof.
    for (auto * block = push.getBody(); block != joined;) {
        if (!spend() || block == caught || block->getNumArguments() ||
            !protectedBlocks.insert(block).second || block->getSinglePredecessor() != previous) {
            return refuse("DOM iterator close requires a private acyclic suppression chain");
        }
        normal.push_back(block);
        for (auto & operation : block->without_terminator()) {
            if (!spend(uint64_t(1) + operation.getNumOperands())) {
                return refuse("DOM iterator close normalization work budget exhausted");
            }
            if (auto found = llvm::dyn_cast<ctjs::PopHandlerOp>(operation)) {
                if (pop || !close) {
                    return refuse("DOM iterator close has misplaced handler removal");
                }
                pop = found;
            } else if (auto found = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                if (close || pop) {
                    return refuse("DOM iterator close requires one protected call");
                }
                close = found;
            } else {
                auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
                if (close || pop ||
                    (!llvm::isa<ctjs::ConstantOp>(operation) &&
                     !(load && load.getName() == "__ctbrowser_iter_close"))) {
                    return refuse("DOM iterator close protection contains another source effect");
                }
                setup.push_back(&operation);
            }
        }
        auto * terminator = block->getTerminator();
        previous = block;
        if (auto check = llvm::dyn_cast<ctjs::CheckOp>(terminator)) {
            if (pop || check.getHandler() != caught || !check.getContOperands().empty() ||
                !check.getHandlerOperands().empty()) {
                return refuse("DOM iterator close status edge changes its suppression scope");
            }
            block = check.getCont();
        } else if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(terminator);
                   branch && pop && branch.getDest() == joined &&
                   branch.getDestOperands().empty()) {
            block = joined;
        } else {
            return refuse("DOM iterator close normal path must pop before the saved throw");
        }
    }
    if (!close || !pop || !close->use_empty()) {
        return refuse("DOM iterator close requires an unobserved call and one handler removal");
    }
    for (auto * block : caught->getPredecessors()) {
        if (!spend()) { return refuse("DOM iterator close normalization work budget exhausted"); }
        auto check = llvm::dyn_cast<ctjs::CheckOp>(block->getTerminator());
        if (block->getTerminator() != push &&
            (!protectedBlocks.contains(block) || !check || check.getHandler() != caught)) {
            return refuse("DOM iterator close landing has an unrelated predecessor");
        }
    }
    for (auto * block : joined->getPredecessors()) {
        if (!spend()) { return refuse("DOM iterator close normalization work budget exhausted"); }
        if (block != caught && block != previous) {
            return refuse("DOM iterator close saved throw has an unrelated predecessor");
        }
    }
    auto callee = close.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
    const auto undefined = [](mlir::Value value) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue());
    };
    auto flag = close.getArgs().size() == 2 ? close.getArgs()[1].getDefiningOp<ctjs::ConstantOp>()
                                            : ctjs::ConstantOp{};
    auto boolean = flag ? llvm::dyn_cast<ctjs::BooleanAttr>(flag.getValue()) : ctjs::BooleanAttr{};
    if (!callee || callee.getName() != "__ctbrowser_iter_close" ||
        !undefined(close.getReceiver()) || !boolean || !boolean.getValue() ||
        !dominance.dominates(close.getArgs()[0], push)) {
        return refuse("DOM iterator close requires its original abrupt helper invocation");
    }
    for (auto * operation : setup) {
        for (auto * user : operation->getUsers()) {
            if (!spend()) {
                return refuse("DOM iterator close normalization work budget exhausted");
            }
            if (!protectedBlocks.contains(user->getBlock())) {
                return refuse("DOM iterator close setup escapes its protected chain");
            }
        }
    }
    // Only initial intrinsic lookup and constants leave the protected chain.
    // A caller must still reprove the full prefix/global/reentry contract on
    // this private candidate before publication. The call itself remains caught.
    mlir::OpBuilder at(push);
    mlir::IRMapping mapping;
    for (auto * operation : setup) { at.clone(*operation, mapping); }
    mlir::OperationState state(close.getLoc(), ctjs::InvokeOp::getOperationName());
    for (unsigned index = 0; index != 3; ++index) { state.addRegion(); }
    auto invocation = llvm::cast<ctjs::InvokeOp>(at.create(state));
    auto & body = invocation.getBody().emplaceBlock();
    mlir::OpBuilder inside = mlir::OpBuilder::atBlockEnd(&body);
    auto call = llvm::cast<ctjs::CallOp>(inside.clone(*close, mapping));
    ctjs::InvokeExitOp::create(inside, close.getLoc(), call.getResult(), mlir::ValueRange{});
    for (auto * region : {&invocation.getNormalBody(), &invocation.getUnwindBody()}) {
        auto & block = region->emplaceBlock();
        block.addArgument(ctjs::ValueType::get(candidate.getContext()), close.getLoc());
        mlir::OpBuilder continuation = mlir::OpBuilder::atBlockEnd(&block);
        ctjs::InvokeYieldOp::create(continuation, close.getLoc(), mlir::ValueRange{});
    }
    mlir::cf::BranchOp::create(at, push.getLoc(), joined);
    push.erase();
    normal.push_back(caught);
    for (auto * block : normal) { block->dropAllReferences(); }
    for (auto * block : normal) { block->erase(); }
    if (mlir::failed(mlir::verify(*copy))) {
        return refuse("DOM iterator close normalization lost source correspondence");
    }
    entry.getBody().takeBody(copy->getBody());
    return true;
}

} // namespace ctcompile::ctnative
