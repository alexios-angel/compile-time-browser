#include "CallProof.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative::partial_eval {
namespace {
ctjs::FuncOp indexedTarget(mlir::ModuleOp module, unsigned index) {
    ctjs::FuncOp target;
    unsigned matches = 0;
    module.walk([&](ctjs::FuncOp function) {
        if (functionIndex(function) == index) {
            target = function;
            ++matches;
        }
    });
    return matches == 1 ? target : ctjs::FuncOp{};
}
ctjs::FuncOp actualCallee(ctjs::CallDirectOp invoked, value callee, const snapshot & state,
                          mlir::ModuleOp module) {
    if (callee.tag == value::kind::reference && callee.node < state.heap.size()) {
        const auto & item = state.heap[callee.node];
        return item.tag == node::kind::closure ? indexedTarget(module, item.function)
                                               : ctjs::FuncOp{};
    }
    // A global load is normally only bookkeeping in the evaluator. Recover
    // its callable identity only from the same closed declaration contract
    // used by source specialization, never from a provenance annotation.
    auto load = invoked.getCalleeValue().getDefiningOp<ctjs::LoadGlobalOp>();
    if (!load) { return {}; }
    ctjs::StoreGlobalOp declaration;
    unsigned stores = 0;
    module.walk([&](ctjs::StoreGlobalOp store) {
        if (store.getName() == load.getName()) {
            declaration = store;
            ++stores;
        }
    });
    if (stores != 1) { return {}; }
    auto made = declaration.getValue().getDefiningOp<ctjs::CreateClosureOp>();
    auto target = made && made.getFunction() >= 0
                      ? indexedTarget(module, static_cast<unsigned>(made.getFunction()))
                      : ctjs::FuncOp{};
    return target && closedDeclaration(declaration, target, module) ? target : ctjs::FuncOp{};
}
bool undefined(value input) {
    return input.tag == value::kind::constant && llvm::isa<ctjs::UndefinedAttr>(input.constant);
}
} // namespace

value evaluator::directCall(ctjs::CallDirectOp invoked, environment & env, unsigned depth) {
    llvm::SmallVector<value> args;
    for (mlir::Value operand : invoked.getOperands()) { args.push_back(env.lookup(operand)); }
    auto target =
        mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(invoked, invoked.getCalleeAttr());
    if (!target || args.size() < 3 || !undefined(args[1])) {
        return fail("direct call has no proved ordinary target");
    }
    auto original = actualCallee(invoked, args[2], state, module);
    if (!original) {
        // Existing handwritten native-only IR uses an inert undefined callee
        // slot for symbols without a bytecode identity. Actual callable values
        // and indexed source functions never qualify for this convention.
        if (!functionIndex(target) && undefined(args[2])) { return call(target, args, depth); }
        return fail("direct call boxed callee identity is unproved");
    }
    if (original == target) { return call(target, args, depth); }

    // A specialization may have folded or residualized its body. Validate its
    // result and local heap effects under these actual arguments, instead of
    // inferring equivalence from a name, matching schema, or provenance tags.
    snapshot before = state;
    value candidateResult = call(target, args, depth);
    if (!problem.empty()) {
        state = std::move(before);
        return {};
    }
    snapshot candidate = std::move(state);
    state = before;
    // Both executions and graph comparison consume one attempt's step budget.
    // Nodes and call depth retain their existing per-execution bounds.
    state.steps = candidate.steps;
    value originalResult = call(original, args, depth);
    if (!problem.empty()) {
        state = std::move(before);
        return {};
    }
    const auto proof =
        equivalentCallState(candidate, candidateResult, state, originalResult,
                            static_cast<unsigned>(before.heap.size()), state.steps, maxSteps);
    if (proof != callProofResult::equivalent) {
        state = std::move(before);
        return fail(proof == callProofResult::exhausted
                        ? "direct call equivalence budget exhausted"
                        : "direct call target and boxed callee disagree");
    }
    const unsigned spent = state.steps;
    state = std::move(candidate);
    state.steps = spent;
    return candidateResult;
}
} // namespace ctcompile::ctnative::partial_eval
