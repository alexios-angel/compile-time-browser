//===- Admission/HostCallbacks.cpp - Callbacks handed to the host ---------===//
//
// Admits an `el.addEventListener(type, f)` or `setTimeout(f, delay)` call
// closure lifting specialised for its callback (`ctnative.host_callback`). A
// listener's receiver has to be a proved page element and its type a String;
// a timer's delay has to be a Number. Each captured value has to be one the
// callback can hold a copy of without changing what the program sees. The
// runtime's listener table (Runtime/Events.hpp) or timer table
// (Runtime/Timers.hpp) keeps that copy after the registering frame returns, so
// only values with no identity to share qualify: Numbers, Booleans, Strings,
// and element views, whose copies name the same node.
//
// An Event (`!ctnative.event`), a listener's or one `new Event` made, is
// admitted only as the receiver of its typed reads and calls
// (TypeInference::hostCallbackType) and as dispatchEvent's argument: it never
// leaves the function that has it, as the VM's object could.
//
//===----------------------------------------------------------------------===//

#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

bool hostCallbackOperation(admission & self, ctjs::CallOp call) {
    const bool timer = TypeInference::isTimerCall(call);
    const llvm::StringRef method = timer                              ? "setTimeout"
                                   : call->hasAttr(kNativeHostRemove) ? "removeEventListener"
                                                                      : "addEventListener";
    // ponytail: nothing reads a timer's handle until clearTimeout is native.
    if (!call.getResult().use_empty()) {
        return self.refuse("the result of " + method.str() + " is read");
    }
    const mlir::Value first = call.getArgs().front();
    if (timer) {
        if (carrierOf(self.typeOf(first)) != carrier::number) {
            return self.refuse("setTimeout with a timeout of " + printed(self.typeOf(first)) +
                               ", not a Number");
        }
    } else {
        // The realm's own document and window (ctnative.host_target), or an
        // element.
        if (!call->hasAttr(kNativeHostTarget) &&
            carrierOf(self.typeOf(call.getReceiver())) != carrier::domElement) {
            return self.refuse(method.str() + " on " + printed(self.typeOf(call.getReceiver())) +
                               ", not a proved page element, document or window");
        }
        if (carrierOf(self.typeOf(first)) != carrier::string) {
            return self.refuse(method.str() + " with an event type of " +
                               printed(self.typeOf(first)) + ", not a String");
        }
    }
    auto captures = call.getArgs().drop_front();
    // The listener's identity, which the lowering makes from its closure.
    if (call->hasAttr(kNativeHostIdentified)) { captures = captures.drop_front(); }
    if (call->hasAttr(kNativeHostCapture)) {
        const mlir::Value flag = captures.back();
        if (carrierOf(self.typeOf(flag)) != carrier::boolean) {
            return self.refuse(method.str() + " with options of " + printed(self.typeOf(flag)) +
                               " - only a Boolean capture flag is native");
        }
        captures = captures.drop_back();
    }
    for (mlir::Value capture : captures) {
        switch (carrierOf(self.typeOf(capture))) {
        case carrier::number:
        case carrier::boolean:
        case carrier::string:
        case carrier::domElement: break;
        default:
            return self.refuse("the " + std::string{timer ? "handler" : "listener"} + " of " +
                               method.str() + " captures " + printed(self.typeOf(capture)) +
                               " - the host keeps a copy, which only a Number, a Boolean, a "
                               "String or an element shares");
        }
    }
    return true;
}

namespace {

/// Whether the listener `op` is in was registered on the document or the
/// window, whose currentTarget is not an element.
bool registeredOnDocumentOrWindow(mlir::Operation * op) {
    auto listener = op->getParentOfType<ctjs::FuncOp>();
    bool found = false;
    op->getParentOfType<mlir::ModuleOp>().walk([&](ctjs::CallOp call) {
        auto named = call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeHostCallback);
        found |=
            named && named.getValue() == listener.getSymName() && call->hasAttr(kNativeHostTarget);
    });
    return found;
}

} // namespace

