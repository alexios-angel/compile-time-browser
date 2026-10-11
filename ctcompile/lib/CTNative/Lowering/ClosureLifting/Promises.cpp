//===- ClosureLifting/Promises.cpp - Callbacks a promise keeps -*- C++ -*-===//
//
// Specialises a closure handed to a promise: the executor of
// `new Promise(f)` on the realm's own Promise, or the handler of `p.then(f)`
// on a promise that `new Promise`, `Promise.resolve` or another `then` made.
// The closure's only use is that argument, so the construct or call is its
// one call site, as for a host callback (HostCallbacks.cpp): lift() makes its
// captures leading parameters, and the operation then passes the captured
// values as its arguments. The lowering spells it as
// `ctnative::Promise.construct<T>(std::bind_front(f, captures...))` or
// `ctnative::Promise.prototype.then.call(p, std::bind_front(f, captures...))`
// (Runtime/Promise.hpp).
//
// A handler runs from the microtask queue after the frame that made it has
// returned, so every capture is copied into it, and a binding written after
// the capture is refused. The executor runs during the construct, but takes
// its captures the same way, which keeps one rule for both. Its resolve
// function may only be called, with one argument: a resolve function that is
// stored or passed on would need the promise's value type wherever it goes.
// Which values may be copied, and which promise values are native, is a type
// question admission asks (Admission/Promises.cpp).
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include <array>

namespace ctcompile::ctnative::lowering_detail {
namespace {

/// Whether `value` is a promise made where closure lifting can see it: by
/// `new Promise` on the realm's Promise, `Promise.resolve` or a `then`.
bool madeAsPromise(mlir::Value value) {
    mlir::Operation * made = value.getDefiningOp();
    if (auto construct = llvm::dyn_cast_or_null<ctjs::ConstructOp>(made)) {
        return TypeInference::isRealmGlobal(construct.getCallee().getDefiningOp(), "Promise");
    }
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(made);
    if (!call) { return false; }
    auto read = ctjs::receiverMethod(call);
    if (!read || read.getObject() != call.getReceiver()) { return false; }
    const llvm::StringRef key = ctjs::constantKey(read.getKey());
    if (key == "resolve") {
        return TypeInference::isRealmGlobal(call.getReceiver().getDefiningOp(), "Promise");
    }
    return key == "then" && madeAsPromise(call.getReceiver());
}

} // namespace

void closureLifter::promiseCallbackCensus() {
    promiseCallbacks.clear();
    for (ctjs::CreateClosureOp c : closures) {
        if (!c.getResult().hasOneUse()) { continue; }
        mlir::OpOperand & use = *c.getResult().getUses().begin();
        // A construct's operands are the callee, new.target and the
        // arguments; a call's the callee, the receiver and the arguments. In
        // both the callback is the first argument.
        if (use.getOperandNumber() != 2) { continue; }
        mlir::Operation * user = use.getOwner();
        if (auto construct = llvm::dyn_cast<ctjs::ConstructOp>(user)) {
            if (TypeInference::isRealmGlobal(construct.getCallee().getDefiningOp(), "Promise")) {
                promiseCallbacks[c.getOperation()] = user;
            }
            continue;
        }
        auto call = llvm::dyn_cast<ctjs::CallOp>(user);
        if (!call) { continue; }
        auto read = ctjs::receiverMethod(call);
        if (read && read.getObject() == call.getReceiver() && read.getResult().hasOneUse() &&
            ctjs::methodKey(call) == "then" && madeAsPromise(call.getReceiver())) {
            promiseCallbacks[c.getOperation()] = user;
        }
    }
}

std::optional<std::string> closureLifter::whyNotPromiseCallback(ctjs::CreateClosureOp c) {
    mlir::Operation * site = promiseCallbacks.lookup(c.getOperation());
    const bool executor = llvm::isa<ctjs::ConstructOp>(site);
    const std::string callback =
        executor ? "the executor of new Promise" : "the handler of Promise.prototype.then";
    const unsigned arguments = static_cast<unsigned>(site->getNumOperands() - 2);
    if (arguments != 1) {
        return executor ? "new Promise with " + std::to_string(arguments) +
                              " arguments - only (executor) is native"
                        : "Promise.prototype.then with " + std::to_string(arguments) +
                              " arguments - only (onFulfilled) is native";
    }
    if (auto why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    if (!entry.getArgument(ctjs::arg_receiver).use_empty()) { return callback + " reads `this`"; }
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) {
        return callback + " reads new.target";
    }
    if (ctjs::readsRawArguments(target)) { return callback + " reads its `arguments`"; }
    const unsigned declared = ctjs::parameterCount(entry);
    if (executor && declared != 1) {
        return callback + " declares " + std::to_string(declared) +
               " parameters - only (resolve) is native";
    }
    if (!executor && declared > 1) {
        return callback + " declares " + std::to_string(declared) +
               " parameters - only (value) is native";
    }
    if (executor) {
        // The resolve function is the first declared parameter.
        const mlir::BlockArgument resolve = entry.getArgument(ctjs::implicit_arguments);
        for (mlir::OpOperand & use : resolve.getUses()) {
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            if (!call || use.getOperandNumber() != 0 || call.getArgs().size() != 1) {
                return callback +
                       " uses its resolve function other than by calling it with one value - "
                       "only a resolve function that stays in the executor is native";
            }
        }
    }
    // A handler runs after this frame returns, with its captures copied in.
    for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
        if (slotIsCarried(c, i)) {
            return callback + " captures binding " + std::to_string(i) +
                   ", which is written after it is captured - the promise keeps the callback, "
                   "so it takes its captures by value";
        }
    }
    if (auto why = whyCapturesDoNotLift(c)) { return why; }
    return whyLiftTailFails(c, target, std::array<mlir::Operation *, 1>{site});
}

void closureLifter::rewritePromiseCallback(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                           liftReport & out) {
    mlir::Operation * site = promiseCallbacks.lookup(c.getOperation());
    // The operation passes the captured values in place of the callback.
    const llvm::SmallVector<mlir::Value> captured = liftedCaptures(c);
    const auto captures = static_cast<unsigned>(captured.size());
    if (auto construct = llvm::dyn_cast<ctjs::ConstructOp>(site)) {
        construct.getArgsMutable().assign(captured);
    } else {
        llvm::cast<ctjs::CallOp>(site).getArgsMutable().assign(captured);
    }
    mlir::Builder b(context);
    site->setAttr(kNativePromiseCallback, mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()));
    // The declared parameter follows the implicit arguments and the captures.
    target->setAttr(kNativePromiseParameter,
                    b.getI64IntegerAttr(ctjs::implicit_arguments + captures));
    // Public for the reason rewriteArrayCallback gives: no ctjs.call_direct
    // names the callback, and type inference must still visit its body.
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    markLifted(c, out, captures);
    ++out.calls;
}

} // namespace ctcompile::ctnative::lowering_detail
