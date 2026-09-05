// ClosureLifting/Bindings.cpp - native lowering implementation.
#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

// DOES THIS BOX EVER HOLD A FUNCTION? The cheap half of the question, asked
// first so that a box holding a number never collects a diagnostic about
// closures - and so that `whyNotAFunctionBinding` has an entry for every
// cell the refusal below can land on.
bool closureLifter::holdsAFunction(ctjs::CreateCellOp cell) {
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
        if (write != nullptr && use.getOperandNumber() == 0 &&
            write.getValue().getDefiningOp<ctjs::CreateClosureOp>() != nullptr) {
            return true;
        }
    }
    return false;
}

// A BODY THAT READS ITS RAW ARGUMENT WINDOW, which is the one thing the pad
// below may not change: `op::call` fills a short call's REGISTERS with
// undefined, and `make_arguments_object` copies the raw window whose length
// is argc - so `function f(a, b) { return arguments.length; } f(1)` is 1 in
// the interpreter and would be 2 through a padded direct call. The same
// question ResolveGlobals asks (`reads_raw_arguments`), asked here because
// this step writes call sites of its own.
bool closureLifter::readsRawArguments(ctjs::FuncOp target) {
    bool reads = false;
    target.walk([&](mlir::Operation * o) {
        if (llvm::isa<ctjs::MakeArgumentsOp, ctjs::GatherRestOp>(o)) {
            reads = true;
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    return reads;
}

// CONDITION 4, ASKED OF ONE READ. `read` is the value the binding was read
// into - a `ctjs.cell_get` result in the frame that owns the box, a
// `ctjs.load_upvalue` result one frame in - and every use of it has to be a
// call this step can write, at an arity the callee's entry block accepts.
std::optional<std::string> closureLifter::whyReadIsNotACall(mlir::Value read, ctjs::FuncOp target,
                                                            unsigned & calls) {
    const unsigned parameters = target.getBody().front().getNumArguments() - 3;
    for (mlir::OpOperand & use : read.getUses()) {
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        if (!call || use.getOperandNumber() != 0) {
            return ("the name reaches `" + use.getOwner()->getName().getStringRef() +
                    "`, so the binding holds a function VALUE and this step makes none")
                .str();
        }
        // ARITY IS A HARD VERIFIER FAILURE AND NOT A LATER REFUSAL.
        // CallDirectOp::verifySymbolUses holds the operand count equal to
        // the entry block's, so a surplus call has to be refused HERE; a
        // short one is padded, as VM_CASE(call) pads.
        if (call.getArgs().size() > parameters) {
            return "a call passes " + std::to_string(call.getArgs().size()) + " argument(s) to " +
                   std::to_string(parameters) + " parameter(s) - the surplus has frame semantics";
        }
        if (call.getArgs().size() < parameters && readsRawArguments(target)) {
            return "a call passes " + std::to_string(call.getArgs().size()) + " argument(s) to " +
                   std::to_string(parameters) +
                   " parameter(s) and the callee reads its raw argument window, which the "
                   "pad would lengthen";
        }
        auto caller = call->getParentOfType<ctjs::FuncOp>();
        if (caller && passesNewTarget.contains(caller.getOperation())) {
            return std::string{"a call of it sits in a function that passes new.target"};
        }
        ++calls;
    }
    return std::nullopt;
}

// CONDITION 4 OF ONE CAPTURE SLOT, AND OF EVERY SLOT THE BINDING TRAVELS
// ON TO - PHASE 59 SLICE 2 STEP 5.
//
// `made` holds the box at capture `slot`, so inside its target the binding
// is `ctjs.load_upvalue slot` and the same three questions apply that the
// owning frame already asked of its own reads: nothing ASSIGNS it, every
// read of it is a call this tier can write, and no read goes through a
// closure this step cannot name. A FOURTH ANSWER IS NEW here: a closure
// made inside that target may fill a slot of its OWN from this one -
// `enclosing_indices[i] == slot` is exactly that, slice 1b - and then the
// binding is one frame further in and the same questions are asked of
// `(nested, i)`.
//
// WHAT USED TO STAND HERE WAS A REFUSAL, ON TWO GROUNDS, AND NEITHER
// SURVIVED BEING CHECKED. "Removing the slot renumbers every capture past
// it" describes machinery `removeCaptureSlots` has had all along - its walk
// renumbers a nested closure's `enclosing_indices` beside the target's own
// reads - so renumbering was never what was missing; what actually stops a
// removal is that the REMOVED index has no image, which is that function's
// `move()` fatal and a different sentence. And "there is no call site out
// here to move anything to" stopped being true when step 4 landed:
// `makeBoundCallDirect` writes a `ctjs.call_direct` in a frame the closure
// VALUE cannot reach, which is what its cross-frame calls on bootstrap are.
//
// AN INNER LEVEL THAT FAILS REFUSES THE WHOLE BINDING, and names which
// level failed. The box is one name and the slots are one chain: a rewrite
// that took the outer slots and left an inner one would leave a
// `ctjs.load_upvalue` naming a capture that no longer exists, which is
// `removeCaptureSlots`'s fatal rather than an answer.
//
// CONDITION 3 IS NOT ASKED AGAIN INWARDS, AND DOES NOT NEED TO BE.
// `writeReachesEveryRead` requires the store to dominate every use of every
// closure that captured the box, and a closure made INSIDE one of those
// targets cannot run before its maker does - every execution of it follows
// a call of a closure the store already dominates.
//
// `examined` MAKES IT TERMINATE AND KEEPS THE REMOVAL LIST A SET. The pair
// is the whole of the question - which body is walked, and which index is
// read in it - so meeting one twice is the same answer twice, and a target
// graph that led back to a closure already on the chain would otherwise
// recurse for ever.
std::optional<std::string> closureLifter::examineCapturedSlot(
    functionBinding & plan, ctjs::CreateClosureOp made, unsigned slot, unsigned depth,
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
        // AND THE SLOT NAMED FROM ONE FRAME FURTHER IN, WHICH IS NOW A
        // RECURSION AND NOT A REFUSAL. Collected here and walked after,
        // because the walk is not the place to recurse: `bad` is only known
        // when the walk ends, and an inner refusal reported before an outer
        // one would name the wrong frame. Every entry is taken rather than
        // the first - one nested closure may fill two of its own slots from
        // this one, and `is_contained` answered the old question while this
        // one needs WHICH slot.
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
                examineCapturedSlot(plan, nested, index, depth + 1, examined)) {
            return "a function one frame further in names the binding through its enclosing "
                   "closure, which did not lift: " +
                   *why;
        }
    }
    // POST-ORDER, WHICH IS THE DEEPEST-FIRST ORDER THE REWRITE NEEDS.
    plan.slots.push_back({made, slot, depth});
    return std::nullopt;
}

// CONDITIONS 1 TO 4 OF THE RULE STATED BESIDE `bindingClosures`, one
// sentence each, over one box. Condition 5 is the fixpoint in
// `bindLocalFunctions`, because it is a question about the OTHER boxes.
std::optional<std::string> closureLifter::examineFunctionBinding(ctjs::CreateCellOp cell,
                                                                 functionBinding & plan) {
    plan.cell = cell;
    plan.owner = cell->getParentOfType<ctjs::FuncOp>();
    if (!plan.owner) { return std::string{"it is not inside a ctjs.func"}; }
    // CONDITIONS 1 AND 2, IN ONE WALK, because condition 1 is only known
    // when the walk ends.
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(user)) {
            // THE BOX, NOT THE VALUE PUT IN ONE - the same line the two
            // censuses above draw, and for the same reason.
            if (use.getOperandNumber() != 0) {
                return std::string{"the box itself is put inside another binding"};
            }
            if (plan.store) { return std::string{"it is assigned more than once"}; }
            plan.store = write;
            continue;
        }
        if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) {
            plan.reads.push_back(llvm::cast<ctjs::CellGetOp>(user));
            continue;
        }
        if (llvm::isa<ctjs::CreateClosureOp>(user) && use.getOperandNumber() >= kFirstCapture) {
            plan.captured.emplace_back(llvm::cast<ctjs::CreateClosureOp>(user),
                                       use.getOperandNumber() - kFirstCapture);
            continue;
        }
        return ("the box reaches `" + user->getName().getStringRef() +
                "`, so something other than a name holds it")
            .str();
    }
    if (!plan.store) { return std::string{"nothing is ever assigned to it"}; }
    plan.closure = plan.store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
    if (!plan.closure) {
        return std::string{"what is assigned to it is not a ctjs.create_closure of this frame"};
    }
    // AND THE CLOSURE IS THE BINDING AND NOTHING ELSE. A second use is a
    // function VALUE - stored, returned, passed - and one of the other
    // rules owns that, with a sentence that names it.
    if (!plan.closure.getResult().hasOneUse()) {
        return std::string{"the closure it holds is used as a value somewhere else as well"};
    }
    if (const std::optional<std::string> why = whyTargetIsNotLiftable(plan.closure)) { return why; }
    plan.target = targetOf(plan.closure);
    // CONDITION 3, and it is `writeReachesEveryRead` unchanged: the same
    // two clauses slice 2 steps 1 and 3 ask of the same use list with the
    // same DominanceInfo. Step 1 asks them to replace a read with the
    // store's operand and step 3 asks them to drop the box's initial from a
    // type; this asks them to replace a read with a CALL, and all three
    // rest on the one fact - no read of the binding can happen on a path
    // that skipped the store.
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
    // CONDITION 4, ONE FRAME IN - AND THEN ONE FRAME FURTHER FOR AS LONG AS
    // THE BINDING TRAVELS, which is Phase 59 slice 2 step 5.
    llvm::DenseSet<std::pair<mlir::Operation *, unsigned>> examined;
    for (auto & [made, slot] : plan.captured) {
        if (const std::optional<std::string> why =
                examineCapturedSlot(plan, made, slot, 0, examined)) {
            return why;
        }
    }
    // A NAME NOTHING CALLS IS NOT WORTH ERASING A BOX FOR, and lifting its
    // target would mark private a function with no visible caller - which
    // DeadCodeAnalysis reads as dead and every type in it as `<unvisited>`.
    if (plan.calls == 0) { return std::string{"nothing calls it"}; }
    return std::nullopt;
}