std::optional<bool> eventOperation(admission & self, mlir::Operation * o) {
    const auto event = [&](mlir::Value v) {
        return carrierOf(self.typeOf(v)) == carrier::domEvent;
    };
    // new Event(type, {bubbles, cancelable}): the literal lowers into the
    // construct's event_init, so its fields are Booleans and it has no other
    // use (TypeInference::eventInitFields).
    if (TypeInference::isEventInitPart(o)) {
        auto define = llvm::dyn_cast<ctjs::SetPropertyOp>(o);
        if (!define || carrierOf(self.typeOf(define.getValue())) == carrier::boolean) {
            return true;
        }
        return self.refuse(("new Event with `" + ctjs::constantKey(define.getKey()) + "` of " +
                            printed(self.typeOf(define.getValue())) + ", not a Boolean")
                               .str());
    }
    if (TypeInference::isEventConstruct(o)) {
        auto construct = llvm::cast<ctjs::ConstructOp>(o);
        if (carrierOf(self.typeOf(construct.getArgs()[0])) != carrier::string) {
            return self.refuse("new Event with a type of " +
                               printed(self.typeOf(construct.getArgs()[0])) + ", not a String");
        }
        return event(construct.getResult()) || self.refuse("new Event lost its Event type");
    }
    // A `new Event` whose init is anything else, refused at the construct or,
    // when the init is a literal that would be refused first, at the literal.
    const auto refusedNewEvent = [](mlir::Operation * user) {
        auto construct = llvm::dyn_cast<ctjs::ConstructOp>(user);
        return construct &&
               TypeInference::isRealmGlobal(construct.getCallee().getDefiningOp(), "Event");
    };
    if (refusedNewEvent(o) || (llvm::isa<ctjs::CreateObjectOp>(o) &&
                               llvm::any_of(o->getResult(0).getUsers(), refusedNewEvent))) {
        return self.refuse("new Event with an init that is not an object literal of `bubbles` "
                           "and `cancelable` - only that EventInit is native");
    }
    if (TypeInference::isDispatchEventCall(o)) {
        auto call = llvm::cast<ctjs::CallOp>(o);
        if (carrierOf(self.typeOf(call.getReceiver())) != carrier::domElement) {
            return self.refuse("dispatchEvent on " + printed(self.typeOf(call.getReceiver())) +
                               ", not a proved page element");
        }
        return event(call.getArgs()[0]) ||
               self.refuse("dispatchEvent of " + printed(self.typeOf(call.getArgs()[0])) +
                           ", not an Event made with new Event");
    }
    if (TypeInference::isEventCall(o)) {
        auto call = llvm::cast<ctjs::CallOp>(o);
        if (!event(call.getReceiver())) {
            return self.refuse(ctjs::methodKey(call).str() +
                               " on a value that is not a listener's Event");
        }
        return call.getResult().use_empty() ||
               self.refuse("the result of " + ctjs::methodKey(call).str() + " is read");
    }
    if (llvm::none_of(o->getOperands(), event)) { return std::nullopt; }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(o); read && event(read.getObject())) {
        const llvm::StringRef key = ctjs::constantKey(read.getKey());
        if (key == "currentTarget" && registeredOnDocumentOrWindow(read)) {
            return self.refuse("currentTarget of a listener on the document or the window - "
                               "only an element's listener reads it natively");
        }
        const carrier result = carrierOf(self.typeOf(read.getResult()));
        if ((key == "type" && result == carrier::string) ||
            ((key == "target" || key == "currentTarget") && result == carrier::domElement) ||
            (key == "defaultPrevented" && result == carrier::boolean)) {
            return true;
        }
        return self.refuse(("an Event read through `" + key +
                            "` - only type, target, currentTarget and defaultPrevented are native")
                               .str());
    }
    return self.refuse("an Event used other than through its reads, its preventDefault and "
                       "stopPropagation calls and dispatchEvent - it does not leave its function");
}

} // namespace ctcompile::ctnative::lowering_detail
