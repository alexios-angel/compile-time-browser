#include "../../../CTJS/Lowering/Globals/RegisterFlow.h"
#include "../../HostContract/Analysis.h"
#include "Recovery.h"
#include "ctbrowser/dom/element.hpp"
#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/DenseSet.h"

#include <cstdint>
#include <tuple>
#include <utility>

namespace ctcompile::ctnative::lowering_detail {

namespace {

struct DOMURI : workBudget {
    ctjs::FuncOp function;
    ExceptionInvocationSource source;
    mlir::DominanceInfo dominance;
    llvm::DenseSet<mlir::Operation *> visited;
    llvm::DenseSet<mlir::Block *> reached;
    mlir::Value copiedFrame;
    llvm::SmallVector<ctjs::LoadGlobalOp> jsonLoads;
    // JSON.parse is admitted only when the contract binds the initial JSON.
    bool json = false;

    DOMURI(ctjs::FuncOp function, unsigned maxSteps, bool json)
        : workBudget{maxSteps, "DOM URI normalization work budget exhausted"}, function(function),
          dominance(function), json(json) {}

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

    bool copyCost(const mlir::IRMapping & mapping) {
        return spend(uint64_t(mapping.getValueMap().size()) + mapping.getOperationMap().size());
    }
    bool transfer(mlir::Block * block, mlir::ValueRange operands, mlir::IRMapping & mapping) {
        if (!spend(uint64_t(1) + operands.size()) || operands.size() != block->getNumArguments()) {
            return reject("DOM URI continuation lost its complete register vector");
        }
        // Read the complete old vector before replacing any argument mapping.
        llvm::SmallVector<mlir::Value> actuals;
        for (auto [operand, argument] : llvm::zip(operands, block->getArguments())) {
            if (operand.getType() != argument.getType() || !mapping.contains(operand)) {
                return reject("DOM URI continuation has an unmapped source register");
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
                return reject("DOM URI operand has no preceding source definition");
            }
            if (operand == source.frame.getContext() &&
                !llvm::isa<ctjs::RootOp, ctjs::FrameExitOp>(operation)) {
                return reject("DOM URI source observes its shadow frame");
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
                return reject("DOM URI protected or caught operation lacks an independent "
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
            reject("DOM URI catch observes its semantic error payload");
            return {};
        }
        return invocation.getResult(0);
    }

    mlir::Value emit(mlir::Block * block, mlir::IRMapping & mapping, mlir::OpBuilder & at,
                     bool liveFrame, unsigned depth) {
        if (!spend() || depth >= 64) {
            reject("DOM URI source continuation nesting is too deep");
            return {};
        }
        reached.insert(block);
        for (mlir::Operation & operation : *block) {
            if (!spend() || !operands(operation, mapping)) { return {}; }
            visited.insert(&operation);
            if (auto check = chained(&operation)) {
                if (!liveFrame) {
                    reject("DOM URI call is outside its source frame");
                    return {};
                }
                return invoke(&operation, check, mapping, at, depth);
            }
            if (auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
                if (enter != source.frame || liveFrame || copiedFrame) {
                    reject("DOM URI source has an inconsistent frame entry");
                    return {};
                }
                at.clone(operation, mapping);
                copiedFrame = mapping.lookup(enter.getContext());
                liveFrame = true;
                continue;
            }
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation)) {
                if (!liveFrame || root.getContext() != source.frame.getContext()) {
                    reject("DOM URI root is outside its original frame");
                    return {};
                }
                continue;
            }
            if (auto exit = llvm::dyn_cast<ctjs::FrameExitOp>(operation)) {
                if (!liveFrame || exit.getContext() != source.frame.getContext() ||
                    !llvm::isa_and_nonnull<ctjs::ReturnOp>(exit->getNextNode())) {
                    reject("DOM URI frame exit does not precede its source return");
                    return {};
                }
                liveFrame = false;
                continue;
            }
            if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                if (liveFrame) {
                    reject("DOM URI return retains its source frame");
                    return {};
                }
                return mapping.lookup(returned.getValue());
            }
            if (auto push = llvm::dyn_cast<ctjs::PushHandlerOp>(operation)) {
                if (push != source.push || !liveFrame ||
                    !transfer(push.getBody(), push.getBodyOperands(), mapping)) {
                    reject("DOM URI handler installation lost its source state");
                    return {};
                }
                return emit(push.getBody(), mapping, at, liveFrame, depth + 1);
            }
            if (auto check = llvm::dyn_cast<ctjs::CheckOp>(operation)) {
                if (!llvm::is_contained(source.normal, block) ||
                    !transfer(check.getCont(), check.getContOperands(), mapping)) {
                    reject("DOM URI status edge lacks a protected nonthrowing proof");
                    return {};
                }
                return emit(check.getCont(), mapping, at, liveFrame, depth + 1);
            }
            if (llvm::isa<ctjs::PopHandlerOp>(operation)) { continue; }
            if (auto landing = llvm::dyn_cast<ctjs::CatchLandOp>(operation)) {
                if (landing != source.landing || !mapping.contains(landing.getThrown())) {
                    reject("DOM URI catch has no original invocation payload");
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
                    reject("DOM URI conditional continuation must precede its handler");
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
                reject("DOM URI requires the complete acyclic source CFG");
                return {};
            }
            at.clone(operation, mapping);
        }
        reject("DOM URI continuation has no original return");
        return {};
    }

