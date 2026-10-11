//===- Admission/StringCallbacks.cpp - replace with a function ------------===//
//
// Admits `text.replace(/pattern/g, fn)` once closure lifting specialised the
// callback (kNativeStringCallback): the receiver has to be a String, the
// pattern one the native runtime runs as JavaScript does (Runtime/RegExp.hpp),
// and the callback has to return a String or a Number, which the runtime
// converts with ToString. The method read and the RegExp literal the call
// consumes lower to nothing. `text.toLowerCase()` and `text.trim()` need a
// String receiver, and `text.replace(/re/, s)` a String receiver, a pattern
// as above and a String replacement.
//
//===----------------------------------------------------------------------===//

#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

std::optional<bool> stringCallbackOperation(admission & self, mlir::Operation * o) {
    if (TypeInference::consumedByStringCallback(o)) { return true; }
    if (TypeInference::isStringMethodCall(o)) {
        auto call = llvm::cast<ctjs::CallOp>(o);
        const mlir::Type receiver = self.typeOf(call.getReceiver());
        const llvm::StringRef name = o->getAttrOfType<mlir::StringAttr>(kNativeStringMethod);
        if (carrierOf(receiver) != carrier::string) {
            return self.refuse(("the receiver of String.prototype." + name + " is ").str() +
                               printed(receiver) + ", not a String");
        }
        if (name != "replace") { return true; }
        // replace with a RegExp literal the runtime runs as JavaScript does,
        // and a String replacement (its `$` patterns are GetSubstitution's).
        const mlir::Type replacement = self.typeOf(call.getArgs()[1]);
        if (carrierOf(replacement) != carrier::string) {
            return self.refuse("the replacement of String.prototype.replace is " +
                               printed(replacement) + ", not a String or a function");
        }
        const auto literal = TypeInference::regExpLiteral(call.getArgs()[0]);
        if (!literal) { return self.refuse("a String.prototype.replace call that lost its RegExp"); }
        const std::string why =
            TypeInference::replacePatternProblem(literal->first, literal->second, 0);
        return why.empty() || self.refuse("String.prototype.replace " + why);
    }
    if (!TypeInference::isStringCallbackCall(o)) { return std::nullopt; }
    auto call = llvm::cast<ctjs::CallOp>(o);
    const mlir::Type receiver = self.typeOf(call.getReceiver());
    if (carrierOf(receiver) != carrier::string) {
        return self.refuse("the receiver of String.prototype.replace is " + printed(receiver) +
                           ", not a String");
    }
    auto callback = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
        call, call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeStringCallback));
    const auto literal = TypeInference::regExpLiteral(call.getArgs()[0]);
    if (!callback || !literal) {
        return self.refuse("a String.prototype.replace call that lost its callback or RegExp");
    }
    const auto captures = static_cast<unsigned>(call.getArgs().size()) - 1;
    const unsigned declared = ctjs::parameterCount(callback.getBody().front()) - captures;
    if (const std::string why =
            TypeInference::replacePatternProblem(literal->first, literal->second, declared);
        !why.empty()) {
        return self.refuse("String.prototype.replace " + why);
    }
    mlir::Type returned;
    callback.walk(
        [&](ctjs::ReturnOp ret) { returned = meet(returned, self.typeOf(ret.getValue())); });
    const carrier made = carrierOf(returned);
    if (made != carrier::string && made != carrier::number) {
        return self.refuse("the callback of String.prototype.replace returns " + printed(returned) +
                           ", not a String or a Number");
    }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
