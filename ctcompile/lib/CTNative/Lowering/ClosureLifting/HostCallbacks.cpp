//===- ClosureLifting/HostCallbacks.cpp - Callbacks the host keeps -*- C++ -*-===//
//
// Specialises a closure handed to the host to call later: the listener of
// `el.addEventListener(type, f)`. The closure's only use is that argument, so
// the call is its one call site, as for an Array-method callback
// (ArrayCallbacks.cpp): lift() makes its captures leading parameters, and the
// call then passes the captured values in place of the closure. The lowering
// spells it as
// `ctnative::EventTarget.prototype.addEventListener.call(el, type, std::bind_front(f,
// captures...))`, and the runtime's listener table (Runtime/Events.hpp) owns that callable.
//
// The host keeps the callable after the frame that made it returns, so every
// capture is copied into it: a binding written after the capture would need
// the variable itself, which no longer exists, and is refused. Which values
// may be copied at all is a type question admission asks
// (Admission/HostCallbacks.cpp).
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

#include <array>

namespace ctcompile::ctnative::lowering_detail {

void closureLifter::hostCallbackCensus() {
    hostCallbacks.clear();
    for (ctjs::CreateClosureOp c : closures) {
        if (!c.getResult().hasOneUse()) { continue; }
        mlir::OpOperand & use = *c.getResult().getUses().begin();
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        // Operands are the callee, the receiver and the arguments; the
        // listener is the second argument.
        if (!call || use.getOperandNumber() != 3) { continue; }
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
    const std::string method = "EventTarget.prototype.addEventListener";
    if (call.getArgs().size() != 2) {
        return method + " with options - only (type, listener) is native";
    }
    if (auto why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    // A listener's `this` is the event's currentTarget, which nothing proves
    // yet.
    if (!entry.getArgument(ctjs::arg_receiver).use_empty()) {
        return "the listener of " + method + " reads `this`";
    }
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) {
        return "the listener of " + method + " reads new.target";
    }
    if (ctjs::readsRawArguments(target)) {
        return "the listener of " + method + " reads its `arguments`";
    }
    // The runtime builds no Event object yet.
    if (const unsigned declared = ctjs::parameterCount(entry); declared != 0) {
        return "the listener of " + method + " declares " + std::to_string(declared) +
               " parameters - only a listener that takes no event is native";
    }
    // The host keeps the listener after this frame returns, with its captures
    // copied into it.
    for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
        if (slotIsCarried(c, i)) {
            return "the listener of " + method + " captures binding " + std::to_string(i) +
                   ", which is written after it is captured - the host keeps the listener, "
                   "so it takes its captures by value";
        }
    }
    if (auto why = whyCapturesDoNotLift(c)) { return why; }
    return whyLiftTailFails(c, target, std::array<mlir::Operation *, 1>{call.getOperation()});
}

void closureLifter::rewriteHostCallback(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                        liftReport & out) {
    ctjs::CallOp call = hostCallbacks.lookup(c.getOperation());
    // The call passes the event type, then the captured values where it
    // passed the closure.
    llvm::SmallVector<mlir::Value> arguments{call.getArgs().front()};
    llvm::append_range(arguments, liftedCaptures(c));
    const auto captured = static_cast<unsigned>(arguments.size() - 1);
    call.getArgsMutable().assign(arguments);
    call->setAttr(kNativeHostCallback, mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()));
    target->setAttr(kNativeHostCallback, mlir::UnitAttr::get(context));
    // Public for the reason rewriteArrayCallback gives: no ctjs.call_direct
    // names the listener, and type inference must still visit its body.
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    markLifted(c, out, captured);
    ++out.calls;
}

} // namespace ctcompile::ctnative::lowering_detail