// THE CALL A NAME MAKES, WRITTEN IN A FRAME THE CLOSURE VALUE CANNOT REACH.
//
// `$callee_value` is `undefined`, as it is in the method and constructor
// arms and here for a second reason on top of theirs: the closure is
// defined in ANOTHER ctjs.func and naming it from this one is not something
// SSA allows at all. So `lift()` never sees this site - it walks the uses
// of the closure value - and that is sound only because condition 5 has
// proved the callee has no capture left for a lift to prepend.
void closureLifter::makeBoundCallDirect(ctjs::CallOp call, ctjs::FuncOp target) {
    const unsigned parameters = target.getBody().front().getNumArguments() - 3;
    mlir::OpBuilder at(call);
    const auto valueType = ctjs::ValueType::get(context);
    const mlir::Value undefined =
        ctjs::ConstantOp::create(at, call.getLoc(), valueType, ctjs::UndefinedAttr::get(context));
    llvm::SmallVector<mlir::Value> arguments(call.getArgs());
    while (arguments.size() < parameters) { arguments.push_back(undefined); }
    auto direct = ctjs::CallDirectOp::create(at, call.getLoc(), valueType,
                                             mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()),
                                             call.getReceiver(), undefined, undefined, arguments,
                                             /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
    call.getResult().replaceAllUsesWith(direct.getResult());
    call.erase();
}

