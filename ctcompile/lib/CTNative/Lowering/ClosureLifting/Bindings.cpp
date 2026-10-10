//===- ClosureLifting/Bindings.cpp - Local function bindings ----*- C++ -*-===//
//
// Rule 4 of the closure lifter: a local binding that holds exactly one
// function (`var f = function () {...}`) becomes direct calls of that
// function, and its box and capture slots are removed. The rule and its
// numbered conditions are documented in ClosureLifter.h.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

/// Does this box ever hold a function? The cheap half of the question, asked
/// first so that a box holding a number never collects a diagnostic about
/// closures, and so that `whyNotAFunctionBinding` has an entry for every cell
/// the refusal in whyNotABoundFunction can land on.
bool closureLifter::holdsAFunction(ctjs::CreateCellOp cell) {
    return llvm::any_of(cell.getResult().getUses(), [](mlir::OpOperand & use) {
        return classify(use) == cellUse::write && llvm::cast<ctjs::CellSetOp>(use.getOwner())
                                                      .getValue()
                                                      .getDefiningOp<ctjs::CreateClosureOp>();
    });
}

/// Condition 4 of rule 4 (see ClosureLifter.h) for one read: `read` is the
/// value the binding was read into (a `ctjs.cell_get` result in the frame that
/// owns the box, or a `ctjs.load_upvalue` result in a nested one), and every
/// use of it must be a call this rewrite can turn into a direct call at an
/// arity the callee's entry block accepts. Counts the calls in `calls`.
std::optional<std::string> closureLifter::whyReadIsNotACall(mlir::Value read, ctjs::FuncOp target,
                                                            unsigned & calls) {
    const unsigned parameters =
        target.getBody().front().getNumArguments() - ctjs::implicit_arguments;
    for (mlir::OpOperand & use : read.getUses()) {
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        if (!call || use.getOperandNumber() != 0) {
            return ("the name reaches `" + use.getOwner()->getName().getStringRef() +
                    "`, so the binding holds a function VALUE and this step makes none")
                .str();
        }
        // Arity must be checked here: CallDirectOp::verifySymbolUses requires
        // the operand count to equal the entry block's, so a call with surplus
        // arguments is refused now. A short call is padded with undefined, as
        // the VM's call opcode does (VM_CASE(call) in
        // ctbrowser/lib/Script/vm/run_loop.cpp).
        if (call.getArgs().size() < parameters && ctjs::readsRawArguments(target)) {
            return "a call passes " + std::to_string(call.getArgs().size()) + " argument(s) to " +
                   std::to_string(parameters) +
                   " parameter(s) and the callee reads its raw argument window, which the "
                   "pad would lengthen";
        }
        if (auto why = whyCallSiteDoesNotLift(call, call.getArgs().size(), parameters, "a call")) {
            return why;
        }
        ++calls;
    }
    return std::nullopt;
}

