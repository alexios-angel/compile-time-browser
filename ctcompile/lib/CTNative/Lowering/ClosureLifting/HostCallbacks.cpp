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
// A listener may remove itself through the binding that holds it, as
// Bootstrap's executeAfterTransition does:
//
//   const handler = ({target}) => { ... el.removeEventListener(type, handler); };
//   el.addEventListener(type, handler);
//
// The binding only ever names the listener's own closure, so
// bindSelfRemovingListeners replaces the frame's reads of it with the closure,
// and the listener's capture of it with the closure's identity: the listener
// is then bound to its own ctnative::callable_identity, and removes the
// registration the frame added by it.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include <array>

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// The realm function a timer call names: setTimeout or setInterval.
llvm::StringRef timerName(ctjs::CallOp call) {
    return TypeInference::isRealmGlobal(call.getCallee().getDefiningOp(), "setInterval")
               ? "setInterval"
               : "setTimeout";
}

/// Whether `call` is `x.addEventListener(...)` or `x.removeEventListener(...)`
/// (as `method` names) with the method read only for the call.
bool isListenerCall(ctjs::CallOp call, llvm::StringRef method) {
    auto read = ctjs::receiverMethod(call);
    return read && read.getObject() == call.getReceiver() && read.getResult().hasOneUse() &&
           ctjs::methodKey(call) == method;
}

/// Whether every use of `value` names the listener of an addEventListener or
/// removeEventListener call (operand 3: the callee, the receiver, the type,
/// then the listener), counting the additions in `adds`. With `removalsOnly`,
/// an addition does not qualify.
bool onlyNamesListener(mlir::Value value, bool removalsOnly, unsigned & adds) {
    return llvm::all_of(value.getUses(), [&](mlir::OpOperand & use) {
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        if (!call || use.getOperandNumber() != 3) { return false; }
        if (isListenerCall(call, "removeEventListener")) { return true; }
        const bool add = !removalsOnly && isListenerCall(call, "addEventListener");
        adds += add ? 1 : 0;
        return add;
    });
}

} // namespace

/// Rewrites each listener that removes itself through its own binding into a
/// listener whose every use names it in an addEventListener or
/// removeEventListener call, the shape hostCallbackCensus takes. The binding
/// qualifies when:
///  - it is written once, with a closure made in its frame whose one use is
///    that write, and the write dominates every read of the binding there;
///  - each read in the frame only names the listener of such a call, and at
///    least one of those calls adds it;
///  - the closure itself is the one closure that captures the binding, from
///    its frame's operand, and its body reads that capture only to name the
///    listener of removeEventListener, never writes it and relays it to no
///    nested closure.
/// The frame's reads then become the closure and the write goes, which leaves
/// the binding a constant cell of `undefined` that only the closure captures:
/// a placeholder, in the slot kNativeListenerSelf names on the closure, in
/// which rewriteHostCallback passes the closure's identity. The binding holds
/// the closure from its write on, and the listener runs only after an
/// addition the write dominates, so each read named the closure that one
/// identity stands for.
void closureLifter::bindSelfRemovingListeners() {
    llvm::SmallVector<ctjs::CreateCellOp> cells;
    module.walk([&](ctjs::CreateCellOp cell) { cells.push_back(cell); });
    mlir::DominanceInfo order(module);
    for (ctjs::CreateCellOp cell : cells) {
        ctjs::CellSetOp write;
        ctjs::CreateClosureOp self;
        unsigned slot = 0;
        llvm::SmallVector<ctjs::CellGetOp> reads;
        bool shaped = true;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            switch (classify(use)) {
            case cellUse::write:
                shaped &= !write;
                write = llvm::cast<ctjs::CellSetOp>(use.getOwner());
                break;
            case cellUse::read: reads.push_back(llvm::cast<ctjs::CellGetOp>(use.getOwner())); break;
            case cellUse::capture:
                shaped &= !self;
                self = llvm::cast<ctjs::CreateClosureOp>(use.getOwner());
                slot = use.getOperandNumber() - kFirstCapture;
                break;
            default: shaped = false; break;
            }
        }
        if (!shaped || !write || !self || write.getValue() != self.getResult() ||
            !self.getResult().hasOneUse() || enclosingIndex(self, slot) >= 0) {
            continue;
        }
        auto frame = cell->getParentOfType<ctjs::FuncOp>();
        ctjs::FuncOp target = targetOf(self);
        if (!target || target.getBody().empty() || self->getParentOfType<ctjs::FuncOp>() != frame) {
            continue;
        }
        unsigned adds = 0;
        const bool framed = llvm::all_of(reads, [&](ctjs::CellGetOp read) {
            return read->getParentOfType<ctjs::FuncOp>() == frame &&
                   order.properlyDominates(write.getOperation(), read.getOperation()) &&
                   onlyNamesListener(read.getResult(), false, adds);
        });
        if (!framed || adds == 0) { continue; }
        // In the listener's body: reads of the binding name it only in a
        // removal, and nothing writes or relays the binding.
        const mlir::Value own = target.getBody().front().getArgument(ctjs::arg_callee);
        const auto fits = [&](mlir::Operation * op) {
            unsigned none = 0;
            if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
                return read.getClosure() != own || static_cast<unsigned>(read.getIndex()) != slot ||
                       onlyNamesListener(read.getResult(), true, none);
            }
            if (auto store = llvm::dyn_cast<ctjs::StoreUpvalueOp>(op)) {
                return static_cast<unsigned>(store.getIndex()) != slot;
            }
            if (auto nested = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
                for (unsigned i = 0; i < nested.getUpvalues().size(); ++i) {
                    if (enclosingIndex(nested, i) == static_cast<std::int32_t>(slot)) {
                        return false;
                    }
                }
            }
            return true;
        };
        if (target
                .walk([&](mlir::Operation * op) {
                    return fits(op) ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
                })
                .wasInterrupted()) {
            continue;
        }
        for (ctjs::CellGetOp read : reads) {
            read.getResult().replaceAllUsesWith(self.getResult());
            read.erase();
        }
        // Without its write the binding is a constant cell of `undefined`
        // that only the listener captures: a placeholder for the identity.
        write.erase();
        self->setAttr(kNativeListenerSelf,
                      mlir::Builder(context).getI32IntegerAttr(static_cast<std::int32_t>(slot)));
    }
}

