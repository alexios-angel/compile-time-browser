//===- Prefix.cpp - Evaluate a function's static prefix -------------------===//
//
// evaluator::runPrefix. When a whole function cannot be evaluated, the leading
// straight-line run of static operations in its entry block may still be. The
// prefix is evaluated, and every value it defines that the rest of the
// function uses is recorded as a binding for Residualize.cpp to rebuild.
//
//===----------------------------------------------------------------------===//

#include "Heap.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace ctcompile::ctnative::partial_eval {

/// Whether `op` stays in place when a prefix is evaluated: frame entry,
/// ub.poison placeholders, and global loads used only as a direct-call callee.
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
        // A backedge or a dynamic branch must keep executing its original
        // region, so the prefix stops at the first terminator, region-holding
        // op or dynamic operation. Only the straight-line prefix of the entry
        // block is evaluated.
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
        // A nested call may have changed this attempt's private heap before
        // reaching an unsupported operation, so reject the whole attempt
        // rather than keep the call against an already mutated heap.
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
