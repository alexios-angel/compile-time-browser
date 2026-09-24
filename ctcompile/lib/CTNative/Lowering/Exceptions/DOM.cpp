#include "../../../CTJS/Lowering/Globals/RegisterFlow.h"
#include "Recovery.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/DenseSet.h"

#include <cstdint>
#include <tuple>
#include <utility>

namespace ctcompile::ctnative::lowering_detail {
namespace {

struct DOMURI {
    ctjs::FuncOp function;
    ExceptionInvocationSource source;
    unsigned remaining;
    std::string reason;
    mlir::DominanceInfo dominance;
    llvm::DenseSet<mlir::Operation *> visited;
    llvm::DenseSet<mlir::Block *> reached;
    mlir::Value copiedFrame;
    llvm::SmallVector<ctjs::LoadGlobalOp> jsonLoads;
    // JSON.parse is admitted only when the contract binds the initial JSON.
    bool json = false;

    DOMURI(ctjs::FuncOp function, unsigned maxSteps, bool json)
        : function(function), remaining(maxSteps), dominance(function), json(json) {}

    ctjs::CheckOp chained(mlir::Operation * operation) const {
        for (const auto & [call, check] : source.chain) {
            if (call == operation) { return check; }
        }
        return {};
    }
    // The original member lookup precedes both calls: JSON.parse is read once
    // from the initial JSON object before decodeURIComponent evaluates.
    bool jsonLookup(mlir::Operation & operation) {
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            return json && load.getName() == "JSON";
        }
        auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
        if (!json || !read || ctjs::constantKey(read.getKey()) != "parse") { return false; }
        // Original check edges carry JSON through the complete register vector.
        // Every incoming definition must still be the same initial load.
        for (auto load : jsonLoads) {
            unsigned used = 0;
            const bool found = ctjs::globals_detail::registerFlowHasOrigin(
                read.getObject(), load.getResult(), remaining, &used);
            if (!spend(used)) { return false; }
            if (found) { return true; }
            if (!remaining) { return spend(); }
        }
        return false;
    }

    bool refuse(llvm::StringRef message) {
        if (reason.empty()) { reason = message.str(); }
        return false;
    }
    bool spend(uint64_t amount = 1) {
        if (amount > remaining) { return refuse("DOM URI normalization work budget exhausted"); }
        remaining -= static_cast<unsigned>(amount);
        return true;
    }
    bool copyCost(const mlir::IRMapping & mapping) {
        return spend(uint64_t(mapping.getValueMap().size()) + mapping.getOperationMap().size());
    }
    bool transfer(mlir::Block * block, mlir::ValueRange operands, mlir::IRMapping & mapping) {
        if (!spend(uint64_t(1) + operands.size()) || operands.size() != block->getNumArguments()) {
            return refuse("DOM URI continuation lost its complete register vector");
        }
        // Read the complete old vector before replacing any argument mapping.
        llvm::SmallVector<mlir::Value> actuals;
        for (auto [operand, argument] : llvm::zip(operands, block->getArguments())) {
            if (operand.getType() != argument.getType() || !mapping.contains(operand)) {
                return refuse("DOM URI continuation has an unmapped source register");
            }
            actuals.push_back(mapping.lookup(operand));
        }
        for (auto [argument, actual] : llvm::zip(block->getArguments(), actuals)) {
            mapping.map(argument, actual);
        }
        return true;
    }
    bool operands(mlir::Operation & operation, const mlir::IRMapping & mapping) {
        for (mlir::Value operand : operation.getOperands()) {
            if (!spend()) { return false; }
            if (!mapping.contains(operand) || !dominance.dominates(operand, &operation)) {
                return refuse("DOM URI operand has no preceding source definition");
            }
            if (operand == source.frame.getContext() &&
                !llvm::isa<ctjs::RootOp, ctjs::FrameExitOp>(operation)) {
                return refuse("DOM URI source observes its shadow frame");
            }
        }
        return true;
    }
    bool proveTailEffects() {
        // Identity and String inputs are reproved over the complete final DOM
        // source. This independent effect whitelist justifies every removed
        // non-call status edge, including preparation and the caught tail.
        llvm::DenseSet<mlir::Block *> blocks;
        for (auto * block : source.normal) {
            if (!spend()) { return false; }
            blocks.insert(block);
            if (!json) { continue; }
            for (mlir::Operation & operation : *block) {
                if (!spend()) { return false; }
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
                    load && load.getName() == "JSON") {
                    jsonLoads.push_back(load);
                }
            }
        }
        for (auto * block : source.caught) {
            if (!spend()) { return false; }
            blocks.insert(block);
        }
        for (auto * block : blocks) {
            for (mlir::Operation & operation : *block) {
                if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
                if (chained(&operation) || jsonLookup(operation) ||
                    llvm::isa<ctjs::ConstantOp, ctjs::RootOp, ctjs::FrameExitOp, ctjs::PopHandlerOp,
                              ctjs::CatchLandOp, ctjs::CheckOp, ctjs::ReturnOp, mlir::cf::BranchOp>(
                        operation)) {
                    continue;
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
                    load &&
                    (load.getName() == "decodeURIComponent" || load.getName() == "undefined")) {
                    continue;
                }
                return refuse("DOM URI protected or caught operation lacks an independent "
                              "nonthrowing effect proof");
            }
        }
        return true;
    }