/// Binds the calls ResolveGlobals marked with the closed top-level function
/// their listener names (ctjs.host_listener): the call keeps its load of the
/// binding as the listener's identity, which the lowering makes the
/// declaration's (ctnative::declared_identity), and names the function as a
/// lifted listener is named. A function the host cannot call as a listener
/// leaves the call refused with the reason (ctnative.host_refusal).
static void bindDeclaredListeners(mlir::ModuleOp module) {
    mlir::MLIRContext * context = module.getContext();
    module.walk([&](ctjs::CallOp call) {
        auto named = call->getAttrOfType<mlir::FlatSymbolRefAttr>(kHostListener);
        if (!named || call->hasAttr(kNativeHostCallback)) { return; }
        auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, named);
        const llvm::StringRef method = ctjs::methodKey(call);
        const std::string listener = "the listener of EventTarget.prototype." + method.str();
        std::optional<std::string> why;
        if (call.getArgs().size() != 2 && call.getArgs().size() != 3) {
            why = "EventTarget.prototype." + method.str() + " with " +
                  std::to_string(call.getArgs().size()) +
                  " arguments - only (type, listener, capture) is native";
        } else if (!target || target.getBody().empty()) {
            why = listener + " names no function this module defines";
        } else {
            mlir::Block & entry = target.getBody().front();
            if (!entry.getArgument(ctjs::arg_receiver).use_empty()) {
                why = listener + " reads `this`";
            } else if (!entry.getArgument(ctjs::arg_new_target).use_empty()) {
                why = listener + " reads new.target";
            } else if (ctjs::readsRawArguments(target)) {
                why = listener + " reads its `arguments`";
            } else if (const unsigned declared = ctjs::parameterCount(entry); declared > 1) {
                why = listener + " declares " + std::to_string(declared) +
                      " parameters - only a listener that takes the event alone is native";
            }
        }
        if (why) {
            call->setAttr("ctnative.host_refusal", mlir::StringAttr::get(context, *why));
            return;
        }
        const mlir::Value value = call.getArgs()[1];
        if (auto load = value.getDefiningOp()) { load->setAttr(kNativeHostDeclared, named); }
        llvm::SmallVector<mlir::Value> arguments{call.getArgs()[0], value};
        const bool capture = call.getArgs().size() == 3;
        if (capture) { arguments.push_back(call.getArgs()[2]); }
        for (const llvm::StringRef name : {"document", "window"}) {
            if (TypeInference::isRealmGlobal(call.getReceiver().getDefiningOp(), name)) {
                call->setAttr(kNativeHostTarget, mlir::StringAttr::get(context, name));
            }
        }
        call.getArgsMutable().assign(arguments);
        call->setAttr(kNativeHostCallback, named);
        call->setAttr(kNativeHostIdentified, mlir::UnitAttr::get(context));
        if (method == "removeEventListener") {
            call->setAttr(kNativeHostRemove, mlir::UnitAttr::get(context));
        }
        if (capture) { call->setAttr(kNativeHostCapture, mlir::UnitAttr::get(context)); }
        target->setAttr(kNativeHostCallback, mlir::UnitAttr::get(context));
        // Public for the reason rewriteArrayCallback gives: inference must
        // visit its body, typed from these calls.
        mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    });
}

