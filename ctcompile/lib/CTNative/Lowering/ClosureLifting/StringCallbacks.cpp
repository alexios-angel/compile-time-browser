//===- ClosureLifting/StringCallbacks.cpp - replace callbacks ---*- C++ -*-===//
//
// Specialises a closure passed as the replacement function of
// `text.replace(/pattern/g, fn)`. The closure's only use is that argument, so
// the call is its one call site: lift() makes its captures leading parameters
// as for any lifted closure, and the call then passes the captured values in
// place of the closure, after the RegExp literal. The lowering spells it
//
//   ctnative::String.prototype.replace.call(text, ctnative::RegExp("[A-Z]", "g"),
//                                           std::bind_front(callback, captures...))
//
// Whether the receiver is a String is a type question; admission
// (Admission/StringCallbacks.cpp) asks it once types are known.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

#include <array>

namespace ctcompile::ctnative::lowering_detail {

namespace {
/// The RegExp literal's pattern and flags, read by EmitC/StringCallbacks.cpp.
constexpr llvm::StringLiteral kPattern = "ctnative.replace_pattern";
constexpr llvm::StringLiteral kFlags = "ctnative.replace_flags";
} // namespace

void closureLifter::stringCallbackCensus() {
    stringCallbacks.clear();
    for (ctjs::CreateClosureOp c : closures) {
        if (!c.getResult().hasOneUse()) { continue; }
        mlir::OpOperand & use = *c.getResult().getUses().begin();
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        // Operands are the callee, the receiver, the RegExp and the callback.
        if (!call || use.getOperandNumber() != 3 || call.getArgs().size() != 2) { continue; }
        auto read = ctjs::receiverMethod(call);
        const mlir::Value pattern = call.getArgs()[0];
        if (!read || read.getObject() != call.getReceiver() || !read.getResult().hasOneUse() ||
            ctjs::methodKey(call) != "replace" || !pattern.hasOneUse() ||
            !TypeInference::regExpLiteral(pattern)) {
            continue;
        }
        stringCallbacks[c.getOperation()] = call;
    }
}

std::optional<std::string> closureLifter::whyNotStringCallback(ctjs::CreateClosureOp c) {
    ctjs::CallOp call = stringCallbacks.lookup(c.getOperation());
    const std::string method = "String.prototype.replace";
    if (auto why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    const auto [pattern, flags] = *TypeInference::regExpLiteral(call.getArgs()[0]);
    if (const std::string why =
            TypeInference::replacePatternProblem(pattern, flags, ctjs::parameterCount(entry));
        !why.empty()) {
        return method + " " + why;
    }
    // The runtime calls the function with `this` undefined and no new.target.
    if (!entry.getArgument(ctjs::arg_receiver).use_empty()) {
        return "the callback of " + method + " reads `this`";
    }
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) {
        return "the callback of " + method + " reads new.target";
    }
    if (ctjs::readsRawArguments(target)) {
        return "the callback of " + method + " reads its `arguments`";
    }
    // The runtime holds the callback only for the call, but its captures are
    // copied into it, so a binding written after the capture is refused.
    for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
        if (slotIsCarried(c, i)) {
            return "the callback of " + method + " captures binding " + std::to_string(i) +
                   ", which is written after it is captured - the callback takes its captures "
                   "by value";
        }
    }
    if (auto why = whyCapturesDoNotLift(c)) { return why; }
    return whyLiftTailFails(c, target, std::array<mlir::Operation *, 1>{call.getOperation()});
}

void closureLifter::rewriteStringCallback(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                          liftReport & out) {
    ctjs::CallOp call = stringCallbacks.lookup(c.getOperation());
    const auto captured = liftedCaptures(c);
    // The call passes the RegExp, then the captured values where it passed the
    // closure.
    llvm::SmallVector<mlir::Value> arguments{call.getArgs()[0]};
    llvm::append_range(arguments, captured);
    call.getArgsMutable().assign(arguments);
    call->setAttr(kNativeStringCallback, mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()));
    // The lowering spells the literal after its constants are rewritten, so
    // it keeps their text here.
    const auto [pattern, flags] = *TypeInference::regExpLiteral(call.getArgs()[0]);
    call->setAttr(kPattern, mlir::StringAttr::get(context, pattern));
    call->setAttr(kFlags, mlir::StringAttr::get(context, flags));
    target->setAttr(kNativeStringCallback, mlir::UnitAttr::get(context));
    // No ctjs.call_direct names the callback, so DeadCodeAnalysis would treat
    // a private one as dead; public, its parameters reach setToEntryState,
    // which leaves them to the call (Inference/StringCallbacks.cpp).
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    markLifted(c, out, static_cast<unsigned>(captured.size()));
    ++out.calls;
}

} // namespace ctcompile::ctnative::lowering_detail
