//===- Admission/HostCallbacks.cpp - Listeners handed to the host ---------===//
//
// Admits an `el.addEventListener(type, f)` call closure lifting specialised for
// its listener (`ctnative.host_callback`): the receiver has to be a proved page
// element, the type a String, and each captured value one the listener can
// hold a copy of without changing what the program sees. The runtime's
// listener table (Runtime/Events.hpp) keeps that copy after the registering
// frame returns, so only values with no identity to share qualify: Numbers,
// Booleans, Strings, and element views, whose copies name the same node.
//
//===----------------------------------------------------------------------===//

#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

bool hostCallbackOperation(admission & self, ctjs::CallOp call) {
    if (!call.getResult().use_empty()) {
        return self.refuse("the result of addEventListener is read");
    }
    if (carrierOf(self.typeOf(call.getReceiver())) != carrier::domElement) {
        return self.refuse("addEventListener on " + printed(self.typeOf(call.getReceiver())) +
                           ", not a proved page element");
    }
    const mlir::Value type = call.getArgs().front();
    if (carrierOf(self.typeOf(type)) != carrier::string) {
        return self.refuse("addEventListener with an event type of " + printed(self.typeOf(type)) +
                           ", not a String");
    }
    for (mlir::Value capture : call.getArgs().drop_front()) {
        switch (carrierOf(self.typeOf(capture))) {
        case carrier::number:
        case carrier::boolean:
        case carrier::string:
        case carrier::domElement: break;
        default:
            return self.refuse("the listener of addEventListener captures " +
                               printed(self.typeOf(capture)) +
                               " - the host keeps a copy, which only a Number, a Boolean, a "
                               "String or an element shares");
        }
    }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
