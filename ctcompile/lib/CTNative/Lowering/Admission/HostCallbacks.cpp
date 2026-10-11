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
//===----------------------------------------------------------------------===//

#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

bool hostCallbackOperation(admission & self, ctjs::CallOp call) {
    const bool timer = TypeInference::isTimerCall(call);
    const llvm::StringRef method = timer ? "setTimeout" : "addEventListener";
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
        if (carrierOf(self.typeOf(call.getReceiver())) != carrier::domElement) {
            return self.refuse("addEventListener on " + printed(self.typeOf(call.getReceiver())) +
                               ", not a proved page element");
        }
        if (carrierOf(self.typeOf(first)) != carrier::string) {
            return self.refuse("addEventListener with an event type of " +
                               printed(self.typeOf(first)) + ", not a String");
        }
    }
    for (mlir::Value capture : call.getArgs().drop_front()) {
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

} // namespace ctcompile::ctnative::lowering_detail
