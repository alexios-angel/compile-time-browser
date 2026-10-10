//===- ReturnedClosures.cpp - Lifting returned closures ------- -*- C++ -*-===//
//
// A closure returned from its creating function outlives that frame, so its
// captures cannot be pointers into it. This file proves which returned
// closures can still become direct calls (their captures copied into an
// environment value at creation) and rewrites their call sites.
//
//===----------------------------------------------------------------------===//
#include "../../Analysis/ClosedValueFlow.h"
#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {
/// Builds a plan for every closure that flows into a ctjs.return: the call
/// sites that invoke it and, when it cannot be lifted, the first reason why
/// (stored in `returnedClosures[c].reason`). Changes no IR.
void closureLifter::returnedClosureCensus() {
    closedValueFlow flow;
    flow.build(module);
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value>> families;
    llvm::DenseSet<mlir::Value> returnedFamilies;
    llvm::DenseMap<mlir::Operation *, unsigned> creations;
    for (mlir::Value value : flow.nodes()) {
        const auto root = flow.find(value);
        families[root].push_back(value);
        for (mlir::Operation * user : value.getUsers()) {
            if (llvm::isa<ctjs::ReturnOp>(user)) { returnedFamilies.insert(root); }
        }
    }
    for (ctjs::CreateClosureOp c : closures) { ++creations[targetOf(c)]; }
    for (ctjs::CreateClosureOp c : closures) {
        if (c->hasAttr(kNativeLifted) || !environmentTarget(c).empty() || isDeclarationClosure(c)) {
            continue;
        }
        const auto root = flow.find(c.getResult());
        if (!root || !returnedFamilies.contains(root)) { continue; }
        auto & plan = returnedClosures[c];
        auto reject = [&](llvm::StringRef why) {
            if (plan.reason.empty()) { plan.reason = ("returned closure " + why).str(); }
        };
        const auto target = targetOf(c);
        // Imported JavaScript has one creation site per function proto. Check
        // it anyway, because changing the target's signature affects every
        // creation site.
        if (creations.lookup(target) != 1) { reject("requires one creation site for its target"); }
        for (mlir::Value value : families[root]) {
            if (value == c.getResult()) {
                // The only concrete producer of this callable family.
            } else if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(value)) {
                auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
                if (!closedValueFlow::closed(fn) || arg.getOwner() != &fn.getBody().front() ||
                    arg.getArgNumber() < 3 || flow.callers[fn].empty()) {
                    reject("parameter requires a closed function with visible callers");
                }
            } else if (auto call = value.getDefiningOp<ctjs::CallDirectOp>()) {
                auto fn = call.getTarget();
                if (!closedValueFlow::closed(fn) || flow.returns[fn].empty()) {
                    reject("result requires a closed function with visible returns");
                }
            } else {
                reject("flow contains another callable or a non-callable producer");
            }
            for (mlir::OpOperand & use : value.getUses()) {
                auto * user = use.getOwner();
                if (auto ret = llvm::dyn_cast<ctjs::ReturnOp>(user)) {
                    if (!closedValueFlow::closed(ret->getParentOfType<ctjs::FuncOp>())) {
                        reject("return requires a closed function");
                    }
                    continue;
                }
                if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
                    // call_direct operands 0-2 are receiver, new.target and
                    // callee; JavaScript arguments start at 3.
                    if (use.getOperandNumber() >= 3) {
                        if (!closedValueFlow::closed(call.getTarget())) {
                            reject("argument requires a closed callee");
                        }
                        continue;
                    }
                    if (use.getOperandNumber() == 2 && call.getTarget() == target) {
                        plan.calls.push_back(user);
                        continue;
                    }
                }
                if (llvm::isa<ctjs::CallOp>(user) && use.getOperandNumber() == 0) {
                    plan.calls.push_back(user);
                    continue;
                }
                reject(("escapes or is inspected through `" + user->getName().getStringRef() + "`")
                           .str());
            }
        }
        for (ctjs::CallDirectOp call : flow.callers[target]) {
            if (!llvm::is_contained(plan.calls, call.getOperation())) {
                reject("target has a direct caller outside the proved callable flow");
            }
        }
        if (plan.calls.empty()) { reject("has no visible invocation"); }
    }
}

