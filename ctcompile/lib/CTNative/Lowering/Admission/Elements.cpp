//===- Admission/Elements.cpp - An ordinary script's element members ------===//
//
// Admits the element calls and reads TypeInference::elementMemberType types
// (Inference/Elements.cpp) when every operand has the carrier the runtime's
// js_element_t member takes (Runtime/Browser.hpp): an element, or an element
// that may be null, as the receiver, and Strings for selectors, attribute
// names and attribute values. A member of null throws the VM's TypeError in
// both engines.
//
//===----------------------------------------------------------------------===//

#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

std::optional<bool> elementOperation(admission & self, mlir::Operation * o) {
    const auto element = [&](mlir::Value v) {
        const carrier c = carrierOf(self.typeOf(v));
        return c == carrier::domElement || c == carrier::nullableElement;
    };
    const auto string = [&](mlir::Value v) { return carrierOf(self.typeOf(v)) == carrier::string; };
    if (const llvm::StringRef key = TypeInference::elementCallKey(o); !key.empty()) {
        auto call = llvm::cast<ctjs::CallOp>(o);
        if (!element(call.getReceiver())) { return std::nullopt; }
        for (mlir::Value argument : call.getArgs()) {
            if (!string(argument)) {
                return self.refuse(("an element's " + key + " given " +
                                    printed(self.typeOf(argument)) + ", not a String")
                                       .str());
            }
        }
        const carrier result = carrierOf(self.typeOf(call.getResult()));
        const bool typed = key == "matches"        ? result == carrier::boolean
                           : key == "getAttribute" ? result == carrier::nullableString
                           : (key == "closest" || key == "querySelector")
                               ? result == carrier::nullableElement
                               : call.getResult().use_empty();
        return typed || self.refuse(("an element's " + key + " whose result is " +
                                     printed(self.typeOf(call.getResult())))
                                        .str());
    }
    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(o);
    if (!read || !element(read.getObject())) { return std::nullopt; }
    const llvm::StringRef key = ctjs::constantKey(read.getKey());
    const carrier result = carrierOf(self.typeOf(read.getResult()));
    if (TypeInference::isElementLink(key)) {
        return result == carrier::nullableElement ||
               self.refuse(("an element's " + key + " that is not an element or null").str());
    }
    if (carrierOf(self.typeOf(read.getObject())) != carrier::nullableElement) {
        return std::nullopt;
    }
    if (key == "tagName" || key == "id" || key == "className") {
        return result == carrier::string ||
               self.refuse(("the `" + key + "` of an element or null that is not a String").str());
    }
    return self.refuse(("an element or null read through `" + key +
                        "` - it reads only tagName, id, className and the element links")
                           .str());
}

} // namespace ctcompile::ctnative::lowering_detail