    bool complete() {
        for (mlir::Block & block : function.getBody()) {
            if (!spend()) { return false; }
            for (mlir::Operation & operation : block) {
                if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
                if (visited.contains(&operation)) { continue; }
                if (reached.contains(&block)) {
                    return reject("DOM URI normalization left source operations unvisited");
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
                if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation);
                    returned && ctjs::isUndefinedConstant(returned.getValue())) {
                    continue;
                }
                return reject("DOM URI source contains an unvisited non-epilogue operation");
            }
        }
        return true;
    }

    bool run() {
        source = inspectSingleInvocationRegion(function, remaining, json ? 2 : 1);
        remaining -= source.steps;
        if (!source.proved()) { return reject(source.refusal); }
        for (const auto & [call, check] : source.chain) {
            (void)check;
            if (!llvm::isa<ctjs::CallOp>(call)) {
                return reject("DOM URI requires original ordinary invocations");
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

namespace {

// Whether every arm of the scf.if defining `value` yields a slot `proved`
// accepts; nullopt when `value` is not an scf.if result.
template <class Proof> std::optional<bool> allArms(mlir::Value value, Proof && proved) {
    auto result = llvm::dyn_cast<mlir::OpResult>(value);
    auto branch = result ? llvm::dyn_cast<mlir::scf::IfOp>(result.getOwner()) : mlir::scf::IfOp{};
    if (!branch) { return std::nullopt; }
    for (auto & region : branch->getRegions()) {
        auto yield = exactYield(region, branch.getNumResults());
        if (!yield || !proved(yield.getOperand(result.getResultNumber()))) { return false; }
    }
    return true;
}

// Exact Boolean truthiness and integer casts/comparisons of completion
// dispatch, folded with operands read through `mapping`. Null otherwise.
mlir::Value foldDispatch(mlir::OpBuilder & at, mlir::Operation & operation,
                         const mlir::IRMapping & mapping) {
    if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
        auto flag =
            ctjs::constantAttr<ctjs::BooleanAttr>(mapping.lookupOrDefault(truth.getValue()));
        if (!flag) { return {}; }
        return mlir::arith::ConstantIntOp::create(at, truth.getLoc(), flag.getValue(), 1);
    }
    if (auto cast = llvm::dyn_cast<mlir::arith::IndexCastUIOp>(operation)) {
        return at.createOrFold<mlir::arith::IndexCastUIOp>(cast.getLoc(), cast.getType(),
                                                           mapping.lookupOrDefault(cast.getIn()));
    }
    if (auto compare = llvm::dyn_cast<mlir::arith::CmpIOp>(operation)) {
        return at.createOrFold<mlir::arith::CmpIOp>(compare.getLoc(), compare.getPredicate(),
                                                    mapping.lookupOrDefault(compare.getLhs()),
                                                    mapping.lookupOrDefault(compare.getRhs()));
    }
    return {};
}

struct DOMAttributeProof {
    ctjs::FuncOp function;
    llvm::ArrayRef<unsigned> elementParameters;
    unsigned & remaining;
    // The constant member name `call` invokes, or empty.
    static llvm::StringRef calleeKey(ctjs::CallOp call) {
        auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        return method ? ctjs::constantKey(method.getKey()) : llvm::StringRef{};
    }
    bool attributeMethod(ctjs::GetPropertyOp method) {
        static constexpr llvm::StringLiteral names[] = {"hasAttribute", "getAttribute",
                                                        "setAttribute"};
        if (!method || !llvm::is_contained(names, ctjs::constantKey(method.getKey()))) {
            return false;
        }
        mlir::Value value = method.getObject();
        // Recovery may forward an unchanged register through a branch result.
        // Identical incoming SSA proves identity without discarding either arm's
        // effects; differing origins still need a separate receiver proof.
        for (unsigned depth = 0; depth < 64; ++depth) {
            auto result = llvm::dyn_cast<mlir::OpResult>(value);
            auto branch =
                result ? llvm::dyn_cast<mlir::scf::IfOp>(result.getOwner()) : mlir::scf::IfOp{};
            if (!branch) { break; }
            if (!branch.getThenRegion().hasOneBlock() || !branch.getElseRegion().hasOneBlock() ||
                !charge(remaining)) {
                return false;
            }
            auto yes = exactYield(branch.getThenRegion(), branch.getNumResults());
            auto no = exactYield(branch.getElseRegion(), branch.getNumResults());
            if (!yes || !no ||
                yes.getOperand(result.getResultNumber()) !=
                    no.getOperand(result.getResultNumber())) {
                return false;
            }
            value = yes.getOperand(result.getResultNumber());
        }
        auto receiver = llvm::dyn_cast<mlir::BlockArgument>(value);
        if (!receiver || receiver.getOwner() != &function.getBody().front() ||
            receiver.getArgNumber() < ctjs::implicit_arguments) {
            return false;
        }
        for (unsigned index : elementParameters) {
            if (!charge(remaining)) { return false; }
            if (index == receiver.getArgNumber() - ctjs::implicit_arguments) { return true; }
        }
        return false;
    }
    // Boolean/String origins may include null: these sets prove nonthrowing
    // primitive conversion, not an exact runtime tag.
    llvm::DenseSet<mlir::Value> booleanReads;
    llvm::DenseSet<mlir::Value> stringReads;
    llvm::DenseSet<mlir::Value> numberReads;
    bool booleanValue(mlir::Value value, unsigned depth = 0) {
        if (depth == 64 || !charge(remaining)) { return false; }
        if (auto compare = value.getDefiningOp<ctjs::CompareOp>()) {
            return compare.getKind() == ctjs::CompareKind::StrictEq;
        }
        if (auto unary = value.getDefiningOp<ctjs::UnaryOp>()) {
            return unary.getKind() == ctjs::UnaryKind::Not;
        }
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        auto call = value.getDefiningOp<ctjs::CallOp>();
        // A saved predicate retains its Boolean value across later DOM writes.
        // Prove the exact read, leaving producer effects and initial method
        // identity to the complete final DOM proof.
        if (booleanReads.contains(value) ||
            (constant && llvm::isa<ctjs::BooleanAttr, ctjs::NullAttr>(constant.getValue())) ||
            (call && attributeRead(call) && attributeCall(call))) {
            return true;
        }
        return allArms(value, [&](mlir::Value arm) { return booleanValue(arm, depth + 1); })
            .value_or(false);
    }
    bool stringValue(mlir::Value value, unsigned depth = 0) {
        if (depth == 64 || !charge(remaining)) { return false; }
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        auto call = value.getDefiningOp<ctjs::CallOp>();
        // Saved getAttribute and Number.toString results may precede the protected
        // body. Reuse their charged origins; producer effects and initial method
        // identity still require the complete final DOM proof.
        if (stringReads.contains(value) ||
            (constant && llvm::isa<ctjs::StringAttr, ctjs::NullAttr>(constant.getValue())) ||
            (call && calleeKey(call) == "getAttribute" && attributeCall(call)) ||
            numberStringCall(call)) {
            return true;
        }
        // A saved prefix selection carries the selected owning String/null value.
        // Prove the same result slot in every arm; this grants no effect authority
        // to the condition or producers, which remain in the final DOM source.
        return allArms(value, [&](mlir::Value arm) { return stringValue(arm, depth + 1); })
            .value_or(false);
    }
    static bool numberCallee(ctjs::CallOp call) {
        auto callee =
            call ? call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
        return callee && callee.getName() == "Number" &&
               ctjs::isUndefinedConstant(call.getReceiver()) && call.getArgs().size() == 1;
    }
    bool numberValue(mlir::Value value, unsigned depth = 0) {
        if (depth == 64 || !charge(remaining)) { return false; }
        if (numberReads.contains(value)) { return true; }
        if (auto constant = value.getDefiningOp<ctjs::ConstantOp>()) {
            return llvm::isa<ctjs::NumberAttr>(constant.getValue());
        }
        if (auto call = value.getDefiningOp<ctjs::CallOp>()) {
            // A successful initial Number call produces a Number. Final DOM
            // proof still validates its binding and input; this grants no
            // effect authority to the call or its argument producers.
            return numberCallee(call);
        }
        // Only immutable Number origins prove conversion cannot invoke user code.
        // This does not discharge the producers' effects or protected status edges.
        if (auto unary = value.getDefiningOp<ctjs::UnaryOp>()) {
            return (unary.getKind() == ctjs::UnaryKind::Neg ||
                    unary.getKind() == ctjs::UnaryKind::Plus ||
                    unary.getKind() == ctjs::UnaryKind::BitNot) &&
                   numberValue(unary.getOperand(), depth + 1);
        }
        const auto numericBinary = [&](auto binary) {
            return (ctjs::isArithmetic(binary.getKind()) || ctjs::isBitwise(binary.getKind())) &&
                   numberValue(binary.getLhs(), depth + 1) &&
                   numberValue(binary.getRhs(), depth + 1);
        };
        if (auto binary = value.getDefiningOp<ctjs::BinaryOp>()) { return numericBinary(binary); }
        if (auto binary = value.getDefiningOp<ctjs::BinaryStaticOp>()) {
            return numericBinary(binary);
        }
        return allArms(value, [&](mlir::Value arm) { return numberValue(arm, depth + 1); })
            .value_or(false);
    }
    // Attribute conversion of a Number/null selection cannot invoke user code.
    // Null stays out of numberValue: that proof also authorizes Number methods.
    bool nullableNumberValue(mlir::Value value, unsigned depth = 0) {
        if (depth == 64 || !charge(remaining)) { return false; }
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        if (constant && llvm::isa<ctjs::NullAttr>(constant.getValue())) { return true; }
        if (auto arms = allArms(
                value, [&](mlir::Value arm) { return nullableNumberValue(arm, depth + 1); })) {
            return *arms;
        }
        return numberValue(value, depth + 1);
    }
    static bool attributeRead(ctjs::CallOp call) { return calleeKey(call) == "hasAttribute"; }
    bool numberStringMethod(ctjs::GetPropertyOp method) {
        return method && ctjs::constantKey(method.getKey()) == "toString" &&
               numberValue(method.getObject());
    }
    bool numberStringCall(ctjs::CallOp call) {
        if (!call || !call.getArgs().empty()) { return false; }
        auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        return numberStringMethod(method) && call.getReceiver() == method.getObject();
    }
    bool attributeCall(ctjs::CallOp call) {
        if (!call) { return false; }
        auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (!attributeMethod(method) || call.getReceiver() != method.getObject()) { return false; }
        const bool writes = ctjs::constantKey(method.getKey()) == "setAttribute";
        if (call.getArgs().size() != (writes ? 2u : 1u)) { return false; }
        auto key = call.getArgs().front().getDefiningOp<ctjs::ConstantOp>();
        auto name = key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue()) : ctjs::StringAttr{};
        if (!name) { return false; }
        if (!writes) { return true; }
        if (!stringValue(call.getArgs()[1]) && !booleanValue(call.getArgs()[1]) &&
            !nullableNumberValue(call.getArgs()[1])) {
            return false;
        }
        if (name.getValue().size() > remaining) { return false; }
        // setAttribute validates before mutation. Charge and call the same public
        // validator as the VM binding; proved primitive conversion cannot reenter.
        remaining -= static_cast<unsigned>(name.getValue().size());
        return ctbrowser::is_valid_attribute_name(
            std::string_view{name.getValue().data(), name.getValue().size()});
    }
    bool nonthrowingCall(ctjs::CallOp call) {
        // Unlike result-tag evidence, suppressing an exception requires an
        // input that cannot invoke ToPrimitive or throw during Number conversion.
        // The initial global binding is checked by the complete final DOM proof.
        return attributeCall(call) || numberStringCall(call) ||
               (numberCallee(call) &&
                (stringValue(call.getArgs().front()) || booleanValue(call.getArgs().front()) ||
                 numberValue(call.getArgs().front())));
    }
    void callResult(ctjs::CallOp call, mlir::Value result) {
        // Called only after the independent nonthrowing proof succeeds.
        if (attributeRead(call)) { booleanReads.insert(result); }
        if (const auto key = calleeKey(call); key == "getAttribute" || key == "toString") {
            stringReads.insert(result);
        }
        if (numberCallee(call)) { numberReads.insert(result); }
    }
    void join(mlir::Value result, mlir::Value left, mlir::Value right) {
        if (booleanValue(left) && booleanValue(right)) { booleanReads.insert(result); }
        if (stringValue(left) && stringValue(right)) { stringReads.insert(result); }
        if (numberValue(left) && numberValue(right)) { numberReads.insert(result); }
    }
};

} // namespace

static llvm::Expected<bool> normalizeDOMCaughtCompletion(ctjs::FuncOp function, unsigned maxSteps,
                                                         llvm::ArrayRef<unsigned> elementParameters,
                                                         ExceptionRecoveryMode mode,
                                                         bool recoveredBody = false) {
    const auto refuse = [](llvm::StringRef reason) -> llvm::Expected<bool> {
        return llvm::createStringError(reason);
    };
    unsigned remaining = maxSteps;
    bool throws = false;
    const auto operandCost = [](mlir::Operation * operation) {
        return uint64_t(1) + operation->getNumOperands();
    };
    // Scan and reserve the final rewrite.
    if (!chargeWalk(function, remaining, 3, [&](mlir::Operation * operation) {
            throws |= llvm::isa<ctjs::ThrowOp>(operation);
            return operandCost(operation);
        })) {
        return refuse("DOM caught throw work budget exhausted");
    }
    if (!recoveredBody) {
        if (!throws && mode == ExceptionRecoveryMode::ExplicitThrows) { return false; }
        auto recovered = recoverPrimitiveExceptionRegion(function, remaining, mode);
        if (!recovered.recovered) { return false; }
        remaining -= recovered.steps;
    }
    if (!chargeWalk(function, remaining, 3, operandCost)) {
        return refuse("DOM caught throw work budget exhausted");
    }
    if (auto error = structureRecoveredPrefix(function, remaining)) { return std::move(error); }
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
        if (depth == 64 || !charge(remaining)) { return false; }
        if (flag.getDefiningOp<mlir::arith::ConstantOp>()) {
            return flag.getType().isInteger(1) && mlir::matchPattern(flag, mlir::m_One());
        }
        return allArms(flag, [&](mlir::Value arm) { return self(self, arm, depth + 1); })
            .value_or(false);
    };
    if (!exit || !yield || exit.getCaughtValues().size() != caught.getNumArguments()) {
        return refuse("DOM caught throw lost its local completion correspondence");
    }
    const bool unconditional = alwaysThrows(alwaysThrows, exit.getIsThrow(), 0);
    DOMAttributeProof attributes{function, elementParameters, remaining, {}, {}, {}};
    const auto effects = [&](auto && self, mlir::Block & block, unsigned depth) -> bool {
        if (depth == 64) { return false; }
        for (mlir::Operation & operation : llvm::make_early_inc_range(block.without_terminator())) {
            if (!charge(remaining)) { return false; }
            if (auto poison = llvm::dyn_cast<mlir::ub::PoisonOp>(operation);
                poison && (!unconditional || (poison.getResult() == exit.getNormalResult() &&
                                              poison.getResult().hasOneUse()))) {
                continue; // Mixed completions select their active values below.
            }
            // Recovery removed protected checks. Prove their effects independently
            // of DOM result typing: even a typed URI/JSON call can throw implicitly.
            // Project exact completion dispatch before a later call needs its
            // saved callee or receiver. Source truthiness still runs normally.
            mlir::OpBuilder at(&operation);
            if (llvm::isa<ctjs::TruthyOp, mlir::arith::IndexCastUIOp, mlir::arith::CmpIOp>(
                    operation)) {
                if (auto folded = foldDispatch(at, operation, {})) {
                    operation.getResult(0).replaceAllUsesWith(folded);
                    operation.erase();
                }
                continue;
            }
            if (llvm::isa<ctjs::RootOp, ctjs::ConstantOp, mlir::arith::ConstantOp>(operation)) {
                continue;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
                compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
                continue;
            }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
                unary && unary.getKind() == ctjs::UnaryKind::Not) {
                continue;
            }
            // Only exact Number arithmetic avoids coercion and implicit throws.
            // Reuse the charged origin proof, but keep checking every producer
            // independently; a Number result alone never discharges its effects.
            if (llvm::isa<ctjs::UnaryOp, ctjs::BinaryOp, ctjs::BinaryStaticOp>(operation) &&
                attributes.numberValue(operation.getResult(0))) {
                continue;
            }
            if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
                load && load.getName() == "Number") {
                continue; // Kept in order; the final proof requires the initial intrinsic.
            }
            // The original entry contract fixes this Element and its initial
            // method identity. Proved Strings/null/Booleans/Numbers cannot invoke
            // user coercion; valid-name writes cannot throw or reenter. Keep
            // every mutation in order and reprove the complete DOM body below.
            // ponytail: entry parameters and identical branch forwarding only;
            // other aliases need their own source identity proof.
            if (auto method = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                attributes.attributeMethod(method) || attributes.numberStringMethod(method)) {
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                attributes.nonthrowingCall(call)) {
                attributes.callResult(call, call.getResult());
                continue;
            }
            if (auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(operation)) {
                auto & invoked = invocation.getBody().front();
                auto call = llvm::dyn_cast<ctjs::CallOp>(invoked.front());
                auto dispatch = llvm::dyn_cast<ctjs::InvokeExitOp>(invoked.back());
                auto & normal = invocation.getNormalBody().front();
                if (invoked.getNumArguments() || !attributes.nonthrowingCall(call) || !dispatch ||
                    call->getNextNode() != dispatch ||
                    dispatch.getNormalResult() != call.getResult() ||
                    normal.getNumArguments() != 1 ||
                    !llvm::isa<ctjs::InvokeYieldOp>(normal.getTerminator())) {
                    return false;
                }
                // Recovery's normal tuple contains only its tag, inactive padding
                // and original saved values. Never infer effects from result types.
                for (auto & projection : normal.without_terminator()) {
                    if (!charge(remaining)) { return false; }
                    if (!llvm::isa<ctjs::ConstantOp, mlir::arith::ConstantOp, mlir::ub::PoisonOp>(
                            projection)) {
                        return false;
                    }
                }
                // Select normal only after the independent effect proof. Keep
                // the complete tuple, including inactive padding, and record
                // primitive evidence only for the surviving call result.
                auto projected = host_detail::projectInvocationContinuation(
                    invocation, false, call.getResult(), at, remaining);
                if (mlir::failed(projected)) { return false; }
                call->moveBefore(invocation);
                attributes.callResult(call, call.getResult());
                invocation.replaceAllUsesWith(*projected);
                invocation.erase();
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                for (auto & region : branch->getRegions()) {
                    if (!exactYield(region, branch.getNumResults()) ||
                        region.front().getNumArguments() ||
                        !self(self, region.front(), depth + 1)) {
                        return false;
                    }
                }
                auto yes = llvm::cast<mlir::scf::YieldOp>(branch.getThenRegion().front().back());
                auto no = llvm::cast<mlir::scf::YieldOp>(branch.getElseRegion().front().back());
                llvm::APInt literal;
                if (branch.getCondition().getType().isInteger(1) &&
                    mlir::matchPattern(branch.getCondition(), mlir::m_ConstantInt(&literal))) {
                    // Both arms passed effects. Move the selected definitions
                    // before forwarding their values, preserving dominance and
                    // order without treating inactive poison as an origin.
                    auto selected = literal.isZero() ? no : yes;
                    for (auto & next :
                         llvm::make_early_inc_range(selected->getBlock()->without_terminator())) {
                        if (!charge(remaining)) { return false; }
                        next.moveBefore(branch);
                    }
                    for (auto [result, value] :
                         llvm::zip(branch.getResults(), selected.getOperands())) {
                        for (auto & use : llvm::make_early_inc_range(result.getUses())) {
                            if (!charge(remaining)) { return false; }
                            use.set(value);
                        }
                    }
                    // Discarded definitions must not survive as evidence when
                    // subsequent projections allocate new SSA values.
                    if (!chargeWalk(branch, remaining, 1, [&](mlir::Operation * nested) {
                            for (auto result : nested->getResults()) {
                                attributes.booleanReads.erase(result);
                                attributes.stringReads.erase(result);
                                attributes.numberReads.erase(result);
                            }
                            return uint64_t(1) + nested->getNumResults();
                        })) {
                        return false;
                    }
                    branch.erase();
                    continue;
                }
                for (auto [result, left, right] :
                     llvm::zip(branch.getResults(), yes.getOperands(), no.getOperands())) {
                    if (!charge(remaining)) { return false; }
                    attributes.join(result, left, right);
                    // Both arms have complete effect proofs. Forward only the
                    // identical saved SSA value, retaining the branch and its
                    // effects. Existing result trimming runs before final DOM
                    // proof; equal callable kinds or names supply no identity.
                    if (left != right) { continue; }
                    for (auto & use : llvm::make_early_inc_range(result.getUses())) {
                        if (!charge(remaining)) { return false; }
                        use.set(left);
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
    if (!unconditional) {
        const auto spend = [&](uint64_t cost) { return charge(remaining, cost); };
        const auto clone = [&](mlir::Operation & operation, mlir::IRMapping & mapping,
                               mlir::OpBuilder & at) {
            if (!chargeWalk(&operation, remaining, 1, [](mlir::Operation * nested) {
                    return uint64_t(1) + nested->getNumOperands() + nested->getNumResults();
                })) {
                return false;
            }
            at.clone(operation, mapping);
            return true;
        };
        // Follow each original yield with its complete result mapping. Recovery
        // may dispatch a nested completion after its first join; consuming the
        // tuple along that same path keeps inactive poison out of the catch.
        const auto emit = [&](auto && self, mlir::Operation * next, mlir::IRMapping & mapping,
                              mlir::OpBuilder & at, llvm::SmallVector<mlir::scf::IfOp> pending,
                              unsigned depth) -> mlir::Value {
            if (depth == 64 || !spend(uint64_t(1) + pending.size())) { return {}; }
            for (; next; next = next->getNextNode()) {
                if (!spend(uint64_t(1) + next->getNumOperands())) { return {}; }
                if (auto returned = llvm::dyn_cast<ctjs::TryExitOp>(next)) {
                    if (!pending.empty() || returned != exit) { return {}; }
                    const auto flag = mapping.lookupOrDefault(returned.getIsThrow());
                    llvm::APInt literal;
                    if (!flag.getType().isInteger(1) ||
                        !mlir::matchPattern(flag, mlir::m_ConstantInt(&literal))) {
                        return {};
                    }
                    if (literal.isZero()) {
                        return mapping.lookupOrDefault(returned.getNormalResult());
                    }
                    for (auto [argument, value] :
                         llvm::zip(caught.getArguments(), returned.getCaughtValues())) {
                        mapping.map(argument, mapping.lookupOrDefault(value));
                    }
                    for (mlir::Operation & operation : caught.without_terminator()) {
                        if (!clone(operation, mapping, at)) { return {}; }
                    }
                    return mapping.lookupOrDefault(yield.getValue());
                }
                if (auto incoming = llvm::dyn_cast<mlir::scf::YieldOp>(next)) {
                    if (pending.empty()) { return {}; }
                    auto branch = pending.pop_back_val();
                    if (incoming->getParentOp() != branch ||
                        incoming.getNumOperands() != branch.getNumResults()) {
                        return {};
                    }
                    for (auto [result, value] :
                         llvm::zip(branch.getResults(), incoming.getOperands())) {
                        mapping.map(result, mapping.lookupOrDefault(value));
                    }
                    return self(self, branch->getNextNode(), mapping, at, std::move(pending),
                                depth + 1);
                }
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(next)) {
                    auto condition = mapping.lookupOrDefault(branch.getCondition());
                    pending.push_back(branch);
                    llvm::APInt literal;
                    if (condition.getType().isInteger(1) &&
                        mlir::matchPattern(condition, mlir::m_ConstantInt(&literal))) {
                        auto & selected =
                            literal.isZero() ? branch.getElseRegion() : branch.getThenRegion();
                        return self(self, &selected.front().front(), mapping, at,
                                    std::move(pending), depth + 1);
                    }
                    auto selected =
                        mlir::scf::IfOp::create(at, branch.getLoc(), attempt.getResult().getType(),
                                                condition, false, false);
                    for (auto [source, destination] :
                         llvm::zip(branch->getRegions(), selected->getRegions())) {
                        if (!spend(uint64_t(mapping.getValueMap().size()) +
                                   mapping.getOperationMap().size() + mapping.getBlockMap().size() +
                                   pending.size() + 2)) {
                            return {};
                        }
                        mlir::IRMapping path(mapping);
                        auto & target = destination.emplaceBlock();
                        auto inside = mlir::OpBuilder::atBlockEnd(&target);
                        auto value =
                            self(self, &source.front().front(), path, inside, pending, depth + 1);
                        if (!value) { return {}; }
                        mlir::scf::YieldOp::create(inside, branch.getLoc(), value);
                    }
                    return selected.getResult(0);
                }
                // Fold exact Boolean/integer completion dispatch. Other source
                // truthiness and comparisons still execute in their original order.
                if (auto folded = foldDispatch(at, *next, mapping)) {
                    mapping.map(next->getResult(0), folded);
                } else if (!clone(*next, mapping, at)) {
                    return {};
                }
            }
            return {};
        };
        mlir::IRMapping mapping;
        mlir::OpBuilder at(attempt);
        auto result = emit(emit, &body.front(), mapping, at, {}, 0);
        if (!result) {
            return refuse("DOM caught throw requires exact branch completion projection");
        }
        const auto isolated = attempt.walk([&](mlir::Operation * operation) {
            if (operation == attempt) { return mlir::WalkResult::advance(); }
            for (auto value : operation->getResults()) {
                for (auto * user : value.getUsers()) {
                    if (!spend(1) || !attempt->isProperAncestor(user)) {
                        return mlir::WalkResult::interrupt();
                    }
                }
            }
            return spend(1) ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
        });
        if (isolated.wasInterrupted()) {
            return refuse("DOM caught throw projection retained an inactive completion value");
        }
        attempt.getResult().replaceAllUsesWith(result);
        attempt.erase();
        if (auto error = normalizeStructuredExits(function, remaining)) { return std::move(error); }
        function.walk([](mlir::Operation * operation) {
            if (llvm::isa<mlir::arith::ConstantOp, mlir::ub::PoisonOp>(operation) &&
                operation->use_empty()) {
                operation->erase();
            }
        });
        return true;
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

llvm::Error normalizeDOMRecoveredCatch(ctjs::FuncOp function, unsigned maxSteps,
                                       llvm::ArrayRef<unsigned> elementParameters) {
    auto result = normalizeDOMCaughtCompletion(function, maxSteps, elementParameters,
                                               ExceptionRecoveryMode::CheckedInvocations, true);
    if (!result) { return result.takeError(); }
    if (!*result) {
        return llvm::createStringError("DOM recovered helper catch lost its completion");
    }
    return llvm::Error::success();
}

llvm::Error normalizeDOMAttributeInvocations(ctjs::FuncOp function, unsigned maxSteps,
                                             llvm::ArrayRef<unsigned> elementParameters) {
    unsigned remaining = maxSteps;
    const auto refuse = [] {
        return llvm::createStringError(
            "DOM attribute completion is invalid or exhausted its budget");
    };
    // Scan, verify and rewrite.
    const bool census = chargeWalk(function, remaining, 3, [](mlir::Operation * operation) {
        uint64_t cost = uint64_t(1) + operation->getNumOperands() + operation->getNumResults();
        for (auto & region : operation->getRegions()) {
            for (auto & block : region) { cost += 1 + block.getNumArguments(); }
        }
        return cost;
    });
    if (!census || !function.getBody().hasOneBlock() || mlir::failed(mlir::verify(function))) {
        return refuse();
    }
    DOMAttributeProof attributes{function, elementParameters, remaining, {}, {}, {}};
    const auto normalize = [&](auto && self, mlir::Block & block, unsigned depth) -> bool {
        if (depth == 64) { return false; }
        for (auto & operation : llvm::make_early_inc_range(block)) {
            if (!charge(remaining)) { return false; }
            if (auto attempt = llvm::dyn_cast<ctjs::TryOp>(operation)) {
                for (auto & region : attempt->getRegions()) {
                    if (!region.hasOneBlock() || !self(self, region.front(), depth + 1)) {
                        return false;
                    }
                }
                continue;
            }
            if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                for (auto & region : branch->getRegions()) {
                    if (region.empty()) { continue; }
                    if (!region.hasOneBlock() || !self(self, region.front(), depth + 1)) {
                        return false;
                    }
                }
                if (!branch.getNumResults()) { continue; }
                auto yes = llvm::cast<mlir::scf::YieldOp>(branch.getThenRegion().front().back());
                auto no = llvm::cast<mlir::scf::YieldOp>(branch.getElseRegion().front().back());
                for (auto [result, left, right] :
                     llvm::zip(branch.getResults(), yes.getOperands(), no.getOperands())) {
                    if (!charge(remaining)) { return false; }
                    attributes.join(result, left, right);
                }
                continue;
            }
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation)) {
                for (auto & region : loop->getRegions()) {
                    if (!region.hasOneBlock() || !self(self, region.front(), depth + 1)) {
                        return false;
                    }
                }
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                attributes.nonthrowingCall(call)) {
                attributes.callResult(call, call.getResult());
            }
            auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(operation);
            if (!invocation || !invocation.getNumResults()) { continue; }
            auto & called = invocation.getBody().front();
            auto call = llvm::dyn_cast<ctjs::CallOp>(called.front());
            auto exit = llvm::cast<ctjs::InvokeExitOp>(called.back());
            if (!call || call->getNextNode() != exit ||
                exit.getNormalResult() != call.getResult() || !attributes.nonthrowingCall(call)) {
                continue;
            }
            // Verification covered both complete continuations. Only the call's
            // independent effect proof permits discarding failure and its saved
            // registers. The normal continuation is outside this protection;
            // preserve its operations, including later throws, after the call.
            call->moveBefore(invocation);
            attributes.callResult(call, call.getResult());
            auto & normal = invocation.getNormalBody().front();
            normal.getArgument(0).replaceAllUsesWith(call.getResult());
            if (!self(self, normal, depth + 1)) { return false; }
            auto yield = llvm::cast<ctjs::InvokeYieldOp>(normal.back());
            for (auto & next : llvm::make_early_inc_range(normal.without_terminator())) {
                if (!charge(remaining)) { return false; }
                next.moveBefore(invocation);
            }
            invocation.replaceAllUsesWith(yield.getValues());
            invocation.erase();
        }
        return remaining != 0;
    };
    if (!normalize(normalize, function.getBody().front(), 0)) { return refuse(); }
    return llvm::Error::success();
}

llvm::Expected<bool> normalizeDOMCaughtThrow(ctjs::FuncOp function, unsigned maxSteps,
                                             llvm::ArrayRef<unsigned> elementParameters) {
    bool throws = false;
    uint64_t size = 0;
    const auto scanned = function.walk([&](mlir::Operation * operation) {
        size += uint64_t(1) + operation->getNumOperands() + operation->getNumResults();
        throws |= llvm::isa<ctjs::ThrowOp>(operation);
        return size <= maxSteps ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
    });
    if (scanned.wasInterrupted()) {
        return llvm::createStringError("DOM caught throw work budget exhausted");
    }
    if (throws) {
        return normalizeDOMCaughtCompletion(function, maxSteps - static_cast<unsigned>(size),
                                            elementParameters,
                                            ExceptionRecoveryMode::ExplicitThrows);
    }
    // Probe on a bounded clone: URI/JSON handlers still need their existing
    // throwing-call consumer when this narrower nonthrowing proof does not apply.
    if (size > maxSteps / 3) { return false; }
    mlir::OwningOpRef<ctjs::FuncOp> copy(llvm::cast<ctjs::FuncOp>(function->clone()));
    auto normalized =
        normalizeDOMCaughtCompletion(*copy, maxSteps - static_cast<unsigned>(size * 3),
                                     elementParameters, ExceptionRecoveryMode::CheckedInvocations);
    if (!normalized) {
        llvm::consumeError(normalized.takeError());
        return false;
    }
    if (*normalized) {
        function->setAttrs((*copy)->getAttrs());
        function.getBody().takeBody(copy->getBody());
    }
    return *normalized;
}

llvm::Error normalizeDOMURI(mlir::ModuleOp candidate, const HostContract & contract,
                            unsigned maxSteps, llvm::StringRef function) {
    unsigned remaining = maxSteps;
    const bool scanned = chargeWalk(candidate, remaining, 1, [](mlir::Operation * operation) {
        return uint64_t(1) + operation->getNumOperands();
    });
    const unsigned scanCost = maxSteps - remaining;
    if (!scanned || scanCost > remaining) {
        return llvm::createStringError("DOM URI normalization work budget exhausted");
    }
    remaining -= scanCost; // Reserve the complete fingerprint scan before printing the module.
    if ((contract.provider != HostContract::Provider::ctbrowserDOM &&
         contract.provider != HostContract::Provider::ctbrowserDOMSession) ||
        !llvm::is_contained(contract.initialIntrinsics, "decodeURIComponent") ||
        contract.moduleSha256 != hostContractFingerprint(candidate)) {
        return llvm::createStringError(
            "DOM URI requires its fingerprinted initial provider binding");
    }
    auto target =
        candidate.lookupSymbol<ctjs::FuncOp>(function.empty() ? contract.entry : function);
    if (!target || target.getBody().empty()) {
        return llvm::createStringError("DOM URI entry is missing");
    }
    candidate.getContext()->getOrLoadDialect<mlir::scf::SCFDialect>();
    DOMURI attempt(target, remaining, llvm::is_contained(contract.initialIntrinsics, "JSON"));
    if (!attempt.run()) { return llvm::createStringError(attempt.refusal); }
    return llvm::Error::success();
}

} // namespace ctcompile::ctnative::lowering_detail
