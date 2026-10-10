//===- ClosureLifting/Callbacks.cpp - Callback specialisation ---*- C++ -*-===//
//
// When every caller of a wrapper function passes the same capture-free
// closure for a parameter, calls through that parameter become direct calls.
// If the closure's identity never escapes, the parameter and the closures
// are removed.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

/// Specialises wrappers whose callers all pass one known capture-free closure
/// for a parameter. Refusals are recorded on the wrapper as
/// `ctnative.callback_refusal`.
void closureLifter::specializeCallbacks(liftReport & out) {
    llvm::SmallVector<ctjs::FuncOp> candidates;
    llvm::DenseSet<mlir::Operation *> seen;
    module.walk([&](ctjs::CallDirectOp call) {
        if (!llvm::any_of(call.getArgs(), [](mlir::Value argument) {
                return argument.getDefiningOp<ctjs::CreateClosureOp>() != nullptr;
            })) {
            return;
        }
        auto target = call.getTarget();
        if (target && seen.insert(target.getOperation()).second) { candidates.push_back(target); }
    });
    for (ctjs::FuncOp wrapper : candidates) {
        // Entry arguments 0-2 are the implicit receiver, new.target and
        // callee; a wrapper needs at least one declared parameter.
        if (wrapper.getBody().empty() || wrapper.getBody().front().getNumArguments() < 4) {
            continue;
        }
        const auto refuse = [&](llvm::StringRef reason) {
            wrapper->setAttr("ctnative.callback_refusal", mlir::StringAttr::get(context, reason));
        };
        if (ctjs::readsRawArguments(wrapper)) {
            refuse("the wrapper reads its raw argument window");
            continue;
        }
        if (passesNewTarget.contains(wrapper.getOperation())) {
            refuse("the wrapper passes new.target");
            continue;
        }
        if (whyOwnClosureEscapes(wrapper)) {
            refuse("the wrapper's own closure escapes");
            continue;
        }
        llvm::SmallVector<ctjs::CallDirectOp> callers;
        llvm::SmallVector<ctjs::CreateClosureOp> wrapperClosures;
        module.walk([&](ctjs::CallDirectOp call) {
            if (call.getCallee() == wrapper.getSymName()) { callers.push_back(call); }
        });
        module.walk([&](ctjs::CreateClosureOp closure) {
            if (targetOf(closure) == wrapper) { wrapperClosures.push_back(closure); }
        });
        bool closed = !callers.empty() && !wrapperClosures.empty();
        for (ctjs::CreateClosureOp closure : wrapperClosures) {
            for (mlir::OpOperand & use : closure.getResult().getUses()) {
                auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                closed &=
                    call && use.getOperandNumber() == 2 && call.getCallee() == wrapper.getSymName();
            }
        }
        for (ctjs::CallDirectOp call : callers) {
            auto closure = call.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>();
            closed &= closure && targetOf(closure) == wrapper;
        }
        if (!closed) {
            refuse("not every caller of the wrapper is a direct call of its closure");
            continue;
        }
        mlir::Block & entry = wrapper.getBody().front();
        if (llvm::any_of(callers, [&](ctjs::CallDirectOp call) {
                return call->getNumOperands() != entry.getNumArguments();
            })) {
            refuse("the wrapper call has a missing or surplus argument");
            continue;
        }
        if (llvm::any_of(callers, [&](ctjs::CallDirectOp call) {
                return !ctjs::isUndefinedConstant(call.getNewTarget());
            })) {
            refuse("the wrapper call has a non-undefined new.target");
            continue;
        }
        for (unsigned parameter = entry.getNumArguments(); parameter-- > 3;) {
            if (!llvm::any_of(callers, [&](ctjs::CallDirectOp call) {
                    return call->getOperand(parameter).getDefiningOp<ctjs::CreateClosureOp>() !=
                           nullptr;
                })) {
                continue;
            }
            ctjs::FuncOp callback;
            llvm::SmallVector<ctjs::CreateClosureOp> supplied;
            llvm::DenseSet<mlir::Operation *> suppliedSet;
            bool uniform = true;
            for (ctjs::CallDirectOp call : callers) {
                auto closure = call->getOperand(parameter).getDefiningOp<ctjs::CreateClosureOp>();
                ctjs::FuncOp target = closure ? targetOf(closure) : ctjs::FuncOp{};
                if (!target || !closure.getUpvalues().empty() || whyTargetIsNotLiftable(closure) ||
                    (callback && callback != target)) {
                    uniform = false;
                    break;
                }
                callback = target;
                if (suppliedSet.insert(closure.getOperation()).second) {
                    supplied.push_back(closure);
                }
            }
            if (!uniform) {
                refuse("callers do not supply one known capture-free callback target");
                continue;
            }
            const unsigned arity =
                callback.getBody().front().getNumArguments() - ctjs::implicit_arguments;
            const bool callbackReadsArguments = ctjs::readsRawArguments(callback);
            mlir::BlockArgument argument = entry.getArgument(parameter);
            llvm::SmallVector<closureCall> calls;
            bool callOnly = !argument.use_empty();
            for (mlir::OpOperand & use : argument.getUses()) {
                const auto call = callSiteOf(use, callback);
                auto direct = llvm::dyn_cast_if_present<ctjs::CallDirectOp>(call.op);
                if (!call || call.args.size() > arity ||
                    (call.args.size() < arity && callbackReadsArguments) ||
                    (direct && (call.args.size() != arity ||
                                !ctjs::isUndefinedConstant(direct.getNewTarget())))) {
                    callOnly = false;
                    continue;
                }
                calls.push_back(call);
            }
            bool removable = callOnly && !whyOwnClosureEscapes(callback);
            for (ctjs::CreateClosureOp closure : supplied) {
                for (mlir::OpOperand & use : closure.getResult().getUses()) {
                    auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                    removable &= call && use.getOperandNumber() == parameter &&
                                 call.getCallee() == wrapper.getSymName();
                }
            }
            module.walk([&](ctjs::CreateClosureOp closure) {
                if (targetOf(closure) == callback &&
                    !suppliedSet.contains(closure.getOperation())) {
                    removable = false;
                }
            });
            mlir::Value callee = argument;
            if (removable) {
                mlir::OpBuilder at(&entry, entry.begin());
                const mlir::Value undefined = undefinedAt(at, supplied.front().getLoc());
                mlir::Operation * moved = at.clone(*supplied.front().getOperation());
                moved->setOperand(0, entry.getArgument(2));
                moved->setOperand(1, undefined);
                callee = moved->getResult(0);
            }
            for (closureCall site : calls) {
                if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(site.op)) {
                    // Resolution already retained the exact symbol and all
                    // evaluated operands. Only the erased callback parameter
                    // needs a replacement value; its receiver, new.target,
                    // arguments and call attributes keep their original IR.
                    if (removable) { direct->setOperand(2, callee); }
                    continue;
                }
                auto call = llvm::cast<ctjs::CallOp>(site.op);
                mlir::OpBuilder at(call);
                auto direct = directCall(at, call.getLoc(), callback, call.getReceiver(), callee,
                                         undefinedAt(at, call.getLoc()), {}, call.getArgs(), arity);
                direct->setAttr("ctnative.callback", mlir::UnitAttr::get(context));
                call.getResult().replaceAllUsesWith(direct.getResult());
                call.erase();
                ++out.calls;
                ++out.callbackCalls;
            }
            if (!removable) {
                refuse("the callback value or its identity escapes the call-only parameter");
                continue;
            }
            for (ctjs::CallDirectOp call : callers) { call->eraseOperand(parameter); }
            llvm::BitVector removed(entry.getNumArguments());
            removed.set(parameter);
            if (mlir::failed(wrapper.eraseArguments(removed))) {
                llvm::report_fatal_error("ctnative: could not erase a proved callback parameter");
            }
            for (ctjs::CreateClosureOp closure : supplied) { closure.erase(); }
            ++out.callbackParameters;
        }
    }
}

} // namespace ctcompile::ctnative::lowering_detail
