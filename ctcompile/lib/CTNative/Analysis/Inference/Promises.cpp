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
//   join of every value it is called with (the value of a promise, which it
//   adopts), by the executor or by a then-handler that captured it, and
//   `new Promise(f)` answers `promise<T>`;
// - a handler's value parameter is the T of the promise `then` is called on,
//   and `then` answers a promise of what the handler returns (again the value
//   of a returned promise);
// - `Promise.resolve(v)` answers `promise<type of v>`, or v's own type when v
//   is a promise, and `Promise.resolve()` a promise of undefined.
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

/// The value a promise resolved with `resolution` settles with: the value of
/// a promise, which it adopts, or `resolution` itself. A null type (nothing
/// visited yet) is bottom.
mlir::Type settledValue(mlir::Type resolution) {
    if (auto promise = llvm::dyn_cast_or_null<PromiseType>(resolution)) {
        return promise.getElementType();
    }
    return resolution;
}

/// Whether `call` calls a lifted executor's resolve function, in the executor
/// or through a then-handler's capture of it (kNativePromiseResolveArgs). A
/// handler's own value parameter is not one, even when it is called.
bool callsResolveFunction(ctjs::CallOp call) {
    auto parameter = llvm::dyn_cast<mlir::BlockArgument>(call.getCallee());
    if (!parameter || !parameter.getOwner()->isEntryBlock()) { return false; }
    auto callback = llvm::dyn_cast<ctjs::FuncOp>(parameter.getOwner()->getParentOp());
    auto resolves =
        callback ? callback->getAttrOfType<mlir::DenseI64ArrayAttr>(kNativePromiseResolveArgs)
                 : mlir::DenseI64ArrayAttr{};
    return resolves && llvm::is_contained(resolves.asArrayRef(), parameter.getArgNumber());
}

/// Whether `call` is `Promise.resolve(v)` or `Promise.resolve()` on the
/// realm's own Promise.
bool callsPromiseResolve(ctjs::CallOp call) {
    auto read = ctjs::receiverMethod(call);
    return read && call.getArgs().size() <= 1 && read.getObject() == call.getReceiver() &&
           ctjs::constantKey(read.getKey()) == "resolve" &&
           TypeInference::isRealmGlobal(call.getReceiver().getDefiningOp(), "Promise");
}

} // namespace

bool TypeInference::isPromiseCall(mlir::Operation * op) {
    if (promiseCallback(op)) { return true; }
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    return call && (callsResolveFunction(call) || callsPromiseResolve(call));
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
            // The value is whatever the resolve function is called with, or
            // the value of a promise it is called with, which it adopts.
            mlir::Type value = BottomType::get(c);
            const auto joinCalls = [&](mlir::Value resolve) {
                for (mlir::Operation * user : resolve.getUsers()) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                    if (call && call.getCallee() == resolve && call.getArgs().size() == 1) {
                        value = meet(value, settledValue(joinLattices(op, call.getArgs(), {})));
                    }
                }
            };
            if (parameter) {
                joinCalls(parameter);
                // A then-handler that captured the resolve function calls it
                // through its capture parameter.
                for (mlir::OpOperand & use : parameter.getUses()) {
                    ctjs::FuncOp handler = llvm::isa<ctjs::CallOp>(use.getOwner())
                                               ? promiseCallback(use.getOwner())
                                               : ctjs::FuncOp{};
                    if (!handler || use.getOperandNumber() < 2) { continue; }
                    mlir::Block & entry = handler.getBody().front();
                    const unsigned index = ctjs::implicit_arguments + use.getOperandNumber() - 2;
                    if (index < entry.getNumArguments()) { joinCalls(entry.getArgument(index)); }
                }
                join(parameter, PromiseResolveType::get(c, value));
            }
            return PromiseType::get(c, value);
        }
        // A handler receives the value of the promise `then` is called on.
        auto promise = llvm::dyn_cast_or_null<PromiseType>(operands[1]->getValue().getType());
        // A value not yet known is not joined: a parameter holding bottom
        // would be visited as a value that is nothing, and an operation that
        // widens on that cannot narrow again.
        if (parameter && promise && !llvm::isa<BottomType>(promise.getElementType())) {
            join(parameter, promise.getElementType());
        }
        // The promise `then` returns settles with what the handler returns,
        // adopting it when that is a promise.
        llvm::SmallVector<mlir::Value> returned;
        callback.walk([&](ctjs::ReturnOp ret) { returned.push_back(ret.getValue()); });
        return PromiseType::get(c, settledValue(joinLattices(op, returned, BottomType::get(c))));
    }
    auto call = llvm::dyn_cast<ctjs::CallOp>(op);
    if (!call) { return {}; }
    if (callsResolveFunction(call)) { return OptType::get(c, BottomType::get(c)); }
    if (callsPromiseResolve(call)) {
        if (call.getArgs().empty()) {
            return PromiseType::get(c, OptType::get(c, BottomType::get(c)));
        }
        // An argument not yet visited answers nothing yet; bottom is the
        // identity of the join.
        const mlir::Type value = operands[2]->getValue().getType();
        return value ? mlir::Type(PromiseType::get(c, settledValue(value))) : BottomType::get(c);
    }
    return {};
}

} // namespace ctcompile::ctnative
