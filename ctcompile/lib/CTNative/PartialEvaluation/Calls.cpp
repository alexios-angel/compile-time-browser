//===- Calls.cpp - Evaluate direct calls ----------------------------------===//
//
// evaluator::directCall. A ctjs.call_direct may target a specialised clone of
// the function its callee value names. When the target differs from that
// original, both are evaluated and their results and heap effects are proved
// equivalent (CallProof.h) before the target's result is used.
//
//===----------------------------------------------------------------------===//

#include "CallProof.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative::partial_eval {
namespace {
/// The function the callee value of `invoked` actually names: the closure's
/// function for a heap closure, or the unique closed declaration for a global
/// load. Null when it cannot be proved.
ctjs::FuncOp actualCallee(ctjs::CallDirectOp invoked, value callee, const snapshot & state,
                          mlir::ModuleOp module) {
    if (callee.tag == value::kind::reference && callee.node < state.heap.size()) {
        const auto & item = state.heap[callee.node];
        return item.tag == node::kind::closure ? ctjs::uniqueFunction(module, item.function)
                                               : ctjs::FuncOp{};
    }
    // The evaluator normally treats a global load as bookkeeping. Recover its
    // callable identity only from the same closed-declaration rule that
    // source specialization uses, never from a provenance annotation.
    auto load = invoked.getCalleeValue().getDefiningOp<ctjs::LoadGlobalOp>();
    if (!load) { return {}; }
    auto declaration = ctjs::uniqueGlobalStore(module, load.getName());
    if (!declaration) { return {}; }
    auto made = declaration.getValue().getDefiningOp<ctjs::CreateClosureOp>();
    auto target = made && made.getFunction() >= 0
                      ? ctjs::uniqueFunction(module, static_cast<unsigned>(made.getFunction()))
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
    auto target = invoked.getTarget();
    if (!target || args.size() < ctjs::implicit_arguments ||
        !undefined(args[ctjs::arg_new_target])) {
        return fail("direct call has no proved ordinary target");
    }
    auto original = actualCallee(invoked, args[ctjs::arg_callee], state, module);
    if (!original) {
        // Hand-written native-only IR passes an undefined callee for a symbol
        // with no source function index. Real callable values and indexed
        // source functions never use that convention.
        if (!functionIndex(target) && undefined(args[ctjs::arg_callee])) {
            return call(target, args, depth);
        }
        return fail("direct call boxed callee identity is unproved");
    }
    if (original == target) { return call(target, args, depth); }

    // A specialization may have folded or residualized its body. Check its
    // result and heap effects under these actual arguments, instead of
    // inferring equivalence from a name, a matching signature, or provenance
    // tags.
    snapshot before = state;
    value candidateResult = call(target, args, depth);
    if (!problem.empty()) {
        state = std::move(before);
        return {};
    }
    snapshot candidate = std::move(state);
    state = before;
    // Both executions and the graph comparison share one step budget. Node
    // and call-depth limits still apply to each execution separately.
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
