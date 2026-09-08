#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/DenseSet.h"

namespace ctcompile::ctnative {
namespace {

// A completion query, not an effect summary for native admission. Apart from
// explicit calls/throws/returns, only declaratively pure operations qualify
// for unwind payloads. Normal returns can also cross imported frame/root
// bookkeeping: its failure cannot supply a successful return's value. In
// particular frame_enter's depth failure is NOT an explicit thrown operand,
// so it still prevents inferring an explicit-only unwind payload.
// Unknown effects and nested handlers require a richer proof and stay boxed.
// Rebuild this query for each visit: neither markers nor earlier IR can supply
// an escaping payload or a normal return. Limits bound both work and the C++
// recursion stack.
struct invokeCompletions {
    enum class Kind {
        NormalReturn,
        Unwind
    };

    Kind kind;
    unsigned remaining = 4096;
    llvm::DenseSet<mlir::Operation *> active;
    llvm::DenseSet<mlir::Operation *> complete;
    llvm::SmallVector<mlir::Value> thrown;
    llvm::SmallVector<mlir::Value> returned;

    explicit invokeCompletions(Kind kind) : kind(kind) {}

    bool collect(ctjs::CallDirectOp call) {
        auto target =
            mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
        if (!target || target.getBody().empty() || target.getUpvalueCount() != 0 ||
            target.getBody().front().getNumArguments() != call.getNumOperands() ||
            mlir::SymbolTable::getSymbolVisibility(target) !=
                mlir::SymbolTable::Visibility::Private) {
            return false;
        }
        if (complete.contains(target)) { return true; }
        if (active.size() >= 32 || !active.insert(target).second) { return false; }
        for (auto & block : target.getBody()) {
            for (auto & operation : block) {
                if (remaining == 0) { return false; }
                --remaining;
                if (auto completion = llvm::dyn_cast<ctjs::ThrowOp>(operation)) {
                    thrown.push_back(completion.getValue());
                } else if (auto completion = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                    // Only returns from the invoked helper complete this
                    // invocation. Returns inside its callees do not.
                    if (active.size() == 1) { returned.push_back(completion.getValue()); }
                } else if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
                    if (!collect(direct)) { return false; }
                } else if (kind == Kind::NormalReturn &&
                           llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(
                               operation)) {
                    // This is not permission to remove the frame or any
                    // exceptional edge. Native admission must independently
                    // prove the complete component before dropping either.
                    continue;
                } else if (operation.getNumRegions() != 0 || !mlir::isPure(&operation)) {
                    return false;
                }
            }
        }
        active.erase(target);
        complete.insert(target);
        return true;
    }
};

} // namespace

mlir::LogicalResult TypeInference::visitCallOperation(
    mlir::CallOpInterface call,
    llvm::ArrayRef<const mlir::dataflow::AbstractSparseLattice *> operands,
    llvm::ArrayRef<mlir::dataflow::AbstractSparseLattice *> results) {
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call.getOperation());
    auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(call->getParentOp());
    if (!direct || !invocation || call->getParentRegion() != &invocation.getBody()) {
        return AbstractSparseForwardDataFlowAnalysis::visitCallOperation(call, operands, results);
    }

    // Ordinary CallOp flow correctly stays conservative when a callee has a
    // throw exit. Here invoke_exit alone selects the normal result, and its
    // verifier prevents that value from reaching the unwind continuation.
    // Join the actual helper's returns only after rebuilding the bounded
    // completion proof. No return is synthesized for a throw-only body.
    invokeCompletions completions{invokeCompletions::Kind::NormalReturn};
    if (!getSolverConfig().isInterprocedural() || results.size() != 1 ||
        !completions.collect(direct) || completions.returned.empty()) {
        for (auto * result : results) { setToEntryState(static_cast<TypeLattice *>(result)); }
        return mlir::success();
    }
    auto * destination = static_cast<TypeLattice *>(results.front());
    auto * point = getProgramPointAfter(call);
    for (auto value : completions.returned) {
        const auto * source = getLatticeElementFor(point, value);
        if (!source->getValue().isUninitialized()) {
            propagateIfChanged(destination, destination->join(source->getValue()));
        }
    }
    return mlir::success();
}

void TypeInference::visitNonControlFlowArguments(
    mlir::Operation * op, const mlir::RegionSuccessor & successor,
    mlir::ValueRange nonSuccessorInputs, llvm::ArrayRef<TypeLattice *> nonSuccessorInputLattices) {
    auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(op);
    if (!invocation || successor.getSuccessor() != &invocation.getUnwindBody() ||
        nonSuccessorInputs.size() != 1 || nonSuccessorInputLattices.size() != 1 ||
        invocation.getBody().empty()) {
        setAllToEntryStates(nonSuccessorInputLattices);
        return;
    }
    auto call = llvm::dyn_cast<ctjs::CallDirectOp>(invocation.getBody().front().front());
    invokeCompletions completions{invokeCompletions::Kind::Unwind};
    if (!call || !completions.collect(call) || completions.thrown.empty()) {
        setAllToEntryStates(nonSuccessorInputLattices);
        return;
    }
    auto * destination = nonSuccessorInputLattices.front();
    auto * point = getProgramPointBefore(&invocation.getUnwindBody().front());
    for (auto value : completions.thrown) {
        const auto * source = getLatticeElementFor(point, value);
        // An operand not reached yet is not boxed. Subscribing this block's
        // program point revisits the payload when the callee's value widens.
        if (!source->getValue().isUninitialized()) {
            propagateIfChanged(destination, destination->join(source->getValue()));
        }
    }
}

} // namespace ctcompile::ctnative