/// Checks condition 4 of rule 4 (see ClosureLifter.h) for capture `slot` of
/// closure `made`, and recursively for every slot of a nested closure that is
/// filled from it.
///
/// `made` holds the box at capture `slot`, so inside its target the binding
/// is `ctjs.load_upvalue slot`. The same questions the owning frame asked of
/// its own reads apply: nothing assigns it, every read is a call this pass can
/// rewrite, and no read goes through a closure it cannot name. In addition, a
/// closure made inside that target may fill one of its own slots from this one
/// (`enclosing_indices[i] == slot`); then the binding is one frame further in
/// and the same questions are asked of `(nested, i)`.
///
/// An inner level that fails refuses the whole binding, and the message names
/// which level failed. The box is one name and the slots are one chain: a
/// rewrite that took the outer slots and left an inner one would leave a
/// `ctjs.load_upvalue` naming a capture that no longer exists, which is a
/// fatal error in `removeCaptureSlots`.
///
/// Condition 3 does not need to be checked again for inner levels.
/// `writeReachesEveryRead` requires the store to dominate every use of every
/// closure that captured the box, and a closure made inside one of those
/// targets cannot run before its maker does.
///
/// `examined` makes the recursion terminate and keeps the removal list a set:
/// a (closure, slot) pair fully determines the answer, so meeting one twice
/// gives the same answer, and a target graph that led back to a closure
/// already on the chain would otherwise recurse forever.
std::optional<std::string> closureLifter::examineCapturedSlot(
    functionBinding & plan, ctjs::CreateClosureOp made, unsigned slot,
    llvm::DenseSet<std::pair<mlir::Operation *, unsigned>> & examined) {
    if (!examined.insert({made.getOperation(), slot}).second) { return std::nullopt; }
    ctjs::FuncOp holder = targetOf(made);
    if (!holder || holder.getBody().empty() || holder.getBody().front().getNumArguments() < 3) {
        return "capture " + std::to_string(slot) +
               " of it is taken by a closure whose target this tier cannot see";
    }
    mlir::Block & entry = holder.getBody().front();
    std::optional<std::string> bad;
    llvm::SmallVector<std::pair<ctjs::CreateClosureOp, unsigned>> inward;
    holder.getBody().walk([&](mlir::Operation * o) {
        if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(o)) {
            if (write.getClosure() != entry.getArgument(2)) {
                bad =
                    std::string{"a function that reads it writes an upvalue this step cannot name"};
            } else if (static_cast<unsigned>(write.getIndex()) == slot) {
                bad = std::string{"a function that reads it ASSIGNS the binding, so the "
                                  "name holds a variable and not one function"};
            }
            return;
        }
        if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(o)) {
            if (read.getClosure() != entry.getArgument(2)) {
                bad =
                    std::string{"a function that reads it reads an upvalue this step cannot name"};
            } else if (static_cast<unsigned>(read.getIndex()) == slot) {
                if (const std::optional<std::string> why =
                        whyReadIsNotACall(read.getResult(), plan.target, plan.calls)) {
                    bad = why;
                }
            }
            return;
        }
        // A nested closure that fills one of its slots from this one carries
        // the binding a frame further in. Collect every such (closure, slot)
        // pair here and recurse after the walk, so that a refusal at this
        // level is reported before one from a deeper level. One nested
        // closure may fill several of its slots from this one, so record each
        // index.
        if (auto nested = llvm::dyn_cast<ctjs::CreateClosureOp>(o)) {
            const auto captures = static_cast<unsigned>(nested.getUpvalues().size());
            for (unsigned i = 0; i < captures; ++i) {
                if (enclosingIndex(nested, i) == static_cast<std::int32_t>(slot)) {
                    inward.emplace_back(nested, i);
                }
            }
        }
    });
    if (bad) { return bad; }
    for (auto & [nested, index] : inward) {
        if (const std::optional<std::string> why =
                examineCapturedSlot(plan, nested, index, examined)) {
            return "a function one frame further in names the binding through its enclosing "
                   "closure, which did not lift: " +
                   *why;
        }
    }
    // Post-order, which is the deepest-first order the rewrite needs.
    plan.slots.push_back({made, slot});
    return std::nullopt;
}

/// Conditions 1 to 4 of rule 4 (stated beside `bindingClosures` in
/// ClosureLifter.h) for one box. Condition 5 is the fixpoint in
/// `bindLocalFunctions`, because it is a question about the other boxes.
std::optional<std::string> closureLifter::examineFunctionBinding(ctjs::CreateCellOp cell,
                                                                 functionBinding & plan) {
    plan.cell = cell;
    plan.owner = cell->getParentOfType<ctjs::FuncOp>();
    if (!plan.owner) { return std::string{"it is not inside a ctjs.func"}; }
    // Conditions 1 and 2 in one walk, because condition 1 is only known when
    // the walk ends.
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        switch (classify(use)) {
        // The box itself stored somewhere, not a value stored into it; the
        // censuses draw the same line, for the same reason.
        case cellUse::storedInto:
            return std::string{"the box itself is put inside another binding"};
        case cellUse::write:
            if (plan.store) { return std::string{"it is assigned more than once"}; }
            plan.store = llvm::cast<ctjs::CellSetOp>(user);
            continue;
        case cellUse::read: plan.reads.push_back(llvm::cast<ctjs::CellGetOp>(user)); continue;
        case cellUse::capture:
            plan.captured.emplace_back(llvm::cast<ctjs::CreateClosureOp>(user),
                                       use.getOperandNumber() - kFirstCapture);
            continue;
        case cellUse::other:
            return ("the box reaches `" + user->getName().getStringRef() +
                    "`, so something other than a name holds it")
                .str();
        }
    }
    if (!plan.store) { return std::string{"nothing is ever assigned to it"}; }
    plan.closure = plan.store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
    if (!plan.closure) {
        return std::string{"what is assigned to it is not a ctjs.create_closure of this frame"};
    }
    // The closure must be the binding and nothing else. A second use makes it
    // a function value (stored, returned, passed), which another rule handles
    // with a message that names it.
    if (!plan.closure.getResult().hasOneUse()) {
        return std::string{"the closure it holds is used as a value somewhere else as well"};
    }
    if (const std::optional<std::string> why = whyTargetIsNotLiftable(plan.closure)) { return why; }
    plan.target = targetOf(plan.closure);
    // Condition 3: the single store must reach every read. This reuses
    // `writeReachesEveryRead`, which rules 1 and 3 also use; all rely on no
    // read of the binding happening on a path that skipped the store.
    if (!writeReachesEveryRead(cell, plan.store)) {
        return std::string{
            "its one assignment does not reach every read of it, and a read before it "
            "yields the undefined the binding was hoisted with"};
    }
    for (ctjs::CellGetOp read : plan.reads) {
        if (const std::optional<std::string> why =
                whyReadIsNotACall(read.getResult(), plan.target, plan.calls)) {
            return why;
        }
    }
    // Condition 4 for reads in nested functions, following the binding through
    // every level of captures that forwards it.
    llvm::DenseSet<std::pair<mlir::Operation *, unsigned>> examined;
    for (auto & [made, slot] : plan.captured) {
        if (const std::optional<std::string> why =
                examineCapturedSlot(plan, made, slot, examined)) {
            return why;
        }
    }
    // A name nothing calls is not worth erasing a box for, and lifting its
    // target would mark private a function with no visible caller, which
    // DeadCodeAnalysis treats as dead (every type in it becomes `<unvisited>`).
    if (plan.calls == 0) { return std::string{"nothing calls it"}; }
    return std::nullopt;
}

