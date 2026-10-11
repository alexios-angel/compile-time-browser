//===- ClosureLifting/BorrowedCallbacks.cpp - Borrowed callbacks -*- C++ -*-===//
//
// Lifts a closure passed to a parameter that its callee only calls, during the
// call, such as `this._setElementAttributes(el, "paddingRight", v => v + w)`
// whose callee calls its third parameter and keeps nothing. The callee is
// called with different closures from different call sites, so no one call
// site can receive the captures as a lift would pass them. Instead each
// closure stays a value, bound to its captures, and the callee borrows it for
// the call:
//
//   double callee(..., ctnative::function_ref<double(double)> callback);
//   callee(..., std::bind_front(callback_3, w));
//
// Nothing owns an environment, because the bound callable is a value in the
// calling frame (or a temporary of the call expression), and the parameter
// cannot outlive the call: it is only ever the callee of a call in its own
// function. The captures are copied when the callable is bound, so a binding
// written after the closure was made is refused rather than copied.
//
// Closure lifting decides the structure: every caller of the callee is
// visible, every one passes a closure to that parameter, the parameter is used
// only as the callee of a call, and each closure lifts. The types are later
// facts: type inference (Analysis/Inference/BorrowedCallbacks.cpp) types each
// callback from its closure and the calls through the parameter, and
// admission (Admission/BorrowedCallbacks.cpp) requires one C++ signature.
//
//===----------------------------------------------------------------------===//

#include "../../Analysis/ClosedValueFlow.h"
#include "ClosureLifter.h"

#include <array>

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// The callee entry argument `c` is passed to, when `c`'s one use is an
/// argument of a direct call of a function every caller of which is visible;
/// null otherwise.
mlir::BlockArgument borrowingParameter(ctjs::CreateClosureOp c) {
    if (!c.getResult().hasOneUse()) { return {}; }
    mlir::OpOperand & use = *c.getResult().getUses().begin();
    auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
    // Operands 0-2 are the receiver, new.target and callee; arguments follow,
    // in the order of the callee's entry arguments.
    if (!call || use.getOperandNumber() < ctjs::implicit_arguments) { return {}; }
    ctjs::FuncOp callee = call.getTarget();
    if (!closedValueFlow::closed(callee) ||
        use.getOperandNumber() >= callee.getBody().front().getNumArguments()) {
        return {};
    }
    return callee.getBody().front().getArgument(use.getOperandNumber());
}

/// Whether every use of `parameter` is the callee of a ctjs.call.
bool onlyCalled(mlir::BlockArgument parameter) {
    return !parameter.use_empty() && llvm::all_of(parameter.getUses(), [](mlir::OpOperand & use) {
        return llvm::isa<ctjs::CallOp>(use.getOwner()) && use.getOperandNumber() == 0;
    });
}

} // namespace

/// Groups the closures passed to each call-only parameter of a closed
/// function, and keeps the groups to which every caller passes a closure.
void closureLifter::borrowedCallbackCensus() {
    borrowedCallbacks.clear();
    llvm::MapVector<mlir::Value, llvm::SmallVector<ctjs::CreateClosureOp, 2>> passed;
    for (ctjs::CreateClosureOp c : closures) {
        if (lifted.contains(c.getOperation()) || isDeclarationClosure(c)) { continue; }
        if (mlir::BlockArgument parameter = borrowingParameter(c);
            parameter && onlyCalled(parameter)) {
            passed[parameter].push_back(c);
        }
    }
    for (auto & [value, group] : passed) {
        auto parameter = llvm::cast<mlir::BlockArgument>(value);
        auto callee = llvm::cast<ctjs::FuncOp>(parameter.getOwner()->getParentOp());
        // Every caller passes one of the group: a caller that passes anything
        // else (another function value, or `undefined` for a missing argument)
        // gives the parameter a value this rule did not see.
        unsigned callers = 0;
        bool every = true;
        if (auto uses = mlir::SymbolTable::getSymbolUses(callee.getOperation(), module)) {
            for (const mlir::SymbolTable::SymbolUse & use : *uses) {
                auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
                every &= call != nullptr;
                ++callers;
            }
        } else {
            every = false;
        }
        if (!every || callers != group.size()) { continue; }
        // One capture-free callback is the case specializeCallbacks makes a
        // direct call of, which also lets its result keep a type no
        // function_ref can carry (a returned method table, for one).
        if (group.size() == 1 && group.front().getUpvalues().empty()) { continue; }
        for (ctjs::CreateClosureOp c : group) { borrowedCallbacks[c.getOperation()] = group; }
    }
}

