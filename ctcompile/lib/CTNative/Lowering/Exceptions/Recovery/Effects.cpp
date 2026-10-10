//===- Effects.cpp - Recovery effect proof ----------------------*- C++ -*-===//
//
// The effect proof: completion inputs across an acyclic call family, what
// counts as a primitive value, and which operations cannot throw or reenter.
//
// Member functions of `recovery`, the disposable-clone transaction declared in
// Attempt.h; the rest of the lowering uses only Exceptions/Recovery.h.
//
//===----------------------------------------------------------------------===//

#include "../../LoweringSupport.h"
#include "Attempt.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {
/// True when `operation` is pure and has none of CTJS's may-throw, may-reenter
/// or may-suspend traits; MLIR purity alone does not rule those out.
bool effectFree(mlir::Operation * operation) {
    return mlir::isPure(operation) && !operation->hasTrait<ctjs::CTJSMayThrow>() &&
           !operation->hasTrait<ctjs::CTJSMayReenterJS>() &&
           !operation->hasTrait<ctjs::CTJSMaySuspend>();
}
} // namespace

std::optional<unsigned> recovery::contextFor(ctjs::CallDirectOp call, unsigned parent,
                                             primitiveContexts & contexts) {
    if (!spend()) { return {}; }
    auto key = std::pair{call.getOperation(), parent};
    if (auto found = contexts.indices.find(key); found != contexts.indices.end()) {
        return found->second;
    }
    // Bounds the call-family walk so a deep (but acyclic) call chain cannot
    // make admission expensive; deeper chains are refused, not truncated.
    if (contexts.frames[parent].depth >= 32) {
        reject("native invocation completion call depth exceeds 32");
        return {};
    }
    for (unsigned ancestor = parent; ancestor != 0; ancestor = contexts.frames[ancestor].parent) {
        if (!spend()) { return {}; }
        if (contexts.frames[ancestor].call.getCallee() == call.getCallee()) {
            reject("native invocation completion call component is recursive");
            return {};
        }
    }
    const unsigned index = static_cast<unsigned>(contexts.frames.size());
    contexts.frames.push_back({call, parent, contexts.frames[parent].depth + 1});
    contexts.indices.try_emplace(key, index);
    return index;
}

bool recovery::completionInputs(ctjs::CallDirectOp call, bool thrown, unsigned parent,
                                primitiveContexts & contexts,
                                llvm::SmallVectorImpl<primitiveInput> & inputs) {
    // Collects, within the work budget, the values that can complete `call`,
    // independently of carrier selection and native admission. A return
    // completes only the invoked body, whereas an uncaught throw in any
    // descendant can complete its unwind; the exact chain of actual operands
    // is kept for every such completion.
    auto initial = contextFor(call, parent, contexts);
    if (!initial) { return false; }
    llvm::SmallVector<unsigned> pending{*initial};
    llvm::DenseSet<unsigned> seen;
    bool found = false;
    while (!pending.empty()) {
        if (!spend()) { return false; }
        const unsigned context = pending.pop_back_val();
        if (!seen.insert(context).second) { continue; }
        call = contexts.frames[context].call;
        auto target = module ? module.lookupSymbol<ctjs::FuncOp>(call.getCallee()) : ctjs::FuncOp{};
        if (!target || !boundTargets.contains(target) || !boundCalls.contains(call) ||
            target.getBody().empty() || target.getUpvalueCount() != 0 ||
            target.getBody().front().getNumArguments() != call.getNumOperands() ||
            mlir::SymbolTable::getSymbolVisibility(target) !=
                mlir::SymbolTable::Visibility::Private) {
            return false;
        }
        for (mlir::Block & block : target.getBody()) {
            if (!spend(uint64_t(1) + block.getNumArguments())) { return false; }
            for (mlir::Operation & operation : block) {
                if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
                if (auto exit = llvm::dyn_cast<ctjs::ThrowOp>(operation)) {
                    if (thrown) {
                        inputs.emplace_back(exit.getValue(), context);
                        found = true;
                    }
                } else if (auto exit = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                    if (!thrown && context == *initial) {
                        inputs.emplace_back(exit.getValue(), context);
                        found = true;
                    }
                } else if (auto nested = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
                    auto child = contextFor(nested, context, contexts);
                    if (!child) { return false; }
                    pending.push_back(*child);
                } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
                    if (!bindings.contains(load.getNameAttr())) { return false; }
                } else if (operation.getNumRegions() != 0 ||
                           (!llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                                       mlir::cf::BranchOp, mlir::cf::CondBranchOp>(operation) &&
                            !effectFree(&operation))) {
                    return false;
                }
            }
        }
    }
    return found;
}