/// Rewrites a call through the name into a ctjs.call_direct, in a frame the
/// closure value cannot reach.
///
/// `$callee_value` is `undefined`, as in the method and constructor arms, and
/// here also because the closure is defined in another ctjs.func, which SSA
/// cannot name from this one. So `lift()` never sees this site (it walks the
/// uses of the closure value). The site is recorded so the lift can prepend
/// any data captures from the sibling caller's existing capture parameters.
void closureLifter::makeBoundCallDirect(ctjs::CallOp call, ctjs::FuncOp target,
                                        ctjs::CreateClosureOp holder) {
    const unsigned parameters =
        target.getBody().front().getNumArguments() - ctjs::implicit_arguments;
    mlir::OpBuilder at(call);
    const mlir::Value undefined = undefinedAt(at, call.getLoc());
    auto direct = directCall(at, call.getLoc(), target, call.getReceiver(), undefined, undefined,
                             {}, call.getArgs(), parameters);
    boundCaptureCalls[target.getOperation()].push_back({direct, holder});
    call.getResult().replaceAllUsesWith(direct.getResult());
    call.erase();
}

/// Removes the capture slots the binding occupied, in place.
///
/// In place rather than rebuilt: `plans` and `bindingClosures` hold
/// ctjs.create_closure pointers, and a rebuilt operation is a different one,
/// so every reader would hold a dangling pointer and the fixpoint's verdicts
/// would be about operations that no longer exist.
///
/// Four things move together, and leaving any one behind is a verifier error
/// rather than a wrong answer: the operand, the parallel `enclosing_indices`
/// entry, the target's `upvalue_count`, and every upvalue index past the hole
/// (in the target's own reads and writes and in the `enclosing_indices` of
/// the closures it makes).
void closureLifter::removeCaptureSlots(ctjs::CreateClosureOp c, llvm::ArrayRef<unsigned> slots) {
    ctjs::FuncOp target = targetOf(c);
    const auto captures = static_cast<unsigned>(c.getUpvalues().size());
    mlir::Builder builder(context);
    llvm::SmallVector<bool> gone(captures, false);
    for (const unsigned slot : slots) { gone[slot] = true; }
    llvm::SmallVector<std::int32_t> renumbered(captures, -1);
    unsigned kept = 0;
    for (unsigned i = 0; i < captures; ++i) {
        if (!gone[i]) { renumbered[i] = static_cast<std::int32_t>(kept++); }
    }
    if (const mlir::DenseI32ArrayAttr indices = c.getEnclosingIndicesAttr()) {
        llvm::SmallVector<std::int32_t> rest;
        for (unsigned i = 0; i < captures; ++i) {
            if (!gone[i]) { rest.push_back(indices[i]); }
        }
        if (rest.empty()) {
            c->removeAttr("enclosing_indices");
        } else {
            c->setAttr("enclosing_indices", builder.getDenseI32ArrayAttr(rest));
        }
    }
    llvm::BitVector drop(c->getNumOperands(), false);
    for (unsigned i = 0; i < captures; ++i) {
        if (gone[i]) { drop.set(kFirstCapture + i); }
    }
    c->eraseOperands(drop);
    target->setAttr("upvalue_count", builder.getI32IntegerAttr(static_cast<int>(kept)));
    const auto move = [&](std::int32_t index) -> std::int32_t {
        if (index < 0 || static_cast<unsigned>(index) >= captures ||
            renumbered[static_cast<unsigned>(index)] < 0) {
            llvm::report_fatal_error(
                llvm::Twine("ctnative lowering: `") + target.getSymName() +
                "` still names upvalue " + llvm::Twine(index) +
                ", which the local-function rule removed - examineFunctionBinding admitted a "
                "read of the binding that the rewrite did not replace");
        }
        return renumbered[static_cast<unsigned>(index)];
    };
    target.getBody().walk([&](mlir::Operation * o) {
        if (llvm::isa<ctjs::LoadUpvalueOp, ctjs::StoreUpvalueOp>(o)) {
            const auto index = o->getAttrOfType<mlir::IntegerAttr>("index").getInt();
            o->setAttr("index", builder.getI32IntegerAttr(move(static_cast<std::int32_t>(index))));
            return;
        }
        if (auto nested = llvm::dyn_cast<ctjs::CreateClosureOp>(o)) {
            const mlir::DenseI32ArrayAttr indices = nested.getEnclosingIndicesAttr();
            if (!indices) { return; }
            llvm::SmallVector<std::int32_t> moved;
            for (const std::int32_t index : indices.asArrayRef()) {
                moved.push_back(index < 0 ? index : move(index));
            }
            o->setAttr("enclosing_indices", builder.getDenseI32ArrayAttr(moved));
        }
    });
}