/// Why `c`, one closure of a borrowed group, cannot be lifted alone.
std::optional<std::string> closureLifter::whyNotBorrowedMember(ctjs::CreateClosureOp c) {
    if (auto why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    // A call through the parameter passes no receiver or new.target the
    // callback could read, and its argument count is fixed per call.
    if (!entry.getArgument(ctjs::arg_receiver).use_empty()) {
        return "the borrowed callback reads `this`";
    }
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) {
        return "the borrowed callback reads new.target";
    }
    if (ctjs::readsRawArguments(target)) { return "the borrowed callback reads its `arguments`"; }
    const unsigned declared = entry.getNumArguments() - ctjs::implicit_arguments;
    mlir::BlockArgument parameter = borrowingParameter(c);
    for (mlir::Operation * call : parameter.getUsers()) {
        const auto passed = static_cast<unsigned>(llvm::cast<ctjs::CallOp>(call).getArgs().size());
        if (passed != declared) {
            return "the borrowed callback declares " + std::to_string(declared) +
                   " parameters and a call through its parameter passes " + std::to_string(passed) +
                   " - one C++ signature needs the same count";
        }
    }
    // The bound callable holds copies of the captures, made when the closure
    // is: a binding written afterwards would need the variable's address.
    for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
        if (slotIsCarried(c, i)) {
            return "the borrowed callback captures binding " + std::to_string(i) +
                   ", which is written after it is captured - the callback takes its captures "
                   "by value";
        }
    }
    if (auto why = whyCapturesDoNotLift(c)) { return why; }
    // The closure is bound where it is made, so that is where the captures
    // have to have their values.
    return whyLiftTailFails(c, target, std::array<mlir::Operation *, 1>{c.getOperation()});
}

/// Why `c` is not lifted as a borrowed callback: its own reason, or that of
/// another closure passed to the same parameter, since the parameter has one
/// representation for all of them.
std::optional<std::string> closureLifter::whyNotBorrowedCallback(ctjs::CreateClosureOp c) {
    if (auto why = whyNotBorrowedMember(c)) { return why; }
    for (ctjs::CreateClosureOp other : borrowedCallbacks.lookup(c.getOperation())) {
        if (other == c) { continue; }
        if (auto why = whyNotBorrowedMember(other)) {
            return "another callback passed to the same parameter is not lifted: " + *why;
        }
    }
    return std::nullopt;
}

/// Keeps `c` as a value bound to its lifted captures and names `target` on it
/// and on every call through the parameter it is passed to. lift() has made
/// the captures `target`'s leading parameters.
void closureLifter::rewriteBorrowedCallback(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                            liftReport & out) {
    mlir::BlockArgument parameter = borrowingParameter(c);
    const auto captured = liftedCaptures(c);
    c.getUpvaluesMutable().assign(captured);
    c.removeEnclosingIndicesAttr();
    // Nothing reads the enclosing closure or `this` of a bound callback; with
    // them gone the creating frame does not read its own closure for it.
    mlir::OpBuilder at(c);
    const mlir::Value undefined = undefinedAt(at, c.getLoc());
    c.getEnclosingClosureMutable().assign(undefined);
    c.getEnclosingThisMutable().assign(undefined);
    const auto symbol = mlir::FlatSymbolRefAttr::get(target.getSymNameAttr());
    c->setAttr(kNativeBorrowedCallback, symbol);
    c->removeAttr(kNativeClosureReason);
    target->setAttr(kNativeBorrowedCallback, mlir::UnitAttr::get(context));
    for (mlir::Operation * call : parameter.getUsers()) {
        llvm::SmallVector<mlir::Attribute> callbacks;
        if (auto listed = call->getAttrOfType<mlir::ArrayAttr>(kNativeBorrowedCall)) {
            llvm::append_range(callbacks, listed.getValue());
        }
        callbacks.push_back(symbol);
        call->setAttr(kNativeBorrowedCall, mlir::ArrayAttr::get(context, callbacks));
    }
    // lift() made the callback private, but no ctjs.call_direct names it, and
    // MLIR's DeadCodeAnalysis treats a private function nothing calls as dead,
    // so type inference would never visit its body. Public, its parameters
    // reach TypeInference::setToEntryState, which leaves them to the closure
    // and the calls (Inference/BorrowedCallbacks.cpp).
    mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Public);
    ++out.closures;
    out.captures += static_cast<unsigned>(captured.size());
}

} // namespace ctcompile::ctnative::lowering_detail
