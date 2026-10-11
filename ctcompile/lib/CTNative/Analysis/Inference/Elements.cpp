//===- Elements.cpp - Types of an ordinary script's element members -------===//
//
// The element calls and reads Bootstrap's SelectorEngine and Manipulator
// make, typed for an ordinary script (no host contract), where an element is
// the runtime's js_element_t (Runtime/Browser.hpp):
//
//   el.matches(s)                               Boolean
//   el.closest(s), el.querySelector(s)          an element or null
//   el.getAttribute(s)                          a String or null
//   el.setAttribute(s, v), el.removeAttribute(s)  undefined
//   el.previousElementSibling, el.nextElementSibling, el.parentElement
//                                               an element or null
//
// An element or null is `opt<dom_element>`. Its members are typed as an
// element's: reading one off null throws the VM's TypeError, and the runtime
// throws the same text.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"
#include "ctcompile/CTNative/IR/CTNativeLattice.h"

namespace ctcompile::ctnative {

namespace {

/// One element method an ordinary script may call: its key and its number of
/// arguments.
struct elementMethod {
    llvm::StringLiteral key;
    unsigned arity;
};
constexpr elementMethod elementMethods[] = {
    {"matches", 1},      {"closest", 1},      {"querySelector", 1},
    {"getAttribute", 1}, {"setAttribute", 2}, {"removeAttribute", 1},
};

/// Whether `type` is an element or an element that may be null.
bool elementOrNull(mlir::Type type) {
    if (auto opt = llvm::dyn_cast_or_null<OptType>(type)) { type = opt.getElementType(); }
    return llvm::isa_and_nonnull<DOMElementType>(type);
}

} // namespace

llvm::StringRef TypeInference::elementCallKey(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    auto read = call ? ctjs::receiverMethod(call) : ctjs::GetPropertyOp{};
    if (!read || read.getObject() != call.getReceiver() || !read.getResult().hasOneUse()) {
        return {};
    }
    const llvm::StringRef key = ctjs::methodKey(call);
    for (const elementMethod & method : elementMethods) {
        if (key == method.key && call.getArgs().size() == method.arity) { return method.key; }
    }
    return {};
}

bool TypeInference::isElementLink(llvm::StringRef key) {
    return key == "previousElementSibling" || key == "nextElementSibling" || key == "parentElement";
}

mlir::Type TypeInference::elementMemberType(mlir::Operation * op,
                                            llvm::ArrayRef<const TypeLattice *> operands) {
    mlir::MLIRContext * c = op->getContext();
    const auto element = DOMElementType::get(c);
    if (const llvm::StringRef key = elementCallKey(op); !key.empty()) {
        // Operands are the callee, the receiver, then the arguments.
        if (!elementOrNull(operands[1]->getValue().getType())) { return {}; }
        if (key == "matches") { return BoolType::get(c); }
        if (key == "closest" || key == "querySelector") { return OptType::get(c, element); }
        if (key == "getAttribute") { return OptType::get(c, defaultStringType(c)); }
        return OptType::get(c, BottomType::get(c));
    }
    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
    if (!read || !elementOrNull(operands[0]->getValue().getType())) { return {}; }
    const llvm::StringRef key = ctjs::constantKey(read.getKey());
    if (isElementLink(key)) { return OptType::get(c, element); }
    // An element that may be null reads the members an element does.
    if (llvm::isa<OptType>(operands[0]->getValue().getType()) &&
        (key == "tagName" || key == "id" || key == "className")) {
        return defaultStringType(c);
    }
    return {};
}

} // namespace ctcompile::ctnative
