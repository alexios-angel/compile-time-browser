//===- ClosureLifting/HostCallbacks.cpp - Callbacks the host keeps -*- C++ -*-===//
//
// Specialises a closure handed to the host to call later: the listener of
// `el.addEventListener(type, f)` or the handler of `setTimeout(f, delay)`. The
// closure's only use is that argument, so the call is its one call site, as
// for an Array-method callback (ArrayCallbacks.cpp): lift() makes its captures
// leading parameters, and the call then passes the event type or the delay,
// then the captured values. The lowering spells it as
// `ctnative::EventTarget.prototype.addEventListener.call(el, type, std::bind_front(f,
// captures...))` or `ctnative::setTimeout(std::bind_front(f, captures...), delay)`,
// and the runtime's listener table (Runtime/Events.hpp) or timer table
// (Runtime/Timers.hpp) owns that callable.
//
// The host keeps the callable after the frame that made it returns, so every
// capture is copied into it, except a binding of that frame written after the
// capture, which the frame and the callable share through a
// std::shared_ptr<ctnative::cell<T>> (rule 2b in ClosureLifter.h). Which values
// may be copied at all is a type question admission asks
// (Admission/HostCallbacks.cpp).
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include <array>

namespace ctcompile::ctnative::lowering_detail {

void closureLifter::hostCallbackCensus() {
    hostCallbacks.clear();
    for (ctjs::CreateClosureOp c : closures) {
        if (!c.getResult().hasOneUse()) { continue; }
        mlir::OpOperand & use = *c.getResult().getUses().begin();
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        if (!call) { continue; }
        // Operands are the callee, the receiver and the arguments; a timer's
        // handler is the first argument, a listener the second.
        if (use.getOperandNumber() == 2 && TypeInference::isTimerCall(call)) {
            hostCallbacks[c.getOperation()] = call;
            continue;
        }
        if (use.getOperandNumber() != 3) { continue; }
        auto read = ctjs::receiverMethod(call);
        if (!read || read.getObject() != call.getReceiver() || !read.getResult().hasOneUse() ||
            ctjs::methodKey(call) != "addEventListener") {
            continue;
        }
        hostCallbacks[c.getOperation()] = call;
    }
}

std::optional<std::string> closureLifter::whyNotHostCallback(ctjs::CreateClosureOp c) {
    ctjs::CallOp call = hostCallbacks.lookup(c.getOperation());
    const bool timer = TypeInference::isTimerCall(call);
    const std::string method = timer ? "setTimeout" : "EventTarget.prototype.addEventListener";
    if (call.getArgs().size() != 2) {
        return timer ? method + " with " + std::to_string(call.getArgs().size()) +
                           " arguments - only (handler, timeout) is native"
                     : method + " with options - only (type, listener) is native";
    }
    if (auto why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    const std::string callback = (timer ? "the handler of " : "the listener of ") + method;
    mlir::Block & entry = target.getBody().front();
    // A listener's `this` is the event's currentTarget and a handler's the
    // window, which nothing proves yet.
    if (!entry.getArgument(ctjs::arg_receiver).use_empty()) { return callback + " reads `this`"; }
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) {
        return callback + " reads new.target";
    }
    if (ctjs::readsRawArguments(target)) { return callback + " reads its `arguments`"; }
    // The runtime builds no Event object yet.
    if (const unsigned declared = ctjs::parameterCount(entry); declared != 0) {
        return callback + " declares " + std::to_string(declared) +
               (timer ? " parameters - only a handler that takes none is native"
                      : " parameters - only a listener that takes no event is native");
    }
    // The host keeps the listener after this frame returns, with its captures
    // copied into it; a binding of this frame written after the capture is
    // shared through a cell instead (rule 2b in ClosureLifter.h).
    for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
        if (slotIsCarried(c, i) && !slotIsShared(c, i)) {
            return callback + " captures binding " + std::to_string(i) +
                   ", which is written after it is captured and reaches it through an "
                   "enclosing closure - the host keeps the " +
                   (timer ? "handler" : "listener") +
                   ", and only a binding of the frame that registers it is shared";
        }
    }
    if (auto why = whyCapturesDoNotLift(c)) { return why; }
    return whyLiftTailFails(c, target, std::array<mlir::Operation *, 1>{call.getOperation()});
}

void closureLifter::rewriteHostCallback(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                        liftReport & out) {
    ctjs::CallOp call = hostCallbacks.lookup(c.getOperation());
    // The call passes the event type or the delay, then the captured values.
    const bool timer = TypeInference::isTimerCall(call);
    llvm::SmallVector<mlir::Value> arguments{call.getArgs()[timer ? 1 : 0]};
    llvm::append_range(arguments, liftedCaptures(c));
    const auto captured = static_cast<unsigned>(arguments.size() - 1);
    call.getArgsMutable().assign(arguments);
    call->setAttr(kNativeHostCallback, mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()));
    if (timer) { call->setAttr(kNativeHostTimer, mlir::UnitAttr::get(context)); }
    target->setAttr(kNativeHostCallback, mlir::UnitAttr::get(context));
    // Public for the reason rewriteArrayCallback gives: no ctjs.call_direct
    // names the callback, and type inference must still visit its body.
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    markLifted(c, out, captured);
    ++out.calls;
}

} // namespace ctcompile::ctnative::lowering_detail
