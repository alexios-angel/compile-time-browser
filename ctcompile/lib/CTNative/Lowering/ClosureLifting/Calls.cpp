//===- ClosureLifting/Calls.cpp - Call sites and the lifter census --------===//
//
// Call-site helpers shared by the closure-lifting rules, and census(), which
// records every closure, object, call and constructor site and computes the
// cell verdicts before any rewrite runs.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

/// The ctjs.func that closure `c` creates, or null if it is not in this module.
ctjs::FuncOp closureLifter::targetOf(ctjs::CreateClosureOp c) {
    return byIndex.lookup(static_cast<unsigned>(c.getFunction()));
}

closureLifter::closureCall closureLifter::callSiteOf(mlir::OpOperand & use, ctjs::FuncOp target) {
    mlir::Operation * user = use.getOwner();
    if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
        if (use.getOperandNumber() != 0) { return {}; }
        return {user, call.getArgs(), call.getReceiver()};
    }
    if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
        // The symbol must be this closure's own target. A call_direct whose
        // callee value is this closure but whose symbol is another function
        // is not a call of it. The resolver never builds one; checking keeps
        // that an enforced invariant rather than an assumption.
        if (use.getOperandNumber() != 2 || !target || direct.getCallee() != target.getSymName()) {
            return {};
        }
        return {user, direct.getArgs(), direct.getReceiver()};
    }
    return {};
}

/// The closure a call site dispatches on, in either shape, or null.
ctjs::CreateClosureOp closureLifter::closureCalledBy(mlir::Operation * user) {
    if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
        return call.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
    }
    if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
        if (auto load = direct.getCalleeValue().getDefiningOp<ctjs::LoadGlobalOp>()) {
            auto made = objectArgumentDeclarations.lookup(load.getName());
            return made && targetOf(made) == direct.getTarget() ? made : ctjs::CreateClosureOp{};
        }
        if (auto made = direct.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>()) {
            return targetOf(made) == direct.getTarget() ? made : ctjs::CreateClosureOp{};
        }
        // bindLocalFunctions proved these calls before erasing the captured
        // callable. Its target and recorded site are the identity; input annotations
        // or an arbitrary callee value cannot substitute for that proof.
        if (!ctjs::isUndefinedConstant(direct.getCalleeValue())) { return {}; }
        auto found = uniqueClosureByTarget.lookup(direct.getTarget());
        if (!found || !bindingClosures.contains(found.getOperation())) { return {}; }
        if (targetOf(found).getUpvalueCount() == 0) { return found; }
        const auto sites = boundCaptureCalls.find(direct.getTarget().getOperation());
        return sites != boundCaptureCalls.end() && llvm::any_of(sites->second,
                                                                [&](const boundCaptureCall & site) {
                                                                    return site.call == direct;
                                                                })
                   ? found
                   : ctjs::CreateClosureOp{};
    }
    return {};
}

/// The JavaScript arguments of a ctjs.call or ctjs.call_direct site.
mlir::ValueRange closureLifter::argsOfCallSite(mlir::Operation * user) {
    if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) { return call.getArgs(); }
    return llvm::cast<ctjs::CallDirectOp>(user).getArgs();
}

/// Builds `byIndex` and `passesNewTarget`. The local-function rule needs both
/// before census() runs, so they live here and census() calls this too.
void closureLifter::indexAndNewTargets() {
    module.walk([&](ctjs::FuncOp fn) {
        if (const std::optional<unsigned> index = functionIndex(fn)) {
            byIndex.try_emplace(*index, fn);
        }
    });
    module.walk([&](ctjs::PassNewTargetOp o) {
        if (auto holder = o->getParentOfType<ctjs::FuncOp>()) {
            passesNewTarget.insert(holder.getOperation());
        }
    });
}

/// Records every closure, object literal, call and `new` site, computes
/// `mutatesUpvalue`, and runs the single-write and shared-cell censuses. Runs
/// before any rewrite.
void closureLifter::census() {
    indexAndNewTargets();
    // Before any rule asks about a prototype: the class census marks the
    // setup operations those rules skip.
    classCensus();
    module.walk([&](mlir::Operation * o) {
        auto holder = o->getParentOfType<ctjs::FuncOp>();
        if (llvm::isa<ctjs::StoreUpvalueOp>(o)) {
            if (holder) { mutatesUpvalue.insert(holder.getOperation()); }
        } else if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(o)) {
            closures.push_back(made);
        } else if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(o)) {
            objects.push_back(object);
        } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(o)) {
            allCalls.push_back(call);
        } else if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(o)) {
            // Sites --ctjs-resolve-globals already named are kept beside the
            // unnamed ones because the object-argument census has to reach
            // both: a literal passed to a named call is in the same position
            // as one passed to an unnamed call.
            allDirectCalls.push_back(direct);
        } else if (auto built = llvm::dyn_cast<ctjs::ConstructOp>(o)) {
            allConstructs.push_back(built);
        }
    });
    for (ctjs::CreateClosureOp made : closures) {
        if (auto target = targetOf(made)) {
            auto [at, unique] = uniqueClosureByTarget.try_emplace(target, made);
            if (!unique) { at->second = {}; }
        }
    }
    // The fixpoint over the closure-target graph: the maker of a closure whose
    // target writes an upvalue may write one of its own slots too.
    for (bool changed = true; changed;) {
        changed = false;
        for (ctjs::CreateClosureOp c : closures) {
            ctjs::FuncOp target = targetOf(c);
            if (!target || !mutatesUpvalue.contains(target.getOperation())) { continue; }
            auto maker = c->getParentOfType<ctjs::FuncOp>();
            if (!maker) { continue; }
            changed |= mutatesUpvalue.insert(maker.getOperation()).second;
        }
    }
    // Cells with one dominating write are censused after the fixpoint, because
    // singleWriteCensus skips targets in `mutatesUpvalue` and the fixpoint
    // above is what completes that set.
    singleWriteCensus();
    // Shared cells come after that, because the by-value path has first
    // choice: a cell singleWriteCensus took is copied into a parameter,
    // which costs no pointer and no indirection.
    sharedCellCensus();
}

} // namespace ctcompile::ctnative::lowering_detail
