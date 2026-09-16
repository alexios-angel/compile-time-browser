// recovery - inspection: the one-handler shape the preserved importer
// CFG must have, and what a checked invocation must look like to be represented.
//
// One of six files carved out of a 1,251-line Exceptions/Recovery.cpp on
// 2026-09-08. All are member functions of `recovery`, the disposable-clone
// transaction declared in Attempt.h beside this; Exceptions/Recovery.h, the
// header the rest of the lowering sees, did not change.

#include "Attempt.h"

namespace ctcompile::ctnative::lowering_detail {

bool recovery::inspect() {
    unsigned pushes = 0, landings = 0, frames = 0;
    for (mlir::Block & block : function.getBody()) {
        if (!spend(uint64_t(1) + block.getNumArguments())) { return false; }
        for (mlir::Operation & operation : block) {
            if (!spend(uint64_t(1) + operation.getNumOperands())) { return false; }
            if (operation.getNumRegions() != 0) {
                return reject("native exception recovery requires the preserved importer CFG");
            }
            if (auto found = llvm::dyn_cast<ctjs::PushHandlerOp>(operation)) {
                push = found;
                ++pushes;
            }
            if (auto found = llvm::dyn_cast<ctjs::CatchLandOp>(operation)) {
                landing = found;
                ++landings;
            }
            if (auto found = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
                frame = found;
                ++frames;
            }
        }
    }
    if (pushes != 1 || landings != 1 || frames != 1 ||
        frame->getBlock() != &function.getBody().front() ||
        push.getHandler() != landing->getBlock() || push.getBody() == push.getHandler() ||
        !landing.getPad().use_empty()) {
        return reject(
            "native exception recovery needs one handler, entry frame and dedicated catch landing");
    }
    width = push.getBody()->getNumArguments();
    if (width == 0 || push.getHandler()->getNumArguments() != width ||
        push.getBodyOperands().size() != width || push.getHandlerOperands().size() != width ||
        !llvm::equal(push.getBodyOperands(), push.getHandlerOperands())) {
        return reject("native exception recovery needs matching complete entry register vectors");
    }
    auto count = frame->getAttrOfType<mlir::IntegerAttr>("reg_count");
    if (!count || count.getInt() != width) {
        return reject("native exception recovery register vector does not match its frame");
    }
    for (mlir::Block * predecessor : push.getHandler()->getPredecessors()) {
        if (!spend()) { return false; }
        auto * term = predecessor->getTerminator();
        if (term != push.getOperation() && !llvm::isa<ctjs::CheckOp>(term)) {
            return reject("native catch landing is reachable without throwing");
        }
    }
    return true;
}

bool recovery::partition(const tail & normal, const tail & caught) {
    // Stop at the original installation site. Nothing before it belongs to
    // the catch, including early returns and fallible Number/property calls.
    tail preceding;
    llvm::SmallVector<mlir::Block *> pending{&function.getBody().front()};
    while (!pending.empty()) {
        if (!spend()) { return false; }
        auto * block = pending.pop_back_val();
        if (block->getParent() != &function.getBody() || normal.active.contains(block) ||
            caught.active.contains(block)) {
            return reject("native exception prefix bypasses its handler installation");
        }
        if (!prefix.insert(block).second) { continue; }
        if (block != &function.getBody().front() && block->getNumArguments() != width) {
            return reject("native exception prefix lost its complete register vector");
        }
        preceding.blocks.push_back(block);
        for (mlir::Operation & operation : *block) {
            if (!spend()) { return false; }
            if (llvm::isa<ctjs::PopHandlerOp, ctjs::CatchLandOp>(operation)) {
                return reject("native exception prefix changes handler state");
            }
        }
        if (block == push->getBlock()) { continue; }
        auto * term = block->getTerminator();
        if (llvm::isa<ctjs::ReturnOp, ctjs::ThrowOp>(term)) { continue; }
        if (!llvm::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp,
                       ctjs::CheckOp>(term)) {
            return reject("native exception prefix has an unsupported control-flow exit");
        }
        auto branch = llvm::cast<mlir::BranchOpInterface>(term);
        for (auto [index, successor] : llvm::enumerate(term->getSuccessors())) {
            auto operands = branch.getSuccessorOperands(static_cast<unsigned>(index));
            if (!spend(uint64_t(1) + operands.size())) { return false; }
            if (operands.getProducedOperandCount() != 0 ||
                operands.size() != successor->getNumArguments()) {
                return reject("native exception prefix lost its complete edge register vector");
            }
            for (auto [value, argument] :
                 llvm::zip(operands.getForwardedOperands(), successor->getArguments())) {
                if (value.getType() != argument.getType()) {
                    return reject("native exception prefix has mismatched edge register types");
                }
            }
            preceding.edges[block].push_back(successor);
            pending.push_back(successor);
        }
    }
    if (!prefix.contains(push->getBlock()) || !acyclic(preceding)) {
        return reject("native exception handler is not reached by an acyclic prefix");
    }
    // The prefix and both tails exhaust every successor reachable from entry,
    // including check unwinds. Remaining blocks are unreachable, such as the
    // importer's pop/return epilogue after an unconditional return or throw.
    // They stay in the original snapshot but need no executable clone.
    return true;
}

bool recovery::inspectInvocation(ctjs::CheckOp check) {
    mlir::Operation * call = nullptr;
    for (mlir::Operation & operation : check->getBlock()->without_terminator()) {
        if (!spend()) { return false; }
        if (llvm::isa<ctjs::CallDirectOp, ctjs::CallOp>(operation)) {
            if (call) {
                return reject("native invocation recovery needs one call per status edge");
            }
            call = &operation;
        }
    }
    if (!call) { return true; }
    for (mlir::Operation & operation : check->getBlock()->without_terminator()) {
        if (&operation == call) { break; }
        if (!spend()) { return false; }
        if (!llvm::isa<ctjs::RootOp>(operation) && !mlir::isPure(&operation)) {
            return reject("native invocation recovery needs independently checked fallible "
                          "preparation before the call");
        }
    }
    // The importer checks a call before its result is moved into the
    // assignment target. Keep this boundary exact: even a pure operation
    // after the call belongs to the normal continuation, not its unwind.
    if (call->getNextNode() != check.getOperation() || !call->getResult(0).hasOneUse() ||
        call->getResult(0).use_begin()->getOwner() != check.getOperation() ||
        !llvm::equal(check.getHandlerOperands(), check->getBlock()->getArguments()) ||
        check.getContOperands().size() != width) {
        return reject("native invocation recovery needs an unpublished call result and its "
                      "complete pre-call register snapshot");
    }
    unsigned resultSlots = 0;
    for (auto [normal, saved] : llvm::zip(check.getContOperands(), check.getHandlerOperands())) {
        if (!spend()) { return false; }
        if (normal == call->getResult(0)) {
            ++resultSlots;
        } else if (normal != saved) {
            return reject("native invocation normal edge changes a non-result register");
        }
    }
    if (resultSlots != 1) {
        return reject("native invocation normal edge must publish one scratch result");
    }
    invocations.try_emplace(check.getOperation(), call);
    return true;
}

ExceptionInvocationSource inspectSingleInvocationRegion(ctjs::FuncOp function, unsigned maxSteps,
                                                        unsigned maxCalls) {
    recovery attempt{function, maxSteps, ExceptionRecoveryMode::CheckedInvocations};
    recovery::tail normal, caught;
    ExceptionInvocationSource result;
    if (attempt.inspect() && attempt.collect(normal, attempt.push.getBody(), true, false) &&
        attempt.collect(caught, attempt.push.getHandler(), false, true) &&
        attempt.partition(normal, caught)) {
        // Every checked call must sit on the one straight-line success path
        // from the handler installation; a call reachable any other way refuses.
        llvm::SmallVector<std::pair<mlir::Operation *, ctjs::CheckOp>> chain;
        llvm::DenseSet<mlir::Block *> walked;
        for (auto * block = attempt.push.getBody(); block && walked.insert(block).second;) {
            if (!attempt.spend()) { break; }
            auto * terminator = block->getTerminator();
            if (auto check = llvm::dyn_cast<ctjs::CheckOp>(terminator)) {
                if (auto found = attempt.invocations.find(check.getOperation());
                    found != attempt.invocations.end()) {
                    chain.emplace_back(found->second, check);
                }
                block = check.getCont();
            } else if (auto branch = llvm::dyn_cast<mlir::cf::BranchOp>(terminator)) {
                block = branch.getDest();
            } else {
                block = nullptr;
            }
        }
        if (maxCalls == 0 || attempt.throws != 0 || attempt.invocations.size() != 1) {
            if (maxCalls <= 1 || attempt.throws != 0 || attempt.invocations.empty() ||
                attempt.invocations.size() > maxCalls) {
                attempt.reject(
                    maxCalls <= 1
                        ? "native invocation source needs one checked call and no explicit throws"
                        : "native invocation source needs a bounded chain of checked calls and no "
                          "explicit throws");
            } else if (chain.size() != attempt.invocations.size()) {
                attempt.reject("native invocation chain must lie on one success path");
            }
        } else if (chain.empty()) {
            chain.emplace_back(attempt.invocations.begin()->second,
                               llvm::cast<ctjs::CheckOp>(attempt.invocations.begin()->first));
        }
        if (attempt.refusal.empty() && attempt.spend(uint64_t(attempt.prefix.size()) +
                                                     normal.blocks.size() + caught.blocks.size())) {
            result.push = attempt.push;
            result.landing = attempt.landing;
            result.frame = attempt.frame;
            result.call = chain.front().first;
            result.check = chain.front().second;
            result.chain = std::move(chain);
            llvm::append_range(result.prefix, attempt.prefix);
            result.normal = std::move(normal.blocks);
            result.caught = std::move(caught.blocks);
        }
    }
    result.steps = maxSteps - attempt.remaining;
    result.refusal = std::move(attempt.refusal);
    return result;
}

} // namespace ctcompile::ctnative::lowering_detail
