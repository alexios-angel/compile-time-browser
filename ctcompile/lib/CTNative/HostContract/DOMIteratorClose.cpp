#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "mlir/Conversion/ControlFlowToSCF/ControlFlowToSCF.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Transforms/CFGToSCF.h"
#include "mlir/Transforms/RegionUtils.h"
#include "llvm/ADT/DenseSet.h"

#include <algorithm>
#include <tuple>

namespace ctcompile::ctnative {

// The caller owns a disposable clone. Preserve both source completions while
// upstream structures their common CFG; no native effect or payload is proved here.
static llvm::Error structureCloseCompletion(ctjs::FuncOp function, unsigned & remaining) {
    const auto refuse = [](llvm::StringRef reason) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), reason);
    };
    bool returns = false;
    uint64_t size = 0, width = 8;
    for (auto & block : function.getBody()) {
        returns |= llvm::isa<ctjs::ReturnOp>(block.getTerminator());
        width = std::max(width, uint64_t(block.getNumArguments()) + 8);
        for (auto & operation : block) {
            size += uint64_t(1) + operation.getNumOperands() + operation.getNumResults();
        }
    }
    if (!returns) { return llvm::Error::success(); }
    // Upstream has no work callback. Reserve its quadratic CFG work and the
    // terminal-region rewrite before touching this clone.
    const uint64_t blocks = function.getBody().getBlocks().size() + 1;
    if (size > remaining / 4) { return refuse("DOM iterator completion work budget exhausted"); }
    remaining -= static_cast<unsigned>(size * 4);
    if (blocks > remaining / width / blocks) {
        return refuse("DOM iterator completion work budget exhausted");
    }
    remaining -= static_cast<unsigned>(blocks * blocks * width);
    auto * context = function.getContext();
    context->getOrLoadDialect<mlir::arith::ArithDialect>();
    context->getOrLoadDialect<mlir::scf::SCFDialect>();
    context->getOrLoadDialect<mlir::ub::UBDialect>();
    mlir::IRRewriter rewriter(context);
    (void)mlir::simplifyRegions(rewriter, function->getRegions());
    mlir::DominanceInfo dominance(function);
    mlir::ControlFlowToSCFTransformation transformation;
    mlir::FailureOr<bool> lifted = mlir::failure();
    {
        mlir::ScopedDiagnosticHandler quiet(context,
                                            [](mlir::Diagnostic &) { return mlir::success(); });
        lifted = mlir::transformCFGToSCF(function.getBody(), transformation, dominance);
    }
    if (mlir::failed(lifted)) {
        return refuse("DOM iterator completion CFG could not be structured");
    }
    auto & root = function.getBody().front();
    auto * dispatch = root.getTerminator();
    auto switcher = llvm::dyn_cast<mlir::cf::SwitchOp>(dispatch);
    auto branch = llvm::dyn_cast<mlir::cf::CondBranchOp>(dispatch);
    // ponytail: the two distinct terminal kinds produced by CFG-to-SCF;
    // keep broader terminal graphs until their region correspondence is proved.
    if (function.getBody().getBlocks().size() != 3 ||
        (!branch && (!switcher || switcher.getCaseDestinations().size() != 1))) {
        return refuse("DOM iterator completion requires one throw/return dispatch");
    }
    auto * normal = branch ? branch.getTrueDest() : switcher.getCaseDestinations().front();
    auto * fallback = branch ? branch.getFalseDest() : switcher.getDefaultDestination();
    auto operands = branch ? branch.getTrueDestOperands() : switcher.getCaseOperands(0);
    auto fallbackOperands = branch ? branch.getFalseDestOperands() : switcher.getDefaultOperands();
    if (normal == fallback || normal == &root || fallback == &root ||
        normal->getSinglePredecessor() != &root || fallback->getSinglePredecessor() != &root ||
        !((llvm::isa<ctjs::ReturnOp>(normal->getTerminator()) &&
           llvm::isa<ctjs::ThrowOp>(fallback->getTerminator())) ||
          (llvm::isa<ctjs::ThrowOp>(normal->getTerminator()) &&
           llvm::isa<ctjs::ReturnOp>(fallback->getTerminator())))) {
        return refuse("DOM iterator completion lost its distinct source exits");
    }
    mlir::OpBuilder at(dispatch);
    mlir::Value selected;
    if (branch) {
        selected = branch.getCondition();
    } else {
        auto key = mlir::arith::ConstantOp::create(
            at, dispatch->getLoc(), switcher.getFlag().getType(),
            at.getIntegerAttr(switcher.getFlag().getType(),
                              *switcher.getCaseValues()->getValues<llvm::APInt>().begin()));
        selected = mlir::arith::CmpIOp::create(
            at, dispatch->getLoc(), mlir::arith::CmpIPredicate::eq, switcher.getFlag(), key);
    }
    auto choice = mlir::scf::IfOp::create(at, dispatch->getLoc(), function.getResultTypes(),
                                          selected, false, false);
    for (auto [block, region, incoming] :
         {std::tuple{normal, &choice.getThenRegion(), operands},
          std::tuple{fallback, &choice.getElseRegion(), fallbackOperands}}) {
        if (block->getNumArguments() != incoming.size()) {
            return refuse("DOM iterator completion lost its incoming values");
        }
        for (auto [argument, value] : llvm::zip(block->getArguments(), incoming)) {
            argument.replaceAllUsesWith(value);
        }
        block->eraseArguments(0, block->getNumArguments());
        block->moveBefore(region, region->end());
        auto * terminal = block->getTerminator();
        mlir::OpBuilder end(terminal);
        mlir::Value value = terminal->getOperand(0);
        if (llvm::isa<ctjs::ThrowOp>(terminal)) {
            auto abrupt = mlir::scf::ExecuteRegionOp::create(end, terminal->getLoc(),
                                                             mlir::TypeRange{}, true);
            auto & body = abrupt.getRegion().emplaceBlock();
            terminal->moveBefore(&body, body.end());
            end.setInsertionPointToEnd(block);
            // This yield is structurally required and unreachable. The actual
            // throw remains a terminator; only the real return supplies a result.
            value = mlir::ub::PoisonOp::create(end, terminal->getLoc(), value.getType());
        } else {
            terminal->erase();
            end.setInsertionPointToEnd(block);
        }
        mlir::scf::YieldOp::create(end, dispatch->getLoc(), value);
    }
    ctjs::ReturnOp::create(at, dispatch->getLoc(), choice.getResult(0));
    dispatch->erase();
    function->removeAttr("ctjs.not_structured");
    return llvm::Error::success();
}

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
    if (pushes == 1 && landings == 1 && push.getHandler() == landing->getBlock() &&
        !landing.getThrown().use_empty()) {
        return refuse("DOM iterator observing catch requires call/check payload and state proof");
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
    if (auto error = structureCloseCompletion(*copy, remaining)) { return std::move(error); }
    if (mlir::failed(mlir::verify(*copy))) {
        return refuse("DOM iterator close normalization lost source correspondence");
    }
    entry.getBody().takeBody(copy->getBody());
    return true;
}

} // namespace ctcompile::ctnative
