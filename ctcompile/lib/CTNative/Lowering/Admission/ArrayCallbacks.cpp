//===- Admission/ArrayCallbacks.cpp - Array-method callback calls ---------===//
//
// Admits a call closure lifting specialised for its Array-method callback
// (`ctnative.array_callback`): the receiver has to be an element array, and
// what the call answers has to have a native carrier the runtime's method
// returns. The runtime (Runtime/Array.hpp) requires map and find not to
// shorten the array; the element-vector proof gives that for free, since the
// callback reaches the array only as a read-only parameter and refuses any
// member of the array in another function.
//
//===----------------------------------------------------------------------===//

#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

bool arrayCallbackOperation(admission & self, ctjs::CallOp call) {
    const llvm::StringRef key = ctjs::methodKey(call);
    const std::string method = ("Array.prototype." + key).str();
    const mlir::Value receiver = call.getReceiver();
    const mlir::Type array = self.typeOf(receiver);
    if (!isElementArray(receiver, array)) {
        std::string why;
        if (llvm::isa_and_nonnull<VecType>(array) &&
            TypeInference::elementVectorAliases(receiver, &why).empty()) {
            return self.refuse("the receiver of " + method + " is an element array: " + why);
        }
        return self.refuse("the receiver of " + method + " is " + printed(array) +
                           ", not an array of Strings, Numbers or rows");
    }
    auto callback = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
        call, call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeArrayCallback));
    mlir::Type returned;
    callback.walk(
        [&](ctjs::ReturnOp ret) { returned = meet(returned, self.typeOf(ret.getValue())); });
    const mlir::Value result = call.getResult();
    if (key == "filter" || key == "some" || key == "every" || key == "find") {
        // The runtime tests the result with the carrier's explicit bool, which
        // is ToBoolean for these three.
        const carrier tested = carrierOf(returned);
        if (tested != carrier::boolean && tested != carrier::number && tested != carrier::string) {
            return self.refuse("the callback of " + method + " returns " + printed(returned) +
                               ", not a Boolean, a Number or a String");
        }
    }
    if (key == "filter" || key == "map") {
        std::string why;
        if (TypeInference::elementVectorAliases(result, &why).empty()) {
            return self.refuse("the result of " + method + " is " + why);
        }
        return isElementArray(result, self.typeOf(result)) ||
               self.refuse("the result of " + method + " is " + printed(self.typeOf(result)) +
                           ", not an array of Strings, Numbers or rows");
    }
    if (key == "reduce") {
        // The runtime's accumulator is the initial value's C++ type, and each
        // callback result is assigned to it.
        const mlir::Value initial = call.getArgs().back();
        const carrier held = carrierOf(self.typeOf(result));
        if (carrierOf(self.typeOf(initial)) != held || carrierOf(returned) != held) {
            return self.refuse("reduce's accumulator changes type: the initial value is " +
                               printed(self.typeOf(initial)) + " and the callback returns " +
                               printed(returned));
        }
    }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