// AND THE CAPTURE SLOTS THE BINDING OCCUPIED, REMOVED IN PLACE.
//
// IN PLACE, NOT REBUILT, and that is not a style choice: `plans` and
// `bindingClosures` hold ctjs.create_closure pointers, and a rebuilt
// operation is a different one - every reader would then be holding a
// dangling closure and the fixpoint's verdicts would be about operations
// that no longer exist.
//
// FOUR THINGS MOVE TOGETHER, and leaving any one of them behind is a
// verifier error rather than a wrong answer: the operand, the parallel
// `enclosing_indices` entry, the target's `upvalue_count`, and every
// upvalue index past the hole - in the target's own reads and writes and in
// the `enclosing_indices` of the closures it makes.
void closureLifter::removeCaptureSlots(ctjs::CreateClosureOp c, llvm::ArrayRef<unsigned> slots) {
    ctjs::FuncOp target = targetOf(c);
    const auto captures = static_cast<unsigned>(c.getUpvalues().size());
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
            c->setAttr("enclosing_indices", mlir::Builder(context).getDenseI32ArrayAttr(rest));
        }
    }
    llvm::BitVector drop(c->getNumOperands(), false);
    for (unsigned i = 0; i < captures; ++i) {
        if (gone[i]) { drop.set(kFirstCapture + i); }
    }
    c->eraseOperands(drop);
    target->setAttr("upvalue_count",
                    mlir::Builder(context).getI32IntegerAttr(static_cast<int>(kept)));
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
        if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(o)) {
            o->setAttr("index", mlir::Builder(context).getI32IntegerAttr(
                                    move(static_cast<std::int32_t>(read.getIndex()))));
            return;
        }
        if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(o)) {
            o->setAttr("index", mlir::Builder(context).getI32IntegerAttr(
                                    move(static_cast<std::int32_t>(write.getIndex()))));
            return;
        }
        if (auto nested = llvm::dyn_cast<ctjs::CreateClosureOp>(o)) {
            const mlir::DenseI32ArrayAttr indices = nested.getEnclosingIndicesAttr();
            if (!indices) { return; }
            llvm::SmallVector<std::int32_t> moved;
            for (const std::int32_t index : indices.asArrayRef()) {
                moved.push_back(index < 0 ? index : move(index));
            }
            o->setAttr("enclosing_indices", mlir::Builder(context).getDenseI32ArrayAttr(moved));
        }
    });
}