    mlir::Value invoke(mlir::Operation * call, ctjs::CheckOp check, mlir::IRMapping & mapping,
                       mlir::OpBuilder & at, unsigned depth) {
        if (!spend(uint64_t(8) + call->getNumOperands()) || !copyCost(mapping)) { return {}; }
        const auto type = ctjs::ValueType::get(function.getContext());
        mlir::OperationState state(call->getLoc(), ctjs::InvokeOp::getOperationName());
        state.addTypes(type);
        for (unsigned index = 0; index != 3; ++index) { state.addRegion(); }
        auto invocation = llvm::cast<ctjs::InvokeOp>(at.create(state));
        auto & body = invocation.getBody().emplaceBlock();
        auto & normal = invocation.getNormalBody().emplaceBlock();
        auto & caught = invocation.getUnwindBody().emplaceBlock();
        normal.addArgument(type, invocation.getLoc());
        auto payload = caught.addArgument(type, invocation.getLoc());
        mlir::IRMapping callMapping(mapping);
        mlir::OpBuilder callAt = mlir::OpBuilder::atBlockEnd(&body);
        auto * copied = callAt.clone(*call, callMapping);
        mlir::OperationState exit(invocation.getLoc(), ctjs::InvokeExitOp::getOperationName());
        exit.addOperands(copied->getResult(0));
        callAt.create(exit);
        visited.insert(check);

        // The original full pre-call vector was inspected before any rewrite.
        // Its SSA values dominate both continuations, so the catch can capture
        // them directly. The failed call's result never enters that mapping.
        for (auto [destination, failed] : {std::pair{&normal, false}, std::pair{&caught, true}}) {
            if (!copyCost(mapping)) { return {}; }
            mlir::IRMapping path(mapping);
            if (failed) {
                path.map(source.landing.getThrown(), payload);
            } else {
                path.map(call->getResult(0), normal.getArgument(0));
            }
            auto * continuation = failed ? check.getHandler() : check.getCont();
            auto values = failed ? check.getHandlerOperands() : check.getContOperands();
            if (!transfer(continuation, values, path)) { return {}; }
            mlir::OpBuilder nested = mlir::OpBuilder::atBlockEnd(destination);
            auto result = emit(continuation, path, nested, true, depth + 1);
            if (!result || !spend()) { return {}; }
            mlir::OperationState yield(invocation.getLoc(),
                                       ctjs::InvokeYieldOp::getOperationName());
            yield.addOperands(result);
            nested.create(yield);
        }
        if (!payload.use_empty()) {
            refuse("DOM URI catch observes its semantic error payload");
            return {};
        }
        return invocation.getResult(0);
    }

