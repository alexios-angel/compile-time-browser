//===- Admission/Promises.cpp - Promise operations ------------------------===//
//
// Admits the promise operations of an ordinary script
// (TypeInference::isPromiseCall): a `new Promise(f)` or `p.then(f)` closure
// lifting specialised for its callback, `Promise.resolve(v)`, and a call of an
// executor's resolve function. A promise is native only when its value was
// proved to be a Number, a Boolean, a String or undefined. Anything else is
// refused by name: an unproved value has no C++ type, and an object could
// have a `then` key, which would make resolving with it adopt that object
// instead. The
// callback's captures are copied into it (Runtime/Promise.hpp keeps a handler
// until the microtask queue runs it), so only values with no identity to
// share qualify, as for a host callback.
//
//===----------------------------------------------------------------------===//

#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {

/// Refuses a promise, or a resolve function, whose value is not native.
bool nativePromise(admission & self, mlir::Value value, llvm::StringRef what) {
    if (carrierOf(self.typeOf(value)) != carrier::none) { return true; }
    return self.refuse(what.str() + " is " + printed(self.typeOf(value)) +
                       " - only a promise of a Number, a Boolean, a String or undefined is "
                       "native; any other value is unproved, and an object could have a `then` "
                       "key");
}

} // namespace

bool promiseOperation(admission & self, mlir::Operation * o) {
    if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(o)) {
        if (!nativePromise(self, made.getResult(), "the promise new Promise makes")) {
            return false;
        }
    }
    auto call = llvm::dyn_cast<ctjs::CallOp>(o);
    if (call && !o->hasAttr(kNativePromiseCallback)) {
        // `Promise.resolve(v)`, or `resolve(v)` in an executor.
        if (auto resolve = llvm::dyn_cast<mlir::BlockArgument>(call.getCallee())) {
            if (!nativePromise(self, resolve, "the executor's resolve function")) { return false; }
            // A promise argument is adopted (Runtime/Promise.hpp).
            const carrier given = carrierOf(self.typeOf(call.getArgs().front()));
            if (!isScalarPromiseValue(given) && given != carrier::promise) {
                return self.refuse("the executor's resolve function is called with " +
                                   printed(self.typeOf(call.getArgs().front())) +
                                   ", not a Number, a Boolean, a String, undefined or a promise "
                                   "of one");
            }
            return true;
        }
        return nativePromise(self, call.getResult(), "the promise Promise.resolve makes");
    }
    if (call) {
        if (!nativePromise(self, call.getReceiver(), "the promise `then` is called on")) {
            return false;
        }
        if (!nativePromise(self, call.getResult(), "the promise Promise.prototype.then returns")) {
            return false;
        }
    }
    const llvm::StringRef callback =
        call ? "the handler of Promise.prototype.then" : "the executor of new Promise";
    const auto captures = call ? call.getArgs() : llvm::cast<ctjs::ConstructOp>(o).getArgs();
    for (mlir::Value capture : captures) {
        switch (carrierOf(self.typeOf(capture))) {
        case carrier::number:
        case carrier::boolean:
        case carrier::string:
        case carrier::domElement: break;
        default:
            return self.refuse(callback.str() + " captures " + printed(self.typeOf(capture)) +
                               " - the promise keeps a copy, which only a Number, a Boolean, a "
                               "String or an element shares");
        }
    }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