bool recovery::primitive(mlir::Value value) {
    // This source CFG has already passed the acyclic-tail check. Follow
    // every predecessor operand, including status unwind state: a value
    // joining a primitive and an unknown never proves conversion safe.
    // staticResultType supplies only unconditional normal-result facts;
    // its producer's effects are checked separately below. No cached
    // dataflow state or user-supplied native marker is consulted.
    primitiveContexts contexts;
    llvm::SmallVector<primitiveInput> pending{{value, 0}};
    llvm::DenseSet<primitiveInput> seen;
    bool hasDefinition = false;
    while (!pending.empty()) {
        if (!spend()) { return false; }
        auto [current, context] = pending.pop_back_val();
        value = current;
        if (!seen.insert({value, context}).second) { continue; }
        if (auto * definition = value.getDefiningOp()) {
            const auto type = staticResultType(definition);
            if (primitiveOrAbsent(type)) {
                hasDefinition = true;
                continue;
            }
            if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(definition)) {
                if (!completionInputs(direct, false, context, contexts, pending)) { return false; }
                continue;
            }
            if (definition == landing.getOperation() && value == landing.getThrown()) {
                if (context != 0 || invocations.empty() || throws != 0) { return false; }
                for (auto [check, invocation] : invocations) {
                    (void)check;
                    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(invocation);
                    if (!spend() || !direct ||
                        !completionInputs(direct, true, 0, contexts, pending)) {
                        return false;
                    }
                }
                continue;
            }
            if (llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp>(definition)) {
                for (auto operand : definition->getOperands()) {
                    pending.emplace_back(operand, context);
                }
                continue;
            }
            return false;
        }
        auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
        if (argument && argument.getOwner()->isEntryBlock()) {
            auto owner = argument.getOwner()->getParentOp();
            auto body = llvm::dyn_cast<ctjs::FuncOp>(owner);
            if (!body) { return false; }
            if (context != 0) {
                auto frame = contexts.frames[context];
                if (body.getSymName() != frame.call.getCallee() ||
                    argument.getArgNumber() >= frame.call.getNumOperands()) {
                    return false;
                }
                pending.emplace_back(frame.call->getOperand(argument.getArgNumber()), frame.parent);
                continue;
            }
            // No selected invocation: a body-wide effect proof needs all
            // live actuals, including callers outside the protected try.
            // The complete acyclic call-graph scan above prevents optimistic
            // recursion through a formal with one known incoming value.
            auto target =
                module ? module.lookupSymbol<ctjs::FuncOp>(body.getSymName()) : ctjs::FuncOp{};
            auto callers = boundCallers.find(target);
            if (!target || !boundTargets.contains(target) || callers == boundCallers.end() ||
                callers->second.empty()) {
                return false;
            }
            for (auto call : callers->second) {
                if (!spend() || argument.getArgNumber() >= call.getNumOperands()) { return false; }
                pending.emplace_back(call->getOperand(argument.getArgNumber()), 0);
            }
            continue;
        }
        if (!argument || argument.getOwner()->isEntryBlock() ||
            argument.getOwner()->hasNoPredecessors()) {
            return false;
        }
        auto & block = *argument.getOwner();
        for (auto pred = block.pred_begin(), end = block.pred_end(); pred != end; ++pred) {
            if (!spend()) { return false; }
            auto branch = llvm::dyn_cast<mlir::BranchOpInterface>((*pred)->getTerminator());
            if (!branch) { return false; }
            auto operands = branch.getSuccessorOperands(pred.getSuccessorIndex());
            const unsigned index = argument.getArgNumber();
            if (index >= operands.size() || operands.isOperandProduced(index)) { return false; }
            pending.emplace_back(operands[index], context);
        }
    }
    return hasDefinition;
}

bool recovery::nonthrowing(mlir::Operation * operation) {
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
        return bindings.contains(load.getNameAttr());
    }
    if (llvm::isa<ctjs::CheckOp, ctjs::ReturnOp, ctjs::ThrowOp, ctjs::PopHandlerOp,
                  ctjs::FrameExitOp, ctjs::CatchLandOp, ctjs::RootOp, mlir::cf::BranchOp,
                  mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(operation)) {
        return true;
    }
    // Unknown operations have neither an effect proof nor a special case.
    if (effectFree(operation)) { return true; }
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
        if (unary.getKind() == ctjs::UnaryKind::Not || unary.getKind() == ctjs::UnaryKind::TypeOf ||
            unary.getKind() == ctjs::UnaryKind::Void) {
            return true;
        }
    }
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
        if (compare.getKind() == ctjs::CompareKind::StrictEq) { return true; }
    }
    if (auto convert = llvm::dyn_cast<ctjs::ConvertOp>(operation)) {
        if (convert.getKind() == ctjs::ConvertKind::ToBoolean) { return true; }
        if (convert.getKind() == ctjs::ConvertKind::ToObject) { return false; }
        return primitive(convert.getOperand());
    }
    if (llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp, ctjs::CompareOp>(
            operation)) {
        return llvm::all_of(operation->getOperands(),
                            [&](mlir::Value value) { return primitive(value); });
    }
    return false;
}

bool recovery::proveEffects(const tail & normal, const tail & caught) {
    // The original ctjs.check still branches to both successors while this
    // runs; nothing is rewritten until the proof succeeds. Only the exact call
    // recorded by inspectInvocation has a represented unwind; a property read,
    // callee lookup, second call or callback may not rely on it. Both
    // continuations, including unchecked catch operations, are scanned before
    // any recovered body is built or adopted.
    bool needsBindings = false;
    for (const tail * plan : {&normal, &caught}) {
        for (mlir::Block * block : plan->blocks) {
            for (mlir::Operation & operation : *block) {
                if (!spend()) { return false; }
                needsBindings |= llvm::isa<ctjs::LoadGlobalOp>(operation);
            }
        }
    }
    if (needsBindings && !proveBindings()) { return false; }
    for (const tail * plan : {&normal, &caught}) {
        for (mlir::Block * block : plan->blocks) {
            for (mlir::Operation & operation : *block) {
                if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
                auto invocation = invocations.lookup(block->getTerminator());
                // Structural recovery also records ordinary (unresolved) calls.
                // This proof cannot vouch for a host or unknown callee, so only
                // a resolved call_direct invocation is skipped here, even when
                // the full unwind snapshot was preserved.
                if (plan == &normal && invocation == &operation &&
                    llvm::isa<ctjs::CallDirectOp>(operation)) {
                    continue;
                }
                if (!nonthrowing(&operation)) {
                    return reject(("native invocation recovery cannot prove a nonthrowing "
                                   "status or continuation operation: `" +
                                   operation.getName().getStringRef() + "`")
                                      .str());
                }
            }
        }
    }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