/// The first reason the returned closure `c` cannot be lifted, or nullopt.
std::optional<std::string> closureLifter::whyNotReturnedClosure(ctjs::CreateClosureOp c) {
    const auto & plan = returnedClosures.find(c)->second;
    if (!plan.reason.empty()) { return plan.reason; }
    if (auto why = whyCapturesDoNotLift(c)) { return why; }
    auto target = targetOf(c);
    auto & entry = target.getBody().front();
    if (!entry.getArgument(0).use_empty()) { return "returned closure reads `this`"; }
    if (!entry.getArgument(1).use_empty()) { return "returned closure reads new.target"; }
    if (auto why = whyOwnClosureEscapes(target)) { return why; }
    for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
        if (slotIsCarried(c, i)) {
            return "returned closure capture " + std::to_string(i) +
                   " is a mutable or late-initialized binding; it needs an owning shared cell";
        }
    }
    // A returned closure copies its captures at creation, unlike the local
    // lift, which can read a binding at each call. So a capture must already
    // be initialized at creation: a hoisted declaration must not silently
    // capture its later value, or a pointer to a dead frame.
    if (auto why = whyCapturesDoNotReach(c, c)) { return "returned closure: " + *why; }
    if (auto why = whyUpvalueReadsDoNotLift(c, target)) { return why; }
    for (mlir::Operation * site : plan.calls) {
        if (argsOfCallSite(site).size() > entry.getNumArguments() - ctjs::implicit_arguments) {
            return "returned closure call has surplus arguments with frame semantics";
        }
        auto caller = site->getParentOfType<ctjs::FuncOp>();
        if (caller && passesNewTarget.contains(caller)) {
            return "returned closure call sits in a function that passes new.target";
        }
    }
    return std::nullopt;
}

/// Rewrites `c` to carry its captures as an environment value and turns each
/// planned call site into a direct call to `target` that passes the captures
/// (read back with ctjs.load_upvalue) as leading arguments.
void closureLifter::liftReturnedClosure(ctjs::CreateClosureOp c, ctjs::FuncOp target,
                                        unsigned captures, unsigned parameters, liftReport & out,
                                        bool preserveReceiver) {
    llvm::SmallVector<mlir::Value> values;
    for (unsigned i = 0; i < captures; ++i) { values.push_back(liftedCapture(c, i)); }
    c.getUpvaluesMutable().assign(values);
    c.removeEnclosingIndicesAttr();
    c->setAttr(kNativeEnvironment, mlir::StringAttr::get(context, target.getSymName()));
    const bool stored = returnedClosures.find(c)->second.stored;
    if (stored) {
        c->setAttr(kNativeStoredCallable, mlir::UnitAttr::get(context));
        target->setAttr(kNativeStoredCallable, mlir::UnitAttr::get(context));
    }
    c->removeAttr(kNativeClosureReason);
    const auto valueType = ctjs::ValueType::get(context);
    for (mlir::Operation * site : returnedClosures.find(c)->second.calls) {
        mlir::OpBuilder at(site);
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(site);
        const auto closure =
            direct ? direct.getCalleeValue() : llvm::cast<ctjs::CallOp>(site).getCallee();
        const mlir::Value undefined = undefinedAt(at, site->getLoc());
        llvm::SmallVector<mlir::Value> args;
        for (unsigned i = 0; i < captures; ++i) {
            auto read = ctjs::LoadUpvalueOp::create(at, site->getLoc(), valueType, closure, i);
            read->setAttr(kNativeEnvironmentRead, mlir::UnitAttr::get(context));
            read->setAttr(kNativeEnvironment, c->getAttr(kNativeEnvironment));
            if (stored) { read->setAttr(kNativeStoredRead, mlir::UnitAttr::get(context)); }
            args.push_back(read.getResult());
        }
        auto call =
            directCall(at, site->getLoc(), target,
                       preserveReceiver ? (direct ? direct.getReceiver()
                                                  : llvm::cast<ctjs::CallOp>(site).getReceiver())
                                        : undefined,
                       closure, undefined, args, argsOfCallSite(site), captures + parameters);
        if (stored) {
            call->setAttr(kNativeStoredCall, at.getI32IntegerAttr(static_cast<int32_t>(captures)));
        }
        site->getResult(0).replaceAllUsesWith(call.getResult());
        site->erase();
        ++out.calls;
    }
    ++out.closures;
    out.captures += captures;
}

} // namespace ctcompile::ctnative::lowering_detail
