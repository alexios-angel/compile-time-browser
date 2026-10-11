//===- Promises.cpp - Types of promise operations -------------------------===//
//
// Closure lifting (Lowering/ClosureLifting/Promises.cpp) specialises the
// executor of `new Promise(f)` and the handler of `p.then(f)` for their one
// operation: it names the callback by `ctnative.promise_callback` and passes
// its captured values as the operation's arguments. No ctjs.call_direct
// reaches the callback, so its parameters are typed here, the way a direct
// call types its callee's:
//
// - an executor's resolve function is a `promise_resolve<T>`, where T is the
//   join of every value it is called with, and `new Promise(f)` answers
//   `promise<T>`;
// - a handler's value parameter is the T of the promise `then` is called on;
// - `Promise.resolve(v)` answers `promise<type of v>`.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative {
namespace {

/// The callback a lifted promise operation names, or null.
ctjs::FuncOp promiseCallback(mlir::Operation * op) {
    auto symbol = op->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativePromiseCallback);
    if (!symbol || !llvm::isa<ctjs::ConstructOp, ctjs::CallOp>(op)) { return {}; }
    auto callback = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(op, symbol);
    return callback && !callback.getBody().empty() ? callback : ctjs::FuncOp{};
}

/// The parameter a lifted executor or handler takes after its captures, or
/// null when it declares none.
mlir::BlockArgument promiseParameter(ctjs::FuncOp callback) {
    auto index = callback->getAttrOfType<mlir::IntegerAttr>(kNativePromiseParameter);
    mlir::Block & entry = callback.getBody().front();
    if (!index || index.getInt() < 0 ||
        static_cast<unsigned>(index.getInt()) >= entry.getNumArguments()) {
        return {};
    }
    return entry.getArgument(static_cast<unsigned>(index.getInt()));
}

/// Whether `call` calls a lifted executor's resolve function.
bool callsResolveFunction(ctjs::CallOp call) {
    auto parameter = llvm::dyn_cast<mlir::BlockArgument>(call.getCallee());
    if (!parameter || !parameter.getOwner()->isEntryBlock()) { return false; }
    auto executor = llvm::dyn_cast<ctjs::FuncOp>(parameter.getOwner()->getParentOp());
    return executor && executor->hasAttr(kNativePromiseParameter) &&
           promiseParameter(executor) == parameter;
}

/// Whether `call` is `Promise.resolve(v)` on the realm's own Promise.
bool callsPromiseResolve(ctjs::CallOp call) {
    auto read = ctjs::receiverMethod(call);
    return read && call.getArgs().size() == 1 && read.getObject() == call.getReceiver() &&
           ctjs::constantKey(read.getKey()) == "resolve" &&
           TypeInference::isRealmGlobal(call.getReceiver().getDefiningOp(), "Promise");
}

} // namespace

bool TypeInference::isPromiseCall(mlir::Operation * op) {
    if (promiseCallback(op)) { return true; }
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    return call && (callsResolveFunction(call) || callsPromiseResolve(call));
}

bool TypeInference::isPromiseCallbackParameter(mlir::Value value) {
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
    if (!argument || !argument.getOwner()->isEntryBlock() ||
        argument.getArgNumber() < ctjs::implicit_arguments) {
        return false;
    }
    auto function = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
    return function && function->hasAttr(kNativePromiseParameter);
}

mlir::Type TypeInference::promiseType(mlir::Operation * op,
                                      llvm::ArrayRef<const TypeLattice *> operands) {
    mlir::MLIRContext * c = op->getContext();
    const auto join = [&](mlir::Value parameter, mlir::Type type) {
        TypeLattice * lattice = getLatticeElement(parameter);
        propagateIfChanged(lattice, lattice->join(TypeValue{type}));
    };
    if (ctjs::FuncOp callback = promiseCallback(op)) {
        mlir::Block & entry = callback.getBody().front();
        // Operands are the callee, the receiver or new.target, then the
        // captures, which the callback takes after its implicit arguments.
        for (unsigned i = 2; i < operands.size(); ++i) {
            const unsigned index = ctjs::implicit_arguments + i - 2;
            const mlir::Type type = operands[i]->getValue().getType();
            if (type && index < entry.getNumArguments()) { join(entry.getArgument(index), type); }
        }
        const mlir::BlockArgument parameter = promiseParameter(callback);
        if (llvm::isa<ctjs::ConstructOp>(op)) {
            // The value is whatever the resolve function is called with.
            mlir::Type value = BottomType::get(c);
            if (parameter) {
                for (mlir::Operation * user : parameter.getUsers()) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                    if (call && call.getCallee() == parameter && call.getArgs().size() == 1) {
                        value = joinLattices(op, call.getArgs(), value);
                    }
                }
                join(parameter, PromiseResolveType::get(c, value));
            }
            return PromiseType::get(c, value);
        }
        // A handler receives the value of the promise `then` is called on.
        auto promise = llvm::dyn_cast_or_null<PromiseType>(operands[1]->getValue().getType());
        if (parameter && promise) { join(parameter, promise.getElementType()); }
        // ponytail: the promise `then` returns is not typed yet; admission
        // refuses reading it.
        return OptType::get(c, BottomType::get(c));
    }
    auto call = llvm::dyn_cast<ctjs::CallOp>(op);
    if (!call) { return {}; }
    if (callsResolveFunction(call)) { return OptType::get(c, BottomType::get(c)); }
    if (callsPromiseResolve(call)) {
        // An argument not yet visited answers nothing yet; bottom is the
        // identity of the join.
        const mlir::Type value = operands[2]->getValue().getType();
        return value ? mlir::Type(PromiseType::get(c, value)) : BottomType::get(c);
    }
    return {};
}

} // namespace ctcompile::ctnative