// THE WHOLE OF STEP 4, RUN ONCE AND BEFORE `census()`.
void closureLifter::bindLocalFunctions(liftReport & out) {
    llvm::MapVector<mlir::Operation *, functionBinding> plans;
    module.walk([&](ctjs::CreateCellOp cell) {
        if (!holdsAFunction(cell)) { return; }
        functionBinding plan;
        if (const std::optional<std::string> why = examineFunctionBinding(cell, plan)) {
            whyNotAFunctionBinding[cell.getOperation()] = *why;
            return;
        }
        plans.insert({cell.getOperation(), plan});
    });
    // CONDITION 5, AS A GREATEST FIXPOINT: start by believing every
    // candidate and drop the ones whose closure closes over something this
    // step does not erase. Greatest, and not least, because MUTUAL
    // recursion is two bindings each of which needs the other - `var a =
    // function () { b(); }; var b = function () { a(); };` - and a least
    // fixpoint takes neither.
    //
    // ONLY A BINDING READ FROM ANOTHER FRAME IS ASKED. A name called only
    // where it was written keeps its closure value in scope, so slice 1's
    // own rule prepends the captures at those calls and any capture it can
    // carry is fine.
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
    // PASS A: EVERY READ BECOMES A CALL OF THE TARGET.
    llvm::MapVector<mlir::Operation *, slotRemoval> removals;
    for (auto & entry : plans) {
        if (!taken.contains(entry.first)) { continue; }
        functionBinding & plan = entry.second;
        bindingClosures.insert(plan.closure.getOperation());
        // IN THE FRAME THAT OWNS THE BOX THE CLOSURE VALUE IS IN SCOPE, so
        // the read is simply replaced by it and slice 1's own rule takes
        // the call - captures, arity, new.target and all. That is why this
        // arm writes no ctjs.call_direct of its own, and why a binding
        // called only here may hold a closure with captures.
        for (ctjs::CellGetOp read : plan.reads) {
            read.getResult().replaceAllUsesWith(plan.closure.getResult());
            read.erase();
        }
        for (auto & [made, slot, depth] : plan.slots) {
            ctjs::FuncOp holder = targetOf(made);
            llvm::SmallVector<ctjs::LoadUpvalueOp> named;
            holder.getBody().walk([&](ctjs::LoadUpvalueOp read) {
                if (static_cast<unsigned>(read.getIndex()) == slot) { named.push_back(read); }
            });
            for (ctjs::LoadUpvalueOp read : named) {
                llvm::SmallVector<mlir::Operation *> sites(read.getResult().getUsers().begin(),
                                                           read.getResult().getUsers().end());
                for (mlir::Operation * site : sites) {
                    // EVERY USE IS A CALL, BECAUSE CONDITION 4 SAID SO -
                    // and this says which claim failed when it is not. An
                    // `llvm::cast` was here, which in a Release build is
                    // unchecked: the same shape that made a relaxed
                    // condition 4 SEGFAULT the plain lift rather than say
                    // anything (see the note beside its own fatal).
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
                    makeBoundCallDirect(call, plan.target);
                    // BOTH COUNTERS, and `calls` is the load-bearing one:
                    // tools/check/native-claims.py reads "rewrote N call(s)"
                    // out of the remark as the number of ctjs.call_direct
                    // this pass made, and a stage that makes them without
                    // counting them is exactly the blindness that file's
                    // own comment is about.
                    ++out.calls;
                    ++out.bound;
                }
                read.erase();
            }
            // AND THE SHALLOWEST DEPTH ANY BINDING NAMED THIS CLOSURE
            // AT, which is the key pass B orders on.
            slotRemoval & removal = removals[made.getOperation()];
            removal.depth = removal.slots.empty() ? depth : std::min(removal.depth, depth);
            removal.slots.push_back(slot);
        }
        ++out.bindings;
    }
    // PASS B: AND THE SLOTS THOSE READS CAME THROUGH, one rewrite per
    // closure however many of its slots held a binding - DEEPEST FIRST.
    //
    // THE ORDER IS A CORRECTNESS CONDITION AND NOT A TIDINESS ONE.
    // `removeCaptureSlots` renumbers, for the closure it is given, every
    // `enclosing_indices` entry of every closure the TARGET makes - and its
    // `move()` has no image for an index that has just been deleted. So a
    // closure that fills a slot from another closure's slot has to be
    // rewritten FIRST, at which point its entry naming that slot is gone
    // and the outer renumbering meets only surviving indices.
    //
    // AND THE KEY IS THE CLOSURE'S OWN NESTING DEPTH, NOT ANY SLOT'S.
    //
    // THIS ORDERED ON SLOT DEPTH AND BOTH CHOICES ARE WRONG. The minimum
    // aborted the compiler on seven lines of ordinary JavaScript:
    //
    //     var w = function (k) { return k + 3; };
    //     var q = function (k) { return k + 4; };
    //     var mid = function (k) {
    //         var p = function (j) { return j + 5; };
    //         var inner = function (m) { return q(m) + p(m); };
    //         return w(k) + inner(k);
    //     };
    //
    // `mid` holds `w`, which stops at depth 0, and `q`, which travels on;
    // `inner` holds the travelled `q` at depth 1 and `p`, owned in mid's
    // own frame, at depth 0. Both closures therefore key on 0, the
    // stable_sort tie keeps `mid` first because w's plan inserted it first,
    // and `move()` finds no image for the slot `inner` still names:
    // `LLVM ERROR: ... still names upvalue 0, which the local-function rule
    // removed`. A REGRESSION FROM A REFUSAL TO AN ABORT - step 4 refused
    // that program.
    //
    // AND THE MAXIMUM IS NOT THE FIX, which is why the paragraph that
    // stood here reasoned its way to the wrong answer: a closure `a`
    // holding one binding at depth 0 and another at depth 1, with a `b`
    // inside a's target holding only the first at depth 1, ties under the
    // maximum and aborts the other way round. No function of the slot
    // depths orders these, because a slot's depth measures how far a
    // BINDING travelled, and what the rewrite needs is how deep the CLOSURE
    // sits.
    //
    // Nesting depth answers it directly: a closure made inside another
    // closure's target is strictly deeper than one made in the frame that
    // built it, whatever bindings either happens to hold. `ctjs.func` is
    // IsolatedFromAbove and the functions are siblings in the module, so
    // the nesting is the closure-creation tree - the target of a
    // create_closure sits one frame inside the function that makes it.
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
        // A CYCLE IS NOT REACHABLE and this stops rather than hangs if one
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
    // PASS C: the store and the box, which nothing uses now.
    for (auto & entry : plans) {
        if (!taken.contains(entry.first)) { continue; }
        functionBinding & plan = entry.second;
        // THE STORE FIRST, because it is itself a use of the box - and then
        // the box has to have none left, which is the claim conditions 1
        // and 2 make and this is where it is asserted rather than assumed.
        plan.store.erase();
        if (!plan.cell.getResult().use_empty()) {
            llvm::report_fatal_error(
                "ctnative lowering: a local function binding still has a use after its reads "
                "and its capture slots were rewritten - examineFunctionBinding admitted a use "
                "the rewrite does not remove");
        }
        plan.cell.erase();
    }
}

// WHY A CLOSURE PUT INTO A LOCAL BINDING IS NOT A DIRECT CALL, which is
// `whyNotAMethodField` one binding along: "it reaches `ctjs.cell_set`"
// names the mechanism and not the obstacle, and the obstacle is always one
// of the clauses above.
std::string closureLifter::whyNotABoundFunction(ctjs::CellSetOp store) {
    auto cell = store.getCell().getDefiningOp<ctjs::CreateCellOp>();
    if (!cell) {
        // NOT A `ctjs.create_cell` OF THIS FRAME, which after slice 2 step
        // 2 has one meaning: the box is a POINTER parameter and this is a
        // write through it - `lift()` turns a `ctjs.store_upvalue` into
        // exactly this shape. A binding something writes through a pointer
        // is a variable, not one function.
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
