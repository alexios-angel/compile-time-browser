//===- ClosureLifting/ArrayCallbacks.cpp - Array-method callbacks -*- C++ -*-===//
//
// Specialises a closure passed to one of the Array methods the native runtime
// implements with a callback (forEach, map, filter, some, every, find, reduce
// and sort, in Runtime/Array.hpp). The closure's only use is that argument, so
// the call is its one call site: lift() makes its captures leading parameters
// as for any lifted closure, and the call then passes the captured values in
// place of the closure. A binding the callback or its frame writes is passed
// by the address of its variable, as a lifted direct call passes it. The
// lowering spells the call as
// `ctnative::Array.prototype.filter.call(a, std::bind_front(callback, captures...))`.
//
// Whether the receiver is an array the runtime can iterate, and what the call
// answers, are type questions; admission (Admission/ArrayCallbacks.cpp) asks
// them once types are known.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

#include <array>

namespace ctcompile::ctnative::lowering_detail {

void closureLifter::arrayCallbackCensus() {
    arrayCallbacks.clear();
    for (ctjs::CreateClosureOp c : closures) {
        if (!c.getResult().hasOneUse()) { continue; }
        mlir::OpOperand & use = *c.getResult().getUses().begin();
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        // Operands are the callee, the receiver and the arguments; the
        // callback is the first argument.
        if (!call || use.getOperandNumber() != 2) { continue; }
        auto read = ctjs::receiverMethod(call);
        if (!read || read.getObject() != call.getReceiver() || !read.getResult().hasOneUse() ||
            !TypeInference::isArrayCallbackMethod(ctjs::methodKey(call))) {
            continue;
        }
        arrayCallbacks[c.getOperation()] = call;
    }
}

std::optional<std::string> closureLifter::whyNotArrayCallback(ctjs::CreateClosureOp c) {
    ctjs::CallOp call = arrayCallbacks.lookup(c.getOperation());
    const std::string method = ("Array.prototype." + ctjs::methodKey(call)).str();
    const bool reduce = ctjs::methodKey(call) == "reduce";
    const bool sort = ctjs::methodKey(call) == "sort";
    const std::size_t arguments = call.getArgs().size();
    if (reduce && arguments < 2) {
        return method + " without an initial value - it throws on an empty array";
    }
    if (arguments > (reduce ? 2u : 1u)) {
        return method + (reduce ? " with an argument after the initial value"
                         : sort ? " with an argument after the comparator"
                                : " called with a thisArg");
    }
    if (auto why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    // The runtime calls the callback with `this` undefined and no new.target.
    if (!entry.getArgument(ctjs::arg_receiver).use_empty()) {
        return "the callback of " + method + " reads `this`";
    }
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) {
        return "the callback of " + method + " reads new.target";
    }
    if (ctjs::readsRawArguments(target)) {
        return "the callback of " + method + " reads its `arguments`";
    }
    // The runtime passes (accumulator,) element, index and array, or a
    // comparator two elements, and calls a callback with as many of them as
    // it declares, from the first.
    const unsigned declared = ctjs::parameterCount(entry);
    const unsigned passed = reduce ? 4 : sort ? 2 : 3;
    if (declared == 0 || declared > passed) {
        return "the callback of " + method + " declares " + std::to_string(declared) +
               " parameters - it is passed " + std::to_string(passed) + ", and needs at least one";
    }
    // The runtime calls the callback only during the call, so a binding the
    // callback or its frame writes (a carried slot) is passed by the address
    // of its variable, which outlives the call. A binding that a callback the
    // runtime keeps also holds is a shared cell's handle instead, which this
    // specialisation does not pass.
    for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
        if (slotIsShared(c, i)) {
            return "the callback of " + method + " captures binding " + std::to_string(i) +
                   ", which a callback the runtime keeps also holds - the callback takes its "
                   "captures by value or by address, not as a shared handle";
        }
    }
    if (auto why = whyCapturesDoNotLift(c)) { return why; }
    return whyLiftTailFails(c, target, std::array<mlir::Operation *, 1>{call.getOperation()});
}

void closureLifter::rewriteArrayCallback(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                         liftReport & out) {
    ctjs::CallOp call = arrayCallbacks.lookup(c.getOperation());
    const auto captured = liftedCaptures(c);
    // The call passes the captured values where it passed the closure, then
    // reduce's initial value.
    llvm::SmallVector<mlir::Value> arguments(captured);
    llvm::append_range(arguments, call.getArgs().drop_front());
    call.getArgsMutable().assign(arguments);
    call->setAttr(kNativeArrayCallback, mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()));
    // lift() listed the captures the callback takes by address in its entry
    // numbers; the call lists them in its own operand numbers, capture k
    // being entry argument 3 + k and operand 2 + k.
    if (auto listed = target->getAttrOfType<mlir::DenseI32ArrayAttr>(kNativeCellArgs)) {
        llvm::SmallVector<int32_t> operands;
        for (const int32_t index : listed.asArrayRef()) { operands.push_back(index - 1); }
        call->setAttr(kNativeCellArgs, mlir::Builder(context).getDenseI32ArrayAttr(operands));
    }
    target->setAttr(kNativeArrayCallback, mlir::UnitAttr::get(context));
    // lift() made the callback private, but no ctjs.call_direct names it, and
    // MLIR's DeadCodeAnalysis treats a private function nothing calls as dead,
    // so type inference would never visit its body. Public, its parameters
    // reach TypeInference::setToEntryState, which leaves them to the call
    // (Inference/ArrayCallbacks.cpp).
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    markLifted(c, out, static_cast<unsigned>(captured.size()));
    ++out.calls;
}

} // namespace ctcompile::ctnative::lowering_detail
