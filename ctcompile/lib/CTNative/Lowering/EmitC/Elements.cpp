//===- Elements.cpp - Emission of an ordinary script's element members ----===//
//
// Spells the element calls and reads Admission/Elements.cpp admits as the
// runtime's js_element_t members of the same names (Runtime/Browser.hpp):
//
//   el.matches(s)                -> el.matches(s)
//   el.previousElementSibling    -> el.previousElementSibling()
//   i.matches(s), i an element or null
//                                -> ctnative::present_element(i).matches(s)
//
// present_element throws the VM's TypeError for null, as reading a member of
// null does.
//
//===----------------------------------------------------------------------===//

#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

std::optional<std::string> elementAction(lowering & lower, mlir::Operation * o) {
    const auto element = [&](mlir::Value v) {
        const carrier c = carrierOf(lower.typeOf(v));
        return c == carrier::domElement || c == carrier::nullableElement;
    };
    // The names are recorded now: by replacement time the keys are emitc
    // constants.
    if (const llvm::StringRef key = TypeInference::elementCallKey(o);
        !key.empty() && element(llvm::cast<ctjs::CallOp>(o).getReceiver())) {
        return ("element_call:" + key).str();
    }
    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(o);
    if (!read || !element(read.getObject())) { return std::nullopt; }
    const llvm::StringRef key = ctjs::constantKey(read.getKey());
    if (TypeInference::isElementLink(key) ||
        carrierOf(lower.typeOf(read.getObject())) == carrier::nullableElement) {
        return ("element_read:" + key).str();
    }
    return std::nullopt;
}

bool replaceElement(mlir::Operation * o, llvm::StringRef kind) {
    mlir::MLIRContext * context = o->getContext();
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    // The element itself, through present_element when it may be null.
    const auto receiver = [&](mlir::Value v) {
        if (opaqueSpelling(v.getType()) != kNullableElementType) { return v; }
        return callValue(b, where, ec::OpaqueType::get(context, kPageElementType),
                         "ctnative::present_element", v);
    };
    const auto nullable = carrierType(context, carrier::nullableElement);
    if (llvm::StringRef key = kind; key.consume_front("element_call:")) {
        auto call = llvm::cast<ctjs::CallOp>(o);
        const mlir::Value self = receiver(call.getReceiver());
        if (key == "setAttribute" || key == "removeAttribute") {
            ec::MemberCallOpaqueOp::create(b, where, mlir::TypeRange{}, self, b.getStringAttr(key),
                                           mlir::ArrayAttr{}, mlir::ArrayAttr{}, call.getArgs());
            lowering::eraseIfUnused(o);
            return true;
        }
        const mlir::Type result = key == "matches" ? carrierType(context, carrier::boolean)
                                  : key == "getAttribute"
                                      ? carrierType(context, carrier::nullableString)
                                      : nullable;
        o->getResult(0).replaceAllUsesWith(memberCall(b, where, result, self, key, call.getArgs()));
        lowering::eraseIfUnused(o);
        return true;
    }
    llvm::StringRef key = kind;
    if (!key.consume_front("element_read:")) { return false; }
    const mlir::Type result =
        TypeInference::isElementLink(key) ? nullable : carrierType(context, carrier::string);
    o->getResult(0).replaceAllUsesWith(
        memberCall(b, where, result, receiver(o->getOperand(0)), key));
    lowering::eraseIfUnused(o);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
