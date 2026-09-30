#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"

#include <bit>
#include <utility>

// Small IR shapes shared by the DOM proofs and normalizers.
namespace ctcompile::ctnative {

// The comparison a truthy flag tests, or null.
inline ctjs::CompareOp truthyComparison(mlir::Value flag) {
    auto truth = flag ? flag.getDefiningOp<ctjs::TruthyOp>() : ctjs::TruthyOp{};
    return truth ? truth.getValue().getDefiningOp<ctjs::CompareOp>() : ctjs::CompareOp{};
}

// Whether `compare` is `subject === undefined`, either way round.
inline bool strictUndefinedTest(ctjs::CompareOp compare, mlir::Value subject) {
    return compare && compare.getKind() == ctjs::CompareKind::StrictEq &&
           ((compare.getLhs() == subject && ctjs::isUndefinedConstant(compare.getRhs())) ||
            (compare.getRhs() == subject && ctjs::isUndefinedConstant(compare.getLhs())));
}

// The comparison guarding `read` at `parent`, with the index it tests there:
// the condition of an scf.if whose then arm holds the read, or of the scf.while
// whose after-block argument keys it. Null otherwise.
inline std::pair<ctjs::CompareOp, mlir::Value> indexGuard(mlir::Operation * parent,
                                                          ctjs::GetPropertyOp read) {
    if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(parent);
        branch && branch.getThenRegion().isAncestor(read->getParentRegion())) {
        return {truthyComparison(branch.getCondition()), read.getKey()};
    }
    auto index = llvm::dyn_cast<mlir::BlockArgument>(read.getKey());
    auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(parent);
    if (!index || !loop || !loop.getAfter().hasOneBlock() || !loop.getBefore().hasOneBlock() ||
        index.getOwner() != &loop.getAfter().front()) {
        return {};
    }
    auto condition =
        llvm::dyn_cast<mlir::scf::ConditionOp>(loop.getBefore().front().getTerminator());
    if (!condition || condition.getArgs().size() <= index.getArgNumber()) { return {}; }
    return {truthyComparison(condition.getCondition()), condition.getArgs()[index.getArgNumber()]};
}

// Check the materialized copy's own bound before aliasing its members to an
// original, potentially longer NodeList. Complete DOM reproof checks progression.
template <typename Spend>
bool hasOwnSnapshotBound(ctjs::GetPropertyOp read, mlir::Value snapshot, Spend spend) {
    if (!llvm::isa<mlir::BlockArgument>(read.getKey())) { return false; }
    for (auto * parent = read->getParentOp(); parent; parent = parent->getParentOp()) {
        if (!spend()) { return false; }
        auto [compare, index] = indexGuard(parent, read);
        auto bound =
            compare ? compare.getRhs().getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
        if (compare && compare.getKind() == ctjs::CompareKind::Lt && compare.getLhs() == index &&
            bound && bound.getObject() == snapshot &&
            ctjs::constantKey(bound.getKey()) == "length") {
            return true;
        }
    }
    return false;
}

// An scf.if on `test` whose arms yield `yes` and `no`; returns its result.
inline mlir::Value emitSelect(mlir::OpBuilder & at, mlir::Location where, mlir::TypeRange types,
                              mlir::Value test, mlir::Value yes, mlir::Value no) {
    auto branch = mlir::scf::IfOp::create(at, where, types, test);
    for (auto [region, value] :
         llvm::zip(branch->getRegions(), llvm::ArrayRef<mlir::Value>{yes, no})) {
        auto & arm = region.emplaceBlock();
        mlir::OpBuilder nested(&arm, arm.begin());
        mlir::scf::YieldOp::create(nested, where, value);
    }
    return branch.getResult(0);
}

// min(object[key], 2^24): the VM materializes at most 2^24 proxy slots.
inline mlir::Value cappedLength(mlir::OpBuilder & at, mlir::Location where, mlir::Type type,
                                mlir::Value object, mlir::Value key) {
    auto size = ctjs::GetPropertyOp::create(at, where, type, object, key);
    auto cap = ctjs::ConstantOp::create(
        at, where,
        ctjs::NumberAttr::get(at.getContext(), std::bit_cast<uint64_t>(double(1U << 24))));
    auto less = ctjs::CompareOp::create(at, where, type, ctjs::CompareKind::Lt, size.getResult(),
                                        cap.getResult());
    auto test = ctjs::TruthyOp::create(at, where, at.getI1Type(), less.getResult());
    return emitSelect(at, where, mlir::TypeRange{type}, test.getResult(), size, cap);
}