/// Rewrites every local binding that holds exactly one function (`var f =
/// function () {...}`) so that each call through the name becomes a direct
/// call of the target, then removes the box. Runs once, before `census()`.
/// With `probeSiblings`, changes no IR and only reports whether some binding
/// would need sibling captures. Otherwise returns whether any binding target
/// takes sibling captures (`siblingBindingTargets`).
bool closureLifter::bindLocalFunctions(liftReport & out, bool probeSiblings) {
    llvm::MapVector<mlir::Operation *, functionBinding> plans;
    module.walk([&](ctjs::CreateCellOp cell) {
        if (!holdsAFunction(cell)) { return; }
        // A static cell is read by name from other functions, which this
        // rewrite would not see, so its box has to stay. whyNotABoundFunction
        // asks for this reason when a closure stored into it is refused.
        if (cell->hasAttr(kNativeStaticCell)) {
            whyNotAFunctionBinding[cell.getOperation()] =
                "it is a static variable of a function that runs once, which other functions "
                "read by name";
            return;
        }
        functionBinding plan;
        if (const std::optional<std::string> why = examineFunctionBinding(cell, plan)) {
            whyNotAFunctionBinding[cell.getOperation()] = *why;
            return;
        }
        plans.insert({cell.getOperation(), plan});
    });
    // Condition 5, as a greatest fixpoint: start by accepting every candidate
    // and drop those whose closure captures something this rewrite does not
    // erase. Greatest rather than least, because mutual recursion (`var a =
    // function () { b(); }; var b = function () { a(); };`) needs each binding
    // to accept the other, and a least fixpoint would accept neither.
    //
    // Only a binding read from another frame is checked. A name called only
    // in the frame that wrote it keeps its closure value in scope, so the
    // ordinary closure lift prepends the captures at those calls.
    llvm::DenseSet<mlir::Operation *> taken;
    for (const auto & entry : plans) { taken.insert(entry.first); }
    for (bool changed = true; changed;) {
        changed = false;
        for (auto & entry : plans) {
            if (!taken.contains(entry.first)) { continue; }
            functionBinding & plan = entry.second;
            if (plan.captured.empty()) { continue; }
            const auto captures = static_cast<unsigned>(plan.closure.getUpvalues().size());
            for (unsigned j = 0; j < captures; ++j) {
                auto held = plan.closure.getUpvalues()[j].getDefiningOp<ctjs::CreateCellOp>();
                if (held && taken.contains(held.getOperation())) { continue; }
                if ((allowSiblingCaptures || probeSiblings) &&
                    llvm::all_of(plan.slots, [&](const functionBinding::capturedSlot & slot) {
                        return siblingCaptureSlot(plan.closure, j, slot.made).has_value();
                    })) {
                    continue;
                }
                whyNotAFunctionBinding[entry.first] =
                    "it is read from inside another function, and capture " + std::to_string(j) +
                    " of the function it holds is not a binding this step erases - a call out "
                    "there has nothing to prepend the capture at";
                taken.erase(entry.first);
                changed = true;
                break;
            }
        }
    }
    // A probe changes no IR. Only candidates that still need data captures
    // after callable-slot removal require a speculative whole-lift attempt.
    for (auto & entry : plans) {
        functionBinding & plan = entry.second;
        if (!taken.contains(entry.first) || plan.captured.empty() ||
            llvm::all_of(plan.closure.getUpvalues(), [&](mlir::Value capture) {
                auto cell = capture.getDefiningOp<ctjs::CreateCellOp>();
                return cell && taken.contains(cell.getOperation());
            })) {
            continue;
        }
        if (probeSiblings) { return true; }
        siblingBindingTargets.insert(plan.target.getOperation());
    }
    if (probeSiblings) { return false; }
    // Pass A: every read becomes a call of the target.
    llvm::MapVector<mlir::Operation *, slotRemoval> removals;
    for (auto & entry : plans) {
        if (!taken.contains(entry.first)) { continue; }
        functionBinding & plan = entry.second;
        bindingClosures.insert(plan.closure.getOperation());
        if (auto id = entry.first->getAttrOfType<mlir::IntegerAttr>(kTrialCell)) {
            boundTrialCells.insert(id.getInt());
        }
        // In the frame that owns the box the closure value is in scope, so each
        // read is replaced by it and the ordinary closure lift (`lift()`)
        // rewrites the call, including captures, arity and new.target. That is
        // why this arm writes no ctjs.call_direct itself, and why a binding
        // called only here may hold a closure with captures.
        for (ctjs::CellGetOp read : plan.reads) {
            read.getResult().replaceAllUsesWith(plan.closure.getResult());
            read.erase();
        }
        for (auto & [made, slot] : plan.slots) {
            ctjs::FuncOp holder = targetOf(made);
            llvm::SmallVector<ctjs::LoadUpvalueOp> named;
            holder.getBody().walk([&](ctjs::LoadUpvalueOp read) {
                if (static_cast<unsigned>(read.getIndex()) == slot) { named.push_back(read); }
            });
            for (ctjs::LoadUpvalueOp read : named) {
                llvm::SmallVector<mlir::Operation *> sites(read.getResult().getUsers().begin(),
                                                           read.getResult().getUsers().end());
                for (mlir::Operation * site : sites) {
                    // Condition 4 guarantees every use is a call. Check it
                    // anyway: an unchecked cast would crash silently in a
                    // Release build instead of naming the broken claim.
                    auto call = llvm::dyn_cast<ctjs::CallOp>(site);
                    if (!call) {
                        llvm::report_fatal_error(
                            llvm::Twine("ctnative lowering: a local binding holding `") +
                            plan.target.getSymName() + "` reaches `" +
                            site->getName().getStringRef() +
                            "`, which is not a call - whyReadIsNotACall admitted a use of the "
                            "name that this step cannot rewrite, and the binding is a "
                            "function VALUE this tier cannot spell");
                    }
                    makeBoundCallDirect(call, plan.target, made);
                    // Count both. tools/check/native-claims.py reads `calls`
                    // from the "rewrote N call(s)" remark as the number of
                    // ctjs.call_direct this pass made.
                    ++out.calls;
                    ++out.bound;
                }
                read.erase();
            }
            slotRemoval & removal = removals[made.getOperation()];
            removal.slots.push_back(slot);
        }
        ++out.bindings;
    }
    // Pass B: remove the slots those reads came through, one rewrite per
    // closure however many of its slots held a binding, deepest first.
    //
    // The order matters for correctness. `removeCaptureSlots` renumbers, for
    // the closure it is given, every `enclosing_indices` entry of every
    // closure the target makes, and its `move()` has no image for an index
    // that has just been deleted. So a closure that fills a slot from another
    // closure's slot has to be rewritten first; its entry naming that slot is
    // then gone and the outer renumbering meets only surviving indices.
    //
    // Sort by the closure's own nesting depth, not by the depth of any slot it
    // holds: one closure can hold bindings that travelled different distances,
    // so neither the minimum nor the maximum slot depth orders closures
    // correctly. For example, with the minimum:
    //
    //     var w = function (k) { return k + 3; };
    //     var q = function (k) { return k + 4; };
    //     var mid = function (k) {
    //         var p = function (j) { return j + 5; };
    //         var inner = function (m) { return q(m) + p(m); };
    //         return w(k) + inner(k);
    //     };
    //
    // `mid` holds `w` at depth 0 and `inner` holds `p` at depth 0, so both
    // tie and `move()` aborts on the slot `inner` still names.
    //
    // A closure made inside another closure's target is strictly deeper than
    // its maker. Because ctjs.func is IsolatedFromAbove and all functions are
    // module-level siblings, nesting is read from the closure-creation tree:
    // the target of a create_closure sits one frame inside the function that
    // makes it.
    llvm::DenseMap<mlir::Operation *, mlir::Operation *> makerOf;
    module.walk([&](ctjs::CreateClosureOp c) {
        ctjs::FuncOp target = targetOf(c);
        auto in = c->getParentOfType<ctjs::FuncOp>();
        if (target && in) { makerOf[target.getOperation()] = in.getOperation(); }
    });
    llvm::DenseMap<mlir::Operation *, unsigned> funcDepth;
    const auto nestingOf = [&](mlir::Operation * made) -> unsigned {
        auto in = made->getParentOfType<ctjs::FuncOp>();
        if (!in) { return 0; }
        llvm::SmallVector<mlir::Operation *> path;
        mlir::Operation * at = in.getOperation();
        // A cycle is not reachable, and this stops rather than hangs if one
        // ever is: the maker edge runs from a target to the function that
        // makes it, and a function cannot be made inside itself.
        llvm::SmallPtrSet<mlir::Operation *, 8> seen;
        while (at && !funcDepth.count(at) && seen.insert(at).second) {
            path.push_back(at);
            at = makerOf.lookup(at);
        }
        unsigned depth = at ? funcDepth.lookup(at) : 0;
        for (mlir::Operation * step : llvm::reverse(path)) { funcDepth[step] = ++depth; }
        return funcDepth.lookup(in.getOperation());
    };
    llvm::SmallVector<std::pair<mlir::Operation *, slotRemoval *>> ordered;
    for (auto & entry : removals) { ordered.emplace_back(entry.first, &entry.second); }
    llvm::stable_sort(ordered, [&](const auto & left, const auto & right) {
        return nestingOf(left.first) > nestingOf(right.first);
    });
    for (auto & [made, removal] : ordered) {
        removeCaptureSlots(llvm::cast<ctjs::CreateClosureOp>(made), removal->slots);
    }
    // Pass C: erase the store and the box, which nothing uses now.
    for (auto & entry : plans) {
        if (!taken.contains(entry.first)) { continue; }
        functionBinding & plan = entry.second;
        // The store first, because it is itself a use of the box; then the
        // box must have no uses left. Conditions 1 and 2 claim this, and here
        // it is asserted rather than assumed.
        plan.store.erase();
        if (!plan.cell.getResult().use_empty()) {
            llvm::report_fatal_error(
                "ctnative lowering: a local function binding still has a use after its reads "
                "and its capture slots were rewritten - examineFunctionBinding admitted a use "
                "the rewrite does not remove");
        }
        plan.cell.erase();
    }
    return !siblingBindingTargets.empty();
}