    mlir::Value emit(mlir::Block * block, mlir::IRMapping & mapping, mlir::OpBuilder & at,
                     bool liveFrame, unsigned depth) {
        if (!spend() || depth >= 64) {
            refuse("DOM URI source continuation nesting is too deep");
            return {};
        }
        reached.insert(block);
        for (mlir::Operation & operation : *block) {
            if (!spend() || !operands(operation, mapping)) { return {}; }
            visited.insert(&operation);
            if (auto check = chained(&operation)) {
                if (!liveFrame) {
                    refuse("DOM URI call is outside its source frame");
                    return {};
                }
                return invoke(&operation, check, mapping, at, depth);
            }
            if (auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
                if (enter != source.frame || liveFrame || copiedFrame) {
                    refuse("DOM URI source has an inconsistent frame entry");
                    return {};
                }
                at.clone(operation, mapping);
                copiedFrame = mapping.lookup(enter.getContext());
                liveFrame = true;
                continue;
            }
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation)) {
                if (!liveFrame || root.getContext() != source.frame.getContext()) {
                    refuse("DOM URI root is outside its original frame");
                    return {};
                }
                continue;
            }
            if (auto exit = llvm::dyn_cast<ctjs::FrameExitOp>(operation)) {
                if (!liveFrame || exit.getContext() != source.frame.getContext() ||
                    !llvm::isa_and_nonnull<ctjs::ReturnOp>(exit->getNextNode())) {
                    refuse("DOM URI frame exit does not precede its source return");
                    return {};
                }
                liveFrame = false;
                continue;
            }
            if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                if (liveFrame) {
                    refuse("DOM URI return retains its source frame");
                    return {};
                }
                return mapping.lookup(returned.getValue());
            }
            if (auto push = llvm::dyn_cast<ctjs::PushHandlerOp>(operation)) {
                if (push != source.push || !liveFrame ||
                    !transfer(push.getBody(), push.getBodyOperands(), mapping)) {
                    refuse("DOM URI handler installation lost its source state");
                    return {};
                }
                return emit(push.getBody(), mapping, at, liveFrame, depth + 1);
            }
            if (auto check = llvm::dyn_cast<ctjs::CheckOp>(operation)) {
                if (!llvm::is_contained(source.normal, block) ||
                    !transfer(check.getCont(), check.getContOperands(), mapping)) {
                    refuse("DOM URI status edge lacks a protected nonthrowing proof");
                    return {};
                }
                return emit(check.getCont(), mapping, at, liveFrame, depth + 1);
            }
            if (llvm::isa<ctjs::PopHandlerOp>(operation)) { continue; }
            if (auto landing = llvm::dyn_cast<ctjs::CatchLandOp>(operation)) {
                if (landing != source.landing || !mapping.contains(landing.getThrown())) {
                    refuse("DOM URI catch has no original invocation payload");
                    return {};
                }
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(operation)) {
                if (!transfer(branch.getDest(), branch.getDestOperands(), mapping)) { return {}; }
                return emit(branch.getDest(), mapping, at, liveFrame, depth + 1);
            }
            if (auto branch = llvm::dyn_cast<mlir::cf::CondBranchOp>(operation)) {
                // ponytail: duplicate bounded prefix continuations; each source
                // path still executes once. Larger trees stop at the work budget.
                if (!llvm::is_contained(source.prefix, block) || !spend(3)) {
                    refuse("DOM URI conditional continuation must precede its handler");
                    return {};
                }
                auto copied =
                    mlir::scf::IfOp::create(at, branch.getLoc(), function.getResultTypes(),
                                            mapping.lookup(branch.getCondition()));
                for (auto [region, successor, values] :
                     {std::tuple{&copied.getThenRegion(), branch.getTrueDest(),
                                 branch.getTrueDestOperands()},
                      std::tuple{&copied.getElseRegion(), branch.getFalseDest(),
                                 branch.getFalseDestOperands()}}) {
                    if (!copyCost(mapping)) { return {}; }
                    mlir::IRMapping path(mapping);
                    if (!transfer(successor, values, path)) { return {}; }
                    auto & destination = region->emplaceBlock();
                    mlir::OpBuilder nested = mlir::OpBuilder::atBlockEnd(&destination);
                    auto result = emit(successor, path, nested, liveFrame, depth + 1);
                    if (!result || !spend()) { return {}; }
                    mlir::scf::YieldOp::create(nested, branch.getLoc(), result);
                }
                return copied.getResult(0);
            }
            if (operation.getNumRegions() || operation.getNumSuccessors() ||
                operation.hasTrait<mlir::OpTrait::IsTerminator>()) {
                refuse("DOM URI requires the complete acyclic source CFG");
                return {};
            }
            at.clone(operation, mapping);
        }
        refuse("DOM URI continuation has no original return");
        return {};
    }

    bool complete() {
        for (mlir::Block & block : function.getBody()) {
            if (!spend()) { return false; }
            for (mlir::Operation & operation : block) {
                if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
                if (visited.contains(&operation)) { continue; }
                if (reached.contains(&block)) {
                    return refuse("DOM URI normalization left source operations unvisited");
                }
                // The importer retains dead pop/undefined-return epilogues.
                // Inspect every operation; no source lookup, call or write can
                // disappear merely because its block had no executable path.
                if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation);
                    constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue())) {
                    continue;
                }
                if (auto exit = llvm::dyn_cast<ctjs::FrameExitOp>(operation);
                    exit && exit.getContext() == source.frame.getContext()) {
                    continue;
                }
                if (llvm::isa<ctjs::PopHandlerOp>(operation)) { continue; }
                // A dead importer epilogue may rejoin a live return block.
                // Its source is unreachable; the destination does not change that.
                if (llvm::isa<mlir::cf::BranchOp>(operation)) { continue; }
                if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                    auto value = returned.getValue().getDefiningOp<ctjs::ConstantOp>();
                    if (value && llvm::isa<ctjs::UndefinedAttr>(value.getValue())) { continue; }
                }
                return refuse("DOM URI source contains an unvisited non-epilogue operation");
            }
        }
        return true;
    }

    bool run() {
        source = inspectSingleInvocationRegion(function, remaining, json ? 2 : 1);
        remaining -= source.steps;
        if (!source.proved()) { return refuse(source.refusal); }
        for (const auto & [call, check] : source.chain) {
            (void)check;
            if (!llvm::isa<ctjs::CallOp>(call)) {
                return refuse("DOM URI requires original ordinary invocations");
            }
        }
        if (!proveTailEffects()) { return false; }
        mlir::Region normalized;
        auto & block = normalized.emplaceBlock();
        mlir::IRMapping mapping;
        for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
            if (!spend()) { return false; }
            mapping.map(argument, block.addArgument(argument.getType(), argument.getLoc()));
        }
        mlir::OpBuilder at(function.getContext());
        at.setInsertionPointToEnd(&block);
        auto result = emit(&function.getBody().front(), mapping, at, false, 0);
        if (!result || !copiedFrame || !complete() || !spend(2)) { return false; }
        ctjs::FrameExitOp::create(at, function.getLoc(), copiedFrame);
        ctjs::ReturnOp::create(at, function.getLoc(), result);
        function.getBody().takeBody(normalized);
        function->removeAttr("ctjs.not_structured");
        return true;
    }
};

} // namespace

