// ClosureLifting/Callbacks.cpp - native lowering implementation.
#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

void closureLifter::specializeCallbacks(liftReport & out) {
    llvm::SmallVector<ctjs::FuncOp> candidates;
    llvm::DenseSet<mlir::Operation *> seen;
    module.walk([&](ctjs::CallDirectOp call) {
        if (!llvm::any_of(call.getArgs(), [](mlir::Value argument) {
                return argument.getDefiningOp<ctjs::CreateClosureOp>() != nullptr;
            })) {
            return;
        }
        auto target =
            mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
        if (target && seen.insert(target.getOperation()).second) { candidates.push_back(target); }
    });
    for (ctjs::FuncOp wrapper : candidates) {
        if (wrapper.getBody().empty() || wrapper.getBody().front().getNumArguments() < 4) {
            continue;
        }
        const auto refuse = [&](llvm::StringRef reason) {
            wrapper->setAttr("ctnative.callback_refusal", mlir::StringAttr::get(context, reason));
        };
        bool rawArguments = false;
        wrapper.walk([&](mlir::Operation * operation) {
            rawArguments |= llvm::isa<ctjs::MakeArgumentsOp, ctjs::GatherRestOp>(operation);
        });
        if (rawArguments) {
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
                return !isUndefinedConstant(call.getNewTarget());
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
            const unsigned arity = callback.getBody().front().getNumArguments() - 3;
            bool callbackReadsArguments = false;
            callback.walk([&](mlir::Operation * operation) {
                callbackReadsArguments |=
                    llvm::isa<ctjs::MakeArgumentsOp, ctjs::GatherRestOp>(operation);
            });
            mlir::BlockArgument argument = entry.getArgument(parameter);
            llvm::SmallVector<closureCall> calls;
            bool callOnly = !argument.use_empty();
            for (mlir::OpOperand & use : argument.getUses()) {
                const auto call = callSiteOf(use, callback);
                auto direct = llvm::dyn_cast_if_present<ctjs::CallDirectOp>(call.op);
                if (!call || call.args.size() > arity ||
                    (call.args.size() < arity && callbackReadsArguments) ||
                    (direct &&
                     (call.args.size() != arity || !isUndefinedConstant(direct.getNewTarget())))) {
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
                auto undefined = ctjs::ConstantOp::create(at, supplied.front().getLoc(),
                                                          ctjs::ValueType::get(context),
                                                          ctjs::UndefinedAttr::get(context));
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
                const auto valueType = ctjs::ValueType::get(context);
                auto undefined = ctjs::ConstantOp::create(at, call.getLoc(), valueType,
                                                          ctjs::UndefinedAttr::get(context));
                llvm::SmallVector<mlir::Value> arguments(call.getArgs());
                while (arguments.size() < arity) { arguments.push_back(undefined); }
                auto direct = ctjs::CallDirectOp::create(
                    at, call.getLoc(), valueType,
                    mlir::FlatSymbolRefAttr::get(callback.getSymNameAttr()), call.getReceiver(),
                    undefined, callee, arguments, nullptr, nullptr);
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