// Whether `text` parses as a complete selector list. Callers charge its bytes.
bool validSelector(llvm::StringRef text);

// One operation's size: itself, its operands and results (and attributes, if
// asked), and each nested block with its arguments.
inline uint64_t operationWork(mlir::Operation * operation, bool attributes) {
    uint64_t cost = uint64_t(1) + operation->getNumOperands() + operation->getNumResults() +
                    (attributes ? operation->getAttrs().size() : 0);
    for (auto & region : operation->getRegions()) {
        for (auto & block : region) { cost += uint64_t(1) + block.getNumArguments(); }
    }
    return cost;
}

// Whether no symbol use in `module` names `function`.
inline bool symbolUnused(ctjs::FuncOp function, mlir::ModuleOp module) {
    return mlir::SymbolTable::symbolKnownUseEmpty(function, module.getOperation()) &&
           mlir::SymbolTable::symbolKnownUseEmpty(function, &module.getBodyRegion());
}

// The throw of a saved-throw region - a no-inline, result-free scf.execute_region
// holding exactly one ctjs.throw - or null for any other operation.
inline ctjs::ThrowOp savedThrow(mlir::Operation * operation) {
    auto abrupt = llvm::dyn_cast_or_null<mlir::scf::ExecuteRegionOp>(operation);
    if (!abrupt || !abrupt.getNoInline() || abrupt.getNumResults() || abrupt->getNumOperands() ||
        !abrupt.getRegion().hasOneBlock() || abrupt.getRegion().front().getNumArguments() ||
        !llvm::hasSingleElement(abrupt.getRegion().front())) {
        return {};
    }
    return llvm::dyn_cast<ctjs::ThrowOp>(abrupt.getRegion().front().front());
}

inline void emitSavedThrow(mlir::OpBuilder & at, mlir::Location where, mlir::Value value) {
    auto scope = mlir::scf::ExecuteRegionOp::create(at, where, mlir::TypeRange{});
    scope.setNoInline(true);
    auto & throwing = scope.getRegion().emplaceBlock();
    mlir::OpBuilder inside(&throwing, throwing.begin());
    ctjs::ThrowOp::create(inside, where, value);
}

// The call an invoke protects when its body is exactly that call and the exit
// forwarding its sole-use result; null otherwise.
inline ctjs::CallOp invokedCall(ctjs::InvokeOp invocation) {
    auto & body = invocation.getBody().front();
    auto call = body.empty() ? ctjs::CallOp{} : llvm::dyn_cast<ctjs::CallOp>(body.front());
    auto exit =
        body.empty() ? ctjs::InvokeExitOp{} : llvm::dyn_cast<ctjs::InvokeExitOp>(body.back());
    if (body.getNumArguments() || !call || !exit || call->getNextNode() != exit ||
        exit.getNormalResult() != call.getResult() || !call.getResult().hasOneUse()) {
        return {};
    }
    return call;
}

// A result-free invoke of a clone of `call` whose continuations ignore both
// outcomes. Returns the clone.
inline mlir::Operation * emitSuppressedCall(mlir::OpBuilder & at, mlir::Operation & call,
                                            mlir::IRMapping & mapping) {
    mlir::OperationState state(call.getLoc(), ctjs::InvokeOp::getOperationName());
    for (unsigned index = 0; index != 3; ++index) { state.addRegion(); }
    auto invocation = llvm::cast<ctjs::InvokeOp>(at.create(state));
    auto & body = invocation.getBody().emplaceBlock();
    mlir::OpBuilder inside = mlir::OpBuilder::atBlockEnd(&body);
    auto * cloned = inside.clone(call, mapping);
    ctjs::InvokeExitOp::create(inside, call.getLoc(), cloned->getResult(0), mlir::ValueRange{});
    for (auto * region : {&invocation.getNormalBody(), &invocation.getUnwindBody()}) {
        auto & block = region->emplaceBlock();
        block.addArgument(ctjs::ValueType::get(call.getContext()), call.getLoc());
        mlir::OpBuilder continuation = mlir::OpBuilder::atBlockEnd(&block);
        ctjs::InvokeYieldOp::create(continuation, call.getLoc(), mlir::ValueRange{});
    }
    return cloned;
}

