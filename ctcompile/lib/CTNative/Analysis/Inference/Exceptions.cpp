//===- Exceptions.cpp - Type inference through ctjs.invoke ----------------===//
//
// TypeInference's transfer functions for a direct call inside a ctjs.invoke:
// the normal result joins the callee's return values, and the unwind block's
// payload joins the values the callee can throw.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/DenseSet.h"

namespace ctcompile::ctnative {
namespace {

/// Collects what a direct call inside a `ctjs.invoke` can complete with: the
/// values its body (and the private, capture-free functions it calls directly)
/// can throw, or the values the invoked function itself returns.
///
/// Used only to refine the types flowing out of an invoke; it is not an effect
/// summary and proves nothing for native admission. `collect` fails, and the
/// caller falls back to the conservative entry state, on any operation other
/// than a call, throw, return or pure op. The exception: frame_enter,
/// frame_exit and root are skipped when collecting return values, since their
/// failure cannot produce a return value; for unwind payloads they still fail,
/// because frame_enter's depth error is a throw not listed in `thrown`.
/// Build a fresh instance per visit. `remaining` (4096 operations) and the
/// 32-deep `active` set bound the work and the C++ recursion.
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
        auto target = call.getTarget();
        if (!target || target.getBody().empty() || target.getUpvalueCount() != 0 ||
            target.getBody().front().getNumArguments() != call.getNumOperands() ||
            !target.isPrivate()) {
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
                    // This does not allow removing the frame or any exceptional
                    // edge; native admission must prove that separately.
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

void TypeInference::joinCollected(mlir::ProgramPoint * point, TypeLattice * destination,
                                  mlir::ValueRange values) {
    for (auto value : values) {
        const auto * source = getLatticeElementFor(point, value);
        // Querying from the original point subscribes it to pending values.
        if (!source->getValue().isUninitialized()) {
            propagateIfChanged(destination, destination->join(source->getValue()));
        }
    }
}

mlir::LogicalResult TypeInference::visitCallOperation(
    mlir::CallOpInterface call,
    llvm::ArrayRef<const mlir::dataflow::AbstractSparseLattice *> operands,
    llvm::ArrayRef<mlir::dataflow::AbstractSparseLattice *> results) {
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call.getOperation());
    auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(call->getParentOp());
    if (!direct || !invocation || call->getParentRegion() != &invocation.getBody()) {
        return AbstractSparseForwardDataFlowAnalysis::visitCallOperation(call, operands, results);
    }

    // Ordinary call flow stays conservative when a callee can throw. Inside an
    // invoke, only invoke_exit carries the normal result, and its verifier keeps
    // that value from reaching the unwind block, so the helper's returns can be
    // joined directly once collect() succeeds. A throw-only body gets no
    // synthesized return.
    invokeCompletions completions{invokeCompletions::Kind::NormalReturn};
    if (!getSolverConfig().isInterprocedural() || results.size() != 1 ||
        !completions.collect(direct) || completions.returned.empty()) {
        for (auto * result : results) { setToEntryState(static_cast<TypeLattice *>(result)); }
        return mlir::success();
    }
    auto * destination = static_cast<TypeLattice *>(results.front());
    auto * point = getProgramPointAfter(call);
    joinCollected(point, destination, completions.returned);
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
    joinCollected(point, destination, completions.thrown);
}

} // namespace ctcompile::ctnative