/// Why a closure put into a local binding is not turned into direct calls
/// (the local-binding counterpart of `whyNotAMethodField`). "It reaches
/// `ctjs.cell_set`" names the mechanism, not the obstacle; the obstacle is
/// always one of rule 4's conditions.
std::string closureLifter::whyNotABoundFunction(ctjs::CellSetOp store) {
    auto cell = store.getCell().getDefiningOp<ctjs::CreateCellOp>();
    if (!cell) {
        // Not a `ctjs.create_cell` of this frame: the box is a pointer
        // parameter (rule 2 turns a `ctjs.store_upvalue` into this shape),
        // and a binding written through a pointer is a variable, not one
        // function.
        return "it is written into a binding this tier carries by pointer, so the name holds "
               "a variable and not one function";
    }
    const auto found = whyNotAFunctionBinding.find(cell.getOperation());
    if (found == whyNotAFunctionBinding.end()) {
        llvm::report_fatal_error(
            "ctnative lowering: a ctjs.create_closure is stored into a ctjs.create_cell for "
            "which the local-function rule wrote no reason - bindLocalFunctions and this "
            "refusal have drifted apart");
    }
    return "it is the value of a local binding this tier cannot call directly: " + found->second;
}

} // namespace ctcompile::ctnative::lowering_detail