void closureLifter::hostCallbackCensus() {
    hostCallbacks.clear();
    bindDeclaredListeners(module);
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
    const std::string method =
        timer ? timerName(calls.front()).str() : "EventTarget.prototype.addEventListener";
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
    llvm::SmallVector<mlir::Operation *> sites;
    for (ctjs::CallOp call : calls) { sites.push_back(call.getOperation()); }
    return whyLiftTailFails(c, target, sites);
}

void closureLifter::rewriteHostCallback(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                        liftReport & out) {
    const auto & calls = hostCallbacks.find(c.getOperation())->second;
    // A listener that removes itself receives its identity in the capture
    // slot that held its binding (bindSelfRemovingListeners).
    const auto self = c->getAttrOfType<mlir::IntegerAttr>(kNativeListenerSelf);
    // A listener that is removed, or added twice, needs the identity of its
    // closure creation: the closure value itself stays, and becomes that
    // identity in the lowering.
    const bool identified = self || calls.size() > 1 || llvm::any_of(calls, [](ctjs::CallOp call) {
                                return isListenerCall(call, "removeEventListener");
                            });
    if (identified) { c->setAttr(kNativeHostIdentity, mlir::UnitAttr::get(context)); }
    llvm::SmallVector<mlir::Value> captures = liftedCaptures(c);
    if (self) { captures[static_cast<unsigned>(self.getInt())] = c.getResult(); }
    // Respells one registration or removal with the listener's `identity`:
    // the closure in the frame that makes it, its identity parameter inside it.
    const auto respell = [&](ctjs::CallOp call, mlir::Value identity) {
        // The call passes the event type or the delay, the identity, the
        // captured values (none to a removal), then a listener's capture flag.
        const bool timer = TypeInference::isTimerCall(call);
        const bool remove = isListenerCall(call, "removeEventListener");
        const bool capture = !timer && call.getArgs().size() == 3;
        llvm::SmallVector<mlir::Value> arguments{call.getArgs()[timer ? 1 : 0]};
        if (identified) { arguments.push_back(identity); }
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
        if (timer) {
            call->setAttr(kNativeHostTimer, mlir::StringAttr::get(context, timerName(call)));
        }
        if (capture) { call->setAttr(kNativeHostCapture, mlir::UnitAttr::get(context)); }
        if (identified) { call->setAttr(kNativeHostIdentified, mlir::UnitAttr::get(context)); }
        if (remove) { call->setAttr(kNativeHostRemove, mlir::UnitAttr::get(context)); }
        ++out.calls;
    };
    for (ctjs::CallOp call : calls) { respell(call, c.getResult()); }
    if (self) {
        // lift() made the slot an entry argument; every use of it names the
        // listener of a removal (bindSelfRemovingListeners).
        const unsigned index = captureArgument(static_cast<unsigned>(self.getInt()));
        const mlir::Value identity = target.getBody().front().getArgument(index);
        target->setAttr(kNativeListenerSelf,
                        mlir::Builder(context).getI32IntegerAttr(static_cast<std::int32_t>(index)));
        llvm::SmallVector<ctjs::CallOp> removals;
        for (mlir::Operation * user : identity.getUsers()) {
            removals.push_back(llvm::cast<ctjs::CallOp>(user));
        }
        for (ctjs::CallOp call : removals) { respell(call, identity); }
    }
    target->setAttr(kNativeHostCallback, mlir::UnitAttr::get(context));
    // Public for the reason rewriteArrayCallback gives: no ctjs.call_direct
    // names the callback, and type inference must still visit its body.
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    markLifted(c, out, static_cast<unsigned>(captures.size()));
}

} // namespace ctcompile::ctnative::lowering_detail
