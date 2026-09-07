#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/DenseSet.h"

namespace ctcompile::ctnative {
namespace {

// A payload query, not an effect summary for native admission. Apart from
// explicit calls/throws/returns, only declaratively pure operations qualify.
// Unknown effects and nested handlers require a richer proof and stay boxed.
// Rebuild this query for each visit: neither markers nor earlier IR can supply
// an escaping payload. Limits bound both work and the C++ recursion stack.
struct invokePayloads {
    unsigned remaining = 4096;
    llvm::DenseSet<mlir::Operation *> active;
    llvm::DenseSet<mlir::Operation *> complete;
    llvm::SmallVector<mlir::Value> values;

    bool collect(ctjs::CallDirectOp call) {
        auto target =
            mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
        if (!target || target.getBody().empty() || target.getUpvalueCount() != 0 ||
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
                if (auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(operation)) {
                    values.push_back(thrown.getValue());
                } else if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
                    if (!collect(direct)) { return false; }
                } else if (operation.getNumRegions() != 0 ||
                           (!llvm::isa<ctjs::ReturnOp>(operation) && !mlir::isPure(&operation))) {
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
    invokePayloads payloads;
    if (!call || !payloads.collect(call) || payloads.values.empty()) {
        setAllToEntryStates(nonSuccessorInputLattices);
        return;
    }
    auto * destination = nonSuccessorInputLattices.front();
    auto * point = getProgramPointBefore(&invocation.getUnwindBody().front());
    for (auto value : payloads.values) {
        const auto * source = getLatticeElementFor(point, value);
        // An operand not reached yet is not boxed. Subscribing this block's
        // program point revisits the payload when the callee's value widens.
        if (!source->getValue().isUninitialized()) {
            propagateIfChanged(destination, destination->join(source->getValue()));
        }
    }
}

} // namespace ctcompile::ctnative