// Replace `branch` by the operations and yielded values of its selected arm.
// An absent else arm contributes nothing.
inline void inlineSelectedArm(mlir::scf::IfOp branch, bool condition) {
    auto & region = condition ? branch.getThenRegion() : branch.getElseRegion();
    if (!region.empty()) {
        auto & body = region.front();
        auto yield = llvm::cast<mlir::scf::YieldOp>(body.getTerminator());
        branch.replaceAllUsesWith(yield.getOperands());
        yield.erase();
        branch->getBlock()->getOperations().splice(branch->getIterator(), body.getOperations());
    }
    branch.erase();
}

// Replace `operation` by a copy with only its `kept` results (ascending),
// moving its regions. Callers trim each terminator to match.
inline mlir::Operation * keepResults(mlir::Operation * operation, llvm::ArrayRef<unsigned> kept) {
    mlir::OperationState state(operation->getLoc(), operation->getName());
    state.addOperands(operation->getOperands());
    for (unsigned index : kept) { state.addTypes(operation->getResult(index).getType()); }
    state.addAttributes(operation->getAttrs());
    for (auto & region : operation->getRegions()) { state.addRegion()->takeBody(region); }
    mlir::OpBuilder at(operation);
    auto * copied = at.create(state);
    for (auto [position, index] : llvm::enumerate(kept)) {
        operation->getResult(index).replaceAllUsesWith(
            copied->getResult(static_cast<unsigned>(position)));
    }
    operation->erase();
    return copied;
}

// Incoming SCF edges of a while block argument or an if/while result. Only
// transport is followed; the caller decides what a leaf may be.
inline bool transportInputs(mlir::Value value, llvm::SmallVectorImpl<mlir::Value> & inputs) {
    if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value)) {
        auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(argument.getOwner()->getParentOp());
        if (!loop) { return false; }
        auto index = argument.getArgNumber();
        if (argument.getOwner() == &loop.getBefore().front()) {
            inputs.push_back(loop.getInits()[index]);
            inputs.push_back(loop.getAfter().front().getTerminator()->getOperand(index));
        } else {
            inputs.push_back(
                llvm::cast<mlir::scf::ConditionOp>(loop.getBefore().front().getTerminator())
                    .getArgs()[index]);
        }
        return true;
    }
    auto result = llvm::dyn_cast<mlir::OpResult>(value);
    if (!result) { return false; }
    if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(result.getOwner())) {
        for (auto & region : branch->getRegions()) {
            inputs.push_back(region.front().getTerminator()->getOperand(result.getResultNumber()));
        }
        return true;
    }
    if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(result.getOwner())) {
        inputs.push_back(
            llvm::cast<mlir::scf::ConditionOp>(loop.getBefore().front().getTerminator())
                .getArgs()[result.getResultNumber()]);
        return true;
    }
    return false;
}

// The operand an integer comparison tests against an arith.constant, with that
// constant; the constant is null when neither side is one.
inline std::pair<mlir::Value, mlir::arith::ConstantOp> constantComparison(
    mlir::arith::CmpIOp compare) {
    if (auto constant = compare.getRhs().getDefiningOp<mlir::arith::ConstantOp>()) {
        return {compare.getLhs(), constant};
    }
    return {compare.getRhs(), compare.getLhs().getDefiningOp<mlir::arith::ConstantOp>()};
}

// A constant completion tag: non-negative with at most `bits` active bits.
inline mlir::IntegerAttr exactTag(mlir::arith::ConstantOp constant, unsigned bits) {
    auto key =
        constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue()) : mlir::IntegerAttr{};
    return key && !key.getValue().isNegative() && key.getValue().getActiveBits() <= bits
               ? key
               : mlir::IntegerAttr{};
}

} // namespace ctcompile::ctnative
