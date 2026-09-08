// recovery - inspection: the one-entry-handler shape the preserved importer
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
        push->getBlock() != &function.getBody().front() || frame->getBlock() != push->getBlock() ||
        push.getHandler() != landing->getBlock() || push.getBody() == push.getHandler() ||
        !landing.getPad().use_empty()) {
        return reject(
            "native exception recovery needs one entry handler and dedicated catch landing");
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

bool recovery::inspectInvocation(ctjs::CheckOp check) {
    ctjs::CallDirectOp call;
    for (mlir::Operation & operation : check->getBlock()->without_terminator()) {
        if (!spend()) { return false; }
        if (auto found = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
            if (call) {
                return reject("native invocation recovery needs one call per status edge");
            }
            call = found;
        }
    }
    if (!call) { return true; }
    for (mlir::Operation & operation : check->getBlock()->without_terminator()) {
        if (&operation == call.getOperation()) { break; }
        if (!spend()) { return false; }
        if (!llvm::isa<ctjs::RootOp>(operation) && !mlir::isPure(&operation)) {
            return reject("native invocation recovery needs independently checked fallible "
                          "preparation before the call");
        }
    }
    // The importer checks a call before its result is moved into the
    // assignment target. Keep this boundary exact: even a pure operation
    // after the call belongs to the normal continuation, not its unwind.
    if (call->getNextNode() != check.getOperation() || !call.getResult().hasOneUse() ||
        call.getResult().use_begin()->getOwner() != check.getOperation() ||
        !llvm::equal(check.getHandlerOperands(), check->getBlock()->getArguments()) ||
        check.getContOperands().size() != width) {
        return reject("native invocation recovery needs an unpublished call result and its "
                      "complete pre-call register snapshot");
    }
    unsigned resultSlots = 0;
    for (auto [normal, saved] : llvm::zip(check.getContOperands(), check.getHandlerOperands())) {
        if (!spend()) { return false; }
        if (normal == call.getResult()) {
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

} // namespace ctcompile::ctnative::lowering_detail
