// recovery - the transaction: run() proves and rebuilds on the clone, and the
// entry point adopts the recovered body or leaves the original untouched.
//
// One of six files carved out of a 1,251-line Exceptions/Recovery.cpp on
// 2026-09-08. All are member functions of `recovery`, the disposable-clone
// transaction declared in Attempt.h beside this; Exceptions/Recovery.h, the
// header the rest of the lowering sees, did not change.

#include "Attempt.h"

namespace ctcompile::ctnative::lowering_detail {

bool recovery::run() {
    if (!inspect()) { return false; }
    tail normal, caught;
    if (!collect(normal, push.getBody(), true, false) ||
        !collect(caught, push.getHandler(), false, true)) {
        return false;
    }
    if (throws == 0 && invocations.empty()) {
        return reject("native try/catch needs an explicit throw in its active handler");
    }
    if (mode == ExceptionRecoveryMode::EffectCheckedInvocations && !proveEffects(normal, caught)) {
        return false;
    }
    mlir::OpBuilder builder(push);
    mlir::OperationState state(push.getLoc(), "ctjs.try");
    state.addTypes(ctjs::ValueType::get(function.getContext()));
    state.addRegion();
    state.addRegion();
    auto * guarded = builder.create(state);
    if (!cloneTail(normal, guarded->getRegion(0), false) ||
        !cloneTail(caught, guarded->getRegion(1), true) || !structure(guarded->getRegion(0)) ||
        !structure(guarded->getRegion(1))) {
        return false;
    }
    auto & body = guarded->getRegion(0).front();
    auto & handler = guarded->getRegion(1).front();
    auto * exit = body.getTerminator();
    if (exit->getName().getStringRef() != "ctjs.try_exit" ||
        handler.getTerminator()->getName().getStringRef() != "ctjs.try_yield" ||
        exit->getNumOperands() != handler.getNumArguments() + 2) {
        return reject("native exception completion lost its catch-state correspondence");
    }
    // Payload stays first even when unused. Only live original registers
    // require mutable storage spanning the native try and catch regions.
    for (unsigned index = handler.getNumArguments(); index-- > 1;) {
        if (!spend()) { return false; }
        if (!handler.getArgument(index).use_empty()) { continue; }
        exit->eraseOperand(index + 2);
        handler.eraseArgument(index);
    }
    if (!normalizeIndexSwitches(guarded) || !trimUnusedIfResults(guarded)) { return false; }
    push.erase();
    builder.setInsertionPointToEnd(&function.getBody().front());
    ctjs::FrameExitOp::create(builder, guarded->getLoc(), frame.getResult());
    ctjs::ReturnOp::create(builder, guarded->getLoc(), guarded->getResult(0));
    llvm::SmallVector<mlir::Block *> old;
    for (mlir::Block & block : llvm::drop_begin(function.getBody())) { old.push_back(&block); }
    for (mlir::Block * block : old) { block->dropAllReferences(); }
    for (mlir::Block * block : old) { block->erase(); }
    function->removeAttr("ctjs.not_structured");
    return true;
}

ExceptionRecoveryResult recoverPrimitiveExceptionRegion(ctjs::FuncOp function, unsigned maxSteps,
                                                        ExceptionRecoveryMode mode) {
    // Bound the source scan and reserve another scan's cost for the initial
    // clone before allocating it. The caller selects handler-containing
    // functions; no rewrite is visible until every stage succeeds.
    recovery attempt{function, maxSteps, mode};
    if (!attempt.inspect() || !attempt.spend(maxSteps - attempt.remaining)) {
        return {false, std::move(attempt.refusal), {}, maxSteps - attempt.remaining};
    }
    function.getContext()->getOrLoadDialect<mlir::arith::ArithDialect>();
    function.getContext()->getOrLoadDialect<mlir::ub::UBDialect>();
    function.getContext()->getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    if (mode == ExceptionRecoveryMode::EffectCheckedInvocations) {
        function.getContext()->getOrLoadDialect<CTNativeDialect>();
    }
    mlir::OwningOpRef<ctjs::FuncOp> scratch(
        llvm::cast<ctjs::FuncOp>(function->clone(attempt.copies)));
    attempt.function = *scratch;
    if (!attempt.run()) {
        return {false, std::move(attempt.refusal), {}, maxSteps - attempt.remaining};
    }
    mlir::Region original;
    original.takeBody(function.getBody());
    function.getBody().takeBody(scratch->getBody());
    scratch->getBody().takeBody(original);
    // run() only changed this diagnostic attribute. The returned snapshot
    // must retain the original attributes as well as the original body.
    (*scratch)->setAttrs(function->getAttrs());
    function->removeAttr("ctjs.not_structured");
    return {true, {}, std::move(scratch), maxSteps - attempt.remaining};
}

} // namespace ctcompile::ctnative::lowering_detail
