#include "Heap.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace ctcompile::ctnative::partial_eval {

bool retainedPrefixScaffolding(mlir::Operation * op) {
    if (llvm::isa<ctjs::FrameEnterOp>(op) || op->getName().getStringRef() == "ub.poison") {
        return true;
    }
    auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
    return load && !load->hasAttr(kNativeMapConstructor) && calleeSlotOnly(load);
}

std::optional<snapshot> evaluator::runPrefix(ctjs::FuncOp function, llvm::ArrayRef<value> args,
                                             llvm::function_ref<bool(mlir::Operation *)> isStatic) {
    if (maxDepth == 0) { return refuse("call-depth budget exhausted"); }
    if (!function || function.getBody().empty() || function.getUpvalueCount() != 0 ||
        !function.isPrivate()) {
        return refuse("prefix is not a closed capture-free function");
    }
    auto & entry = function.getBody().front();
    if (!entry.hasNoPredecessors() || args.size() != entry.getNumArguments()) {
        return refuse("prefix entry has predecessors or mismatched arguments");
    }
    if (!implicitArgumentsUnobserved(entry, args.size())) { return std::nullopt; }
    environment env;
    for (auto [argument, input] : llvm::zip(entry.getArguments(), args)) { env[argument] = input; }
    llvm::SmallPtrSet<mlir::Operation *, 32> prefix;
    bool initialized = false;
    for (mlir::Operation & op : entry) {
        // A backedge or a dynamic branch must continue to execute its original
        // region. This slice consumes only one straight-line entry prefix.
        if (op.hasTrait<mlir::OpTrait::IsTerminator>() || op.getNumRegions() != 0 ||
            (!isStatic(&op) && !retainedPrefixScaffolding(&op) && !llvm::isa<ctjs::RootOp>(op))) {
            state.boundary = &op;
            break;
        }
        if (++state.steps > maxSteps) { return refuse("step budget exhausted"); }
        prefix.insert(&op);
        if (retainedPrefixScaffolding(&op) || llvm::isa<ctjs::FrameExitOp, ctjs::RootOp>(op)) {
            continue;
        }
        value result = operation(&op, env, 1);
        // A nested call may have changed the attempt's private heap before
        // discovering an unsupported operation. Reject the entire attempt;
        // never retain that call against its already mutated input state.
        if (!problem.empty()) { return {}; }
        if (op.getNumResults() == 1) { env[op.getResult(0)] = result; }
        if (!llvm::isa<ctjs::ConstantOp>(op) && op.getName().getStringRef() != "arith.constant") {
            initialized = true;
        }
    }
    if (!initialized || !state.boundary) { return refuse("no static initialization prefix"); }
    const auto removedUse = [&](mlir::Operation * use) {
        // Prefix operations have no nested regions. Any use in a region of
        // the boundary, or another block, belongs to the retained suffix.
        return prefix.contains(use);
    };
    for (mlir::Operation & op : entry) {
        if (&op == state.boundary) { break; }
        if (retainedPrefixScaffolding(&op)) { continue; }
        for (mlir::Value output : op.getResults()) {
            if (llvm::any_of(output.getUsers(),
                             [&](mlir::Operation * use) { return !removedUse(use); })) {
                value evaluated = env.lookup(output);
                if (evaluated.tag == value::kind::unknown) {
                    return refuse("prefix live value cannot be residualized");
                }
                state.bindings.emplace_back(output, evaluated);
            }
        }
    }
    return std::move(state);
}

} // namespace ctcompile::ctnative::partial_eval
