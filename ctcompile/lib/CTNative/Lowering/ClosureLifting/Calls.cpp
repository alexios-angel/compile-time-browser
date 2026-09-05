// ClosureLifting/Calls.cpp - native lowering implementation.
#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

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
        // THE SYMBOL MUST BE THIS CLOSURE'S OWN TARGET. A call_direct whose
        // callee value is this closure but whose symbol is another function
        // is not a call of it, and the resolver never builds one - asking
        // is what keeps that an invariant rather than an assumption.
        if (use.getOperandNumber() != 2 || !target || direct.getCallee() != target.getSymName()) {
            return {};
        }
        return {user, direct.getArgs(), direct.getReceiver()};
    }
    return {};
}

// The closure a call site dispatches on, in either shape, or null.
ctjs::CreateClosureOp closureLifter::closureCalledBy(mlir::Operation * user) {
    if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
        return call.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
    }
    if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
        return direct.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>();
    }
    return {};
}

// Its arguments, and the operand number argument 0 sits at.
mlir::ValueRange closureLifter::argsOfCallSite(mlir::Operation * user) {
    if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) { return call.getArgs(); }
    return llvm::cast<ctjs::CallDirectOp>(user).getArgs();
}

// THE TWO WALKS BOTH RULES NEED. `targetOf` reads `byIndex` and the
// local-function rule asks `passesNewTarget` of every call site it writes,
// and it runs before census() - so the two are here rather than inline, and
// census() calls this rather than repeating it.
void closureLifter::indexAndNewTargets() {
    module.walk([&](ctjs::FuncOp fn) {
        if (const std::optional<unsigned> index = functionIndexOf(fn)) {
            byIndex.try_emplace(*index, fn);
        }
    });
    module.walk([&](ctjs::PassNewTargetOp o) {
        if (auto holder = o->getParentOfType<ctjs::FuncOp>()) {
            passesNewTarget.insert(holder.getOperation());
        }
    });
}

void closureLifter::census() {
    indexAndNewTargets();
    module.walk([&](mlir::Operation * o) {
        auto holder = o->getParentOfType<ctjs::FuncOp>();
        if (llvm::isa<ctjs::StoreUpvalueOp>(o)) {
            if (holder) { mutatesUpvalue.insert(holder.getOperation()); }
        } else if (llvm::isa<ctjs::PassNewTargetOp>(o)) {
            if (holder) { passesNewTarget.insert(holder.getOperation()); }
        } else if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(o)) {
            closures.push_back(made);
        } else if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(o)) {
            objects.push_back(object);
        } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(o)) {
            allCalls.push_back(call);
        } else if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(o)) {
            // THE SITES --ctjs-resolve-globals ALREADY NAMED, kept beside
            // the unnamed ones because the object-argument census has to
            // reach both: a literal handed to a named call is in exactly
            // the position a literal handed to an unnamed one is.
            allDirectCalls.push_back(direct);
        } else if (auto built = llvm::dyn_cast<ctjs::ConstructOp>(o)) {
            allConstructs.push_back(built);
        }
    });
    // The fixpoint over the closure-target graph.
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
    // AND THE CELLS WITH ONE DOMINATING WRITE, LAST, because condition 4
    // asks `mutatesUpvalue` and the fixpoint above is what settles it.
    singleWriteCensus();
    // AND THE SHARED ONES AFTER THAT, because the by-value path has first
    // refusal: a cell singleWriteCensus took is copied into a parameter,
    // which costs no pointer and no indirection.
    sharedCellCensus();
}

} // namespace ctcompile::ctnative::lowering_detail
