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
// capture is copied into it: a binding written after the capture would need
// the variable itself, which no longer exists, and is refused. Which values
// may be copied at all is a type question admission asks
// (Admission/HostCallbacks.cpp).
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include <array>

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// Whether `call` is `x.addEventListener(...)` or `x.removeEventListener(...)`
/// (as `method` names) with the method read only for the call.
bool isListenerCall(ctjs::CallOp call, llvm::StringRef method) {
    auto read = ctjs::receiverMethod(call);
    return read && read.getObject() == call.getReceiver() && read.getResult().hasOneUse() &&
           ctjs::methodKey(call) == method;
}

} // namespace

void closureLifter::hostCallbackCensus() {
    hostCallbacks.clear();
    for (ctjs::CreateClosureOp c : closures) {
        if (c.getResult().use_empty()) { continue; }
        // Operands are the callee, the receiver and the arguments; a timer's
        // handler is the first argument, a listener the second.
        if (mlir::OpOperand & first = *c.getResult().getUses().begin();
            c.getResult().hasOneUse() && first.getOperandNumber() == 2) {
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(first.getOwner());
                call && TypeInference::isTimerCall(call)) {
                hostCallbacks[c.getOperation()].push_back(call);
            }
            continue;
        }
        llvm::SmallVector<ctjs::CallOp, 1> calls;
        bool adds = false;
        bool all = true;
        for (mlir::OpOperand & use : c.getResult().getUses()) {
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            const bool add = call && isListenerCall(call, "addEventListener");
            all = all && use.getOperandNumber() == 3 &&
                  (add || (call && isListenerCall(call, "removeEventListener"))) &&
                  !llvm::is_contained(calls, call);
            if (!all) { break; }
            adds = adds || add;
            calls.push_back(call);
        }
        if (all && adds) { hostCallbacks[c.getOperation()] = std::move(calls); }
    }
}

std::optional<std::string> closureLifter::whyNotHostCallback(ctjs::CreateClosureOp c) {
    const auto & calls = hostCallbacks.find(c.getOperation())->second;
    const bool timer = TypeInference::isTimerCall(calls.front());
    const std::string method = timer ? "setTimeout" : "EventTarget.prototype.addEventListener";
    // A listener may be given a capture flag; admission checks that it is a
    // Boolean, not an options object.
    for (ctjs::CallOp call : calls) {
        if (call.getArgs().size() != 2 && (timer || call.getArgs().size() != 3)) {
            const std::string name =
                timer ? method : "EventTarget.prototype." + ctjs::methodKey(call).str();
            return timer ? name + " with " + std::to_string(call.getArgs().size()) +
                               " arguments - only (handler, timeout) is native"
                         : name + " with " + std::to_string(call.getArgs().size()) +
                               " arguments - only (type, listener, capture) is native";
        }
    }
    if (auto why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    const std::string callback = (timer ? "the handler of " : "the listener of ") + method;
    mlir::Block & entry = target.getBody().front();
    // A listener's `this` is the event's currentTarget and a handler's the
    // window, which nothing proves yet (a listener reads e.currentTarget).
    if (!entry.getArgument(ctjs::arg_receiver).use_empty()) { return callback + " reads `this`"; }
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) {
        return callback + " reads new.target";
    }
    if (ctjs::readsRawArguments(target)) { return callback + " reads its `arguments`"; }
    // A listener is called with the Event (Runtime/Events.hpp's js_event); a
    // handler with nothing, since extra setTimeout arguments are refused.
    if (const unsigned declared = ctjs::parameterCount(entry); declared > (timer ? 0u : 1u)) {
        return callback + " declares " + std::to_string(declared) +
               (timer ? " parameters - only a handler that takes none is native"
                      : " parameters - only a listener that takes the event alone is native");
    }
    // The host keeps the listener after this frame returns, with its captures
    // copied into it.
    for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
        if (slotIsCarried(c, i)) {
            return callback + " captures binding " + std::to_string(i) +
                   ", which is written after it is captured - the host keeps the " +
                   (timer ? "handler" : "listener") + ", so it takes its captures by value";
        }
    }
    if (auto why = whyCapturesDoNotLift(c)) { return why; }
    llvm::SmallVector<mlir::Operation *> sites;
    for (ctjs::CallOp call : calls) { sites.push_back(call.getOperation()); }
    return whyLiftTailFails(c, target, sites);
}

void closureLifter::rewriteHostCallback(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                        liftReport & out) {
    const auto & calls = hostCallbacks.find(c.getOperation())->second;
    // A listener that is removed, or added twice, needs the identity of its
    // closure creation: the closure value itself stays, and becomes that
    // identity in the lowering.
    const bool identified = calls.size() > 1 || llvm::any_of(calls, [](ctjs::CallOp call) {
                                return isListenerCall(call, "removeEventListener");
                            });
    if (identified) { c->setAttr(kNativeHostIdentity, mlir::UnitAttr::get(context)); }
    const llvm::SmallVector<mlir::Value> captures = liftedCaptures(c);
    for (ctjs::CallOp call : calls) {
        // The call passes the event type or the delay, the identity, the
        // captured values (none to a removal), then a listener's capture flag.
        const bool timer = TypeInference::isTimerCall(call);
        const bool remove = isListenerCall(call, "removeEventListener");
        const bool capture = !timer && call.getArgs().size() == 3;
        llvm::SmallVector<mlir::Value> arguments{call.getArgs()[timer ? 1 : 0]};
        if (identified) { arguments.push_back(c.getResult()); }
        if (!remove) { llvm::append_range(arguments, captures); }
        if (capture) { arguments.push_back(call.getArgs()[2]); }
        // The receiver's name, for the realm's own document and window.
        for (const llvm::StringRef name : {"document", "window"}) {
            if (!timer && TypeInference::isRealmGlobal(call.getReceiver().getDefiningOp(), name)) {
                call->setAttr(kNativeHostTarget, mlir::StringAttr::get(context, name));
            }
        }
        call.getArgsMutable().assign(arguments);
        call->setAttr(kNativeHostCallback, mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()));
        if (timer) { call->setAttr(kNativeHostTimer, mlir::UnitAttr::get(context)); }
        if (capture) { call->setAttr(kNativeHostCapture, mlir::UnitAttr::get(context)); }
        if (identified) { call->setAttr(kNativeHostIdentified, mlir::UnitAttr::get(context)); }
        if (remove) { call->setAttr(kNativeHostRemove, mlir::UnitAttr::get(context)); }
        ++out.calls;
    }
    target->setAttr(kNativeHostCallback, mlir::UnitAttr::get(context));
    // Public for the reason rewriteArrayCallback gives: no ctjs.call_direct
    // names the callback, and type inference must still visit its body.
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    markLifted(c, out, static_cast<unsigned>(captures.size()));
}

} // namespace ctcompile::ctnative::lowering_detail
