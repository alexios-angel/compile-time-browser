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
// returned, so every capture is copied into it, except a binding of that
// frame written after the capture, which the two share through a
// std::shared_ptr<ctnative::cell<T>> (rule 2b in ClosureLifter.h). The executor runs during the
// construct, but takes its captures the same way, which keeps one rule for both. Its resolve
// function may only be called, with one argument: a resolve function that is
// stored or passed on would need the promise's value type wherever it goes.
// A then-handler made in the executor may capture it under the same rule,
// as Popper's memoised update does:
//
//   new Promise(function (t) { Promise.resolve().then(function () { t(s()); }); })
//
// The handler takes its own copy of the resolve function, which shares the
// promise and its [[AlreadyResolved]] record (Runtime/Promise.hpp), so a call
// after the executor has returned settles the same promise. Type inference
// joins those calls into the promise's value (Inference/Promises.cpp), reading
// the arguments kNativePromiseResolveArgs lists.
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

/// Whether every use of `value` calls it with one argument.
bool onlyCalledWithOneValue(mlir::Value value) {
    return llvm::all_of(value.getUses(), [](mlir::OpOperand & use) {
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        return call && use.getOperandNumber() == 0 && call.getArgs().size() == 1;
    });
}

} // namespace

void closureLifter::promiseCallbackCensus() {
    promiseCallbacks.clear();
    executorResolves.clear();
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
                ctjs::FuncOp target = targetOf(c);
                if (target && !target.getBody().empty() &&
                    target.getBody().front().getNumArguments() > ctjs::implicit_arguments) {
                    executorResolves.insert(
                        target.getBody().front().getArgument(ctjs::implicit_arguments));
                }
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
    // The resolve function is the first declared parameter.
    if (executor && !resolveStaysCalled(entry.getArgument(ctjs::implicit_arguments))) {
        return callback +
               " uses its resolve function other than by calling it with one value - only a "
               "resolve function called that way by the executor, or by a then-handler made in "
               "it, is native";
    }
    // A handler runs after this frame returns, with its captures copied in; a
    // binding of this frame written after the capture is shared through a cell
    // instead (rule 2b in ClosureLifter.h).
    for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
        if (slotIsCarried(c, i) && !slotIsShared(c, i)) {
            return callback + " captures binding " + std::to_string(i) +
                   ", which is written after it is captured and reaches it through an "
                   "enclosing closure - the promise keeps the callback, and only a binding "
                   "of the frame that makes it is shared";
        }
    }
    if (auto why = whyCapturesDoNotLift(c)) { return why; }
    return whyLiftTailFails(c, target, std::array<mlir::Operation *, 1>{site});
}

/// Whether the executor's `resolve` is only called with one value, by the
/// executor or by then-handlers made in it. A captured parameter lives in a
/// cell, so a capture is a use of that cell: it must never be written, each
/// read only called, and each closure capturing it a then-handler whose slot
/// is only read to be called, never written and never relayed to a closure of
/// its own. A handler lifted in an earlier round takes the resolve function as
/// an argument of its `then` call instead, and its parameter is checked.
bool closureLifter::resolveStaysCalled(mlir::BlockArgument resolve) {
    const auto handlerOnlyCalls = [&](ctjs::CreateClosureOp handler, unsigned slot) {
        ctjs::FuncOp target = targetOf(handler);
        if (!llvm::isa_and_nonnull<ctjs::CallOp>(promiseCallbacks.lookup(handler.getOperation())) ||
            !target || target.getBody().empty() || writesSlot(target, slot)) {
            return false;
        }
        return !target.getBody()
                    .walk([&](mlir::Operation * op) {
                        auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op);
                        auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
                        const bool relays =
                            made && made.getEnclosingIndicesAttr() &&
                            llvm::is_contained(made.getEnclosingIndicesAttr().asArrayRef(),
                                               static_cast<std::int32_t>(slot));
                        const bool escapes = read &&
                                             static_cast<unsigned>(read.getIndex()) == slot &&
                                             !onlyCalledWithOneValue(read.getResult());
                        return relays || escapes ? mlir::WalkResult::interrupt()
                                                 : mlir::WalkResult::advance();
                    })
                    .wasInterrupted();
    };
    for (mlir::OpOperand & use : resolve.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            if (use.getOperandNumber() == 0 && call.getArgs().size() == 1) { continue; }
            auto symbol = call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativePromiseCallback);
            auto handler =
                symbol ? mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, symbol)
                       : ctjs::FuncOp{};
            if (!handler || handler.getBody().empty() || use.getOperandNumber() < 2) {
                return false;
            }
            const unsigned index = ctjs::implicit_arguments + use.getOperandNumber() - 2;
            mlir::Block & entry = handler.getBody().front();
            if (index >= entry.getNumArguments() ||
                !onlyCalledWithOneValue(entry.getArgument(index))) {
                return false;
            }
            continue;
        }
        auto cell = llvm::dyn_cast<ctjs::CreateCellOp>(user);
        if (!cell) { return false; }
        for (mlir::OpOperand & held : cell.getResult().getUses()) {
            const cellUse kind = classify(held);
            if (kind == cellUse::read &&
                onlyCalledWithOneValue(llvm::cast<ctjs::CellGetOp>(held.getOwner()).getResult())) {
                continue;
            }
            if (kind == cellUse::capture &&
                handlerOnlyCalls(llvm::cast<ctjs::CreateClosureOp>(held.getOwner()),
                                 held.getOperandNumber() - kFirstCapture)) {
                continue;
            }
            return false;
        }
    }
    return true;
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
    // The arguments that hold a resolve function: an executor's parameter, or
    // a handler's captures of its executor's, which resolveStaysCalled let
    // through.
    llvm::SmallVector<std::int64_t> resolves;
    if (llvm::isa<ctjs::ConstructOp>(site)) {
        resolves.push_back(ctjs::implicit_arguments + captures);
    } else {
        for (unsigned i = 0; i < captures; ++i) {
            if (executorResolves.contains(captured[i])) {
                resolves.push_back(ctjs::implicit_arguments + i);
            }
        }
    }
    if (!resolves.empty()) {
        target->setAttr(kNativePromiseResolveArgs, b.getDenseI64ArrayAttr(resolves));
    }
    // Public for the reason rewriteArrayCallback gives: no ctjs.call_direct
    // names the callback, and type inference must still visit its body.
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    markLifted(c, out, captures);
    ++out.calls;
}

} // namespace ctcompile::ctnative::lowering_detail
