//===- Admission/BorrowedCallbacks.cpp - Borrowed callbacks ---------------===//
//
// Admits the two operations of a borrowed callback
// (ClosureLifting/BorrowedCallbacks.cpp): the closure, which becomes
// `std::bind_front(callback, captures...)`, and a call through the parameter
// it is passed to, which calls a `ctnative::function_ref<R(A...)>`. Both need
// values the bound callable can copy and pass, and the function_ref needs one
// signature: every callback the parameter can hold takes exactly the
// argument types of every call through it and returns exactly that call's
// result type.
//
//===----------------------------------------------------------------------===//

#include "Admission.h"

#include "ctcompile/CTNative/Analysis/NativeClosure.h"

#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative::lowering_detail {

bool borrowedCallbackOperation(admission & self, mlir::Operation * o) {
    // A Number, a Boolean or a String is copied into the bound callable and
    // passed by value; anything else would need an owner or a borrow of its
    // own.
    const auto byValue = [&](mlir::Type type, const llvm::Twine & what) {
        const carrier c = carrierOf(type);
        return isScalarCarrier(c) || isStringCarrier(c) ||
               self.refuse(("a borrowed callback's " + what + " is " + printed(type) +
                            ", not a Number, a Boolean or a String")
                               .str());
    };
    if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(o)) {
        auto callback = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
            o, mlir::StringAttr::get(o->getContext(), borrowedTarget(o)));
        for (auto [i, captured] : llvm::enumerate(made.getUpvalues())) {
            // An object the callback takes as a pointer (an arrow's lexical
            // `this`, ClosureLifting/ArrowThis.cpp) is copied as that pointer.
            // That is a borrow: the callee holds the callback as a
            // ctnative::function_ref, which it can only call during the call,
            // so the object outlives every call through it.
            if (callback && admission::isObjectArg(callback, ctjs::implicit_arguments +
                                                                 static_cast<unsigned>(i))) {
                if (!admission::isClosedObject(captured)) {
                    return self.refuse("a borrowed callback's capture " + std::to_string(i) +
                                       " is an object whose shape is not closed");
                }
                continue;
            }
            if (!byValue(self.typeOf(captured), "capture " + llvm::Twine(i))) { return false; }
        }
        return true;
    }
    auto call = llvm::cast<ctjs::CallOp>(o);
    // A field of a set of closures that no write has filled yet is absent,
    // and calling it would throw (ClosureSets.cpp); a typeof test narrows it.
    if (llvm::isa_and_nonnull<OptType>(self.typeOf(call.getCallee()))) {
        return self.refuse("it calls a field of a set of closures that may be absent - test it "
                           "with `typeof f === \"function\"` first");
    }
    const mlir::Type result = self.typeOf(call.getResult());
    if (!byValue(result, "result")) { return false; }
    for (auto [i, argument] : llvm::enumerate(call.getArgs())) {
        if (!byValue(self.typeOf(argument), "argument " + llvm::Twine(i))) { return false; }
    }
    for (auto symbol : o->getAttrOfType<mlir::ArrayAttr>(kNativeBorrowedCall)
                           .getAsRange<mlir::FlatSymbolRefAttr>()) {
        auto callback = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(o, symbol);
        mlir::Block & entry = callback.getBody().front();
        const auto captures = callback->getAttrOfType<mlir::IntegerAttr>(kNativeCaptures);
        const unsigned skip =
            ctjs::implicit_arguments + (captures ? static_cast<unsigned>(captures.getInt()) : 0u);
        for (auto [i, argument] : llvm::enumerate(call.getArgs())) {
            const mlir::Type takes =
                self.typeOf(entry.getArgument(skip + static_cast<unsigned>(i)));
            if (takes != self.typeOf(argument)) {
                return self.refuse(
                    "the borrowed callback `" + symbol.getValue().str() + "` takes parameter " +
                    std::to_string(i) + " as " + printed(takes) + " and a call passes " +
                    printed(self.typeOf(argument)) + " - one C++ signature needs one type");
            }
        }
        mlir::Type returned;
        callback.walk(
            [&](ctjs::ReturnOp ret) { returned = meet(returned, self.typeOf(ret.getValue())); });
        if (returned != result) {
            return self.refuse("the borrowed callback `" + symbol.getValue().str() + "` returns " +
                               printed(returned) + " and the call through its parameter answers " +
                               printed(result) + " - one C++ signature needs one type");
        }
    }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