llvm::Expected<bool> normalizeDOMCaughtThrow(ctjs::FuncOp function, unsigned maxSteps) {
    const auto refuse = [](llvm::StringRef reason) -> llvm::Expected<bool> {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), reason);
    };
    unsigned remaining = maxSteps;
    bool throws = false;
    const auto scanned = function.walk([&](mlir::Operation * operation) {
        const uint64_t cost = uint64_t(1) + operation->getNumOperands();
        if (cost > remaining / 3) { return mlir::WalkResult::interrupt(); }
        remaining -= static_cast<unsigned>(cost * 3); // Scan and reserve the final rewrite.
        throws |= llvm::isa<ctjs::ThrowOp>(operation);
        return mlir::WalkResult::advance();
    });
    if (scanned.wasInterrupted()) { return refuse("DOM caught throw work budget exhausted"); }
    if (!throws) { return false; }
    auto recovered = recoverPrimitiveExceptionRegion(function, remaining);
    if (!recovered.recovered) { return false; }
    remaining -= recovered.steps;
    const auto rewritten = function.walk([&](mlir::Operation * operation) {
        const uint64_t cost = uint64_t(1) + operation->getNumOperands();
        if (cost > remaining / 3) { return mlir::WalkResult::interrupt(); }
        remaining -= static_cast<unsigned>(cost * 3);
        return mlir::WalkResult::advance();
    });
    if (rewritten.wasInterrupted()) { return refuse("DOM caught throw work budget exhausted"); }
    if (!function.getBody().hasOneBlock()) {
        return refuse("DOM caught throw requires a structured local prefix");
    }
    auto attempts = function.getBody().front().getOps<ctjs::TryOp>();
    if (!llvm::hasSingleElement(attempts)) {
        return refuse("DOM caught throw requires one local catch");
    }
    auto attempt = *attempts.begin();
    auto & body = attempt.getBody().front();
    auto & caught = attempt.getCatchBody().front();
    auto exit = llvm::dyn_cast<ctjs::TryExitOp>(body.getTerminator());
    auto yield = llvm::dyn_cast<ctjs::TryYieldOp>(caught.getTerminator());
    const auto alwaysThrows = [&](auto && self, mlir::Value flag, unsigned depth) -> bool {
        if (!remaining || depth == 64) { return false; }
        --remaining;
        if (auto constant = flag.getDefiningOp<mlir::arith::ConstantOp>()) {
            auto literal = llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue());
            return flag.getType().isInteger(1) && literal && literal.getValue().isOne();
        }
        auto result = llvm::dyn_cast<mlir::OpResult>(flag);
        auto branch =
            result ? llvm::dyn_cast<mlir::scf::IfOp>(result.getOwner()) : mlir::scf::IfOp{};
        if (!branch) { return false; }
        for (auto & region : branch->getRegions()) {
            if (!region.hasOneBlock()) { return false; }
            auto incoming = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
            if (!incoming || incoming.getNumOperands() != branch.getNumResults() ||
                !self(self, incoming.getOperand(result.getResultNumber()), depth + 1)) {
                return false;
            }
        }
        return true;
    };
    // ponytail: every protected path must explicitly throw; mixed normal/throw
    // completions still need a separate path-correlated catch-state proof.
    if (!exit || !yield || !alwaysThrows(alwaysThrows, exit.getIsThrow(), 0) ||
        exit.getCaughtValues().size() != caught.getNumArguments()) {
        return refuse("DOM caught throw requires an unconditional local completion");
    }
    const auto effects = [&](auto && self, mlir::Block & block, unsigned depth) -> bool {
        if (depth == 64) { return false; }
        for (mlir::Operation & operation : block.without_terminator()) {
            if (!remaining) { return false; }
            --remaining;
            if (auto poison = llvm::dyn_cast<mlir::ub::PoisonOp>(operation);
                poison && poison.getResult() == exit.getNormalResult() &&
                poison.getResult().hasOneUse()) {
                continue; // The all-throw completion cannot observe its normal result.
            }
            // Recovery removed protected checks. Prove their effects independently
            // of DOM result typing: even a typed URI/JSON call can throw implicitly.
            if (llvm::isa<ctjs::RootOp, ctjs::ConstantOp, ctjs::TruthyOp, mlir::arith::ConstantOp>(
                    operation)) {
                continue;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
                compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                for (auto & region : branch->getRegions()) {
                    if (!region.hasOneBlock() || region.front().getNumArguments() ||
                        !llvm::isa<mlir::scf::YieldOp>(region.front().getTerminator()) ||
                        !self(self, region.front(), depth + 1)) {
                        return false;
                    }
                }
                continue;
            }
            return false;
        }
        return true;
    };
    if (!effects(effects, body, 0)) {
        return refuse("DOM caught throw protected effect lacks a nonthrowing proof");
    }
    for (auto [argument, value] : llvm::zip(caught.getArguments(), exit.getCaughtValues())) {
        argument.replaceAllUsesWith(value);
    }
    for (mlir::Operation & operation : llvm::make_early_inc_range(body.without_terminator())) {
        if (llvm::isa<mlir::ub::PoisonOp>(operation)) { continue; }
        operation.moveBefore(attempt);
    }
    for (mlir::Operation & operation : llvm::make_early_inc_range(caught.without_terminator())) {
        operation.moveBefore(attempt);
    }
    attempt.getResult().replaceAllUsesWith(yield.getValue());
    attempt.erase();
    if (auto error = normalizeStructuredExits(function, remaining)) { return std::move(error); }
    // CFG structuring can leave unused dispatch constants in the outer prefix.
    function.walk([](mlir::arith::ConstantOp constant) {
        if (constant->use_empty()) { constant.erase(); }
    });
    return true;
}

llvm::Error normalizeDOMURI(mlir::ModuleOp candidate, const HostContract & contract,
                            unsigned maxSteps, llvm::StringRef function) {
    unsigned remaining = maxSteps;
    const auto scanned = candidate.walk([&](mlir::Operation * operation) {
        const uint64_t cost = uint64_t(1) + operation->getNumOperands();
        if (cost > remaining) { return mlir::WalkResult::interrupt(); }
        remaining -= static_cast<unsigned>(cost);
        return mlir::WalkResult::advance();
    });
    const unsigned scanCost = maxSteps - remaining;
    if (scanned.wasInterrupted() || scanCost > remaining) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "DOM URI normalization work budget exhausted");
    }
    remaining -= scanCost; // Reserve the complete fingerprint scan before printing the module.
    if ((contract.provider != HostContract::Provider::ctbrowserDOM &&
         contract.provider != HostContract::Provider::ctbrowserDOMSession) ||
        !llvm::is_contained(contract.initialIntrinsics, "decodeURIComponent") ||
        contract.moduleSha256 != hostContractFingerprint(candidate)) {
        return llvm::createStringError(
            llvm::inconvertibleErrorCode(),
            "DOM URI requires its fingerprinted initial provider binding");
    }
    auto target =
        candidate.lookupSymbol<ctjs::FuncOp>(function.empty() ? contract.entry : function);
    if (!target || target.getBody().empty()) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "DOM URI entry is missing");
    }
    candidate.getContext()->getOrLoadDialect<mlir::scf::SCFDialect>();
    DOMURI attempt(target, remaining, llvm::is_contained(contract.initialIntrinsics, "JSON"));
    if (!attempt.run()) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), attempt.reason);
    }
    return llvm::Error::success();
}

} // namespace ctcompile::ctnative::lowering_detail
