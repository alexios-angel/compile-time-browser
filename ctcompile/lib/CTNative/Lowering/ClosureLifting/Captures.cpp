// ClosureLifting/Captures.cpp - native lowering implementation.
#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

// THE CAPTURE CLAUSES, SHARED WITH THE RECEIVER LIFT. A method IS a closure
// in the IR - `{ f: function () {} }` compiles to a `closure` opcode and a
// `set_prop` - so a method that also captures a binding has to satisfy
// exactly these, and factoring them is what makes the two rules compose
// rather than diverge.
std::optional<std::string> closureLifter::whyTargetIsNotLiftable(ctjs::CreateClosureOp c) {
    ctjs::FuncOp target = targetOf(c);
    if (!target) {
        return "its target emitted no ctjs.func - the importer refused it (ctjs.skipped)";
    }
    if (target.getBody().empty() || target.getBody().front().getNumArguments() < 3) {
        return "its target has no body";
    }
    mlir::Block & entry = target.getBody().front();
    const auto captures = static_cast<unsigned>(c.getUpvalues().size());
    if (captures != static_cast<unsigned>(target.getUpvalueCount())) {
        return "its capture list disagrees with the descriptors of the function it names";
    }
    // AN ARROW'S `this` IS LEXICAL, and after the importer's correction the
    // presence of a non-undefined $enclosing_this is the only place the IR
    // says a target is one. A lifted call passes the CALL's receiver as
    // %arg0, which for an arrow is not what the interpreter reads - so an
    // arrow may be lifted only when it never looks.
    //
    // THIS IS THE GUARD THAT KEEPS AN ARROW OUT OF THE RECEIVER LIFT, and
    // it is the one whose removal gives a WRONG ANSWER rather than a
    // refusal: `{ f: () => this.x }` reads the ENCLOSING `this`, every use
    // of it is a constant-key read that condition 3 admits, and lifting it
    // would silently rebind `this` to the literal. It is asked FIRST for
    // that reason, before any rule that admits a `this` use.
    if (!isUndefinedConstant(c.getEnclosingThis()) && !entry.getArgument(0).use_empty()) {
        return "it is an arrow function that reads its lexical `this` - Stage 59B";
    }
    return std::nullopt;
}

// WHICH UPVALUE OF THE ENCLOSING CLOSURE FILLS CAPTURE SLOT i, or -1 when
// the operand beside it is the binding and nothing is filled. The list is
// optional and, when present, exactly as long as the capture list -
// ctjs.create_closure's own description, and its verifier - so a missing
// attribute means every slot is from_parent_local, which is most closures.
std::int32_t closureLifter::enclosingIndex(ctjs::CreateClosureOp c, unsigned i) {
    const mlir::DenseI32ArrayAttr indices = c.getEnclosingIndicesAttr();
    if (!indices || i >= static_cast<unsigned>(indices.size())) { return -1; }
    return indices[i];
}

// THE VALUE A LIFTED CALL PASSES FOR CAPTURE SLOT i, or null when the slot
// is neither shape a lift carries. Two shapes, and both hold the VALUE of a
// binding, never the box:
//
//   * the operand is a ctjs.create_cell of this frame: its initial. A read
//     of the capture in the target is a read of that value, because
//     ctjs.load_upvalue reads THROUGH the cell (run_loop.cpp, get_upvalue:
//     `reg = cell->slot`), and isConstantCell has to prove nothing ever
//     wrote it.
//   * PHASE 59 SLICE 1b: `enclosing_indices[i]` is a k >= 0 - the slot is
//     filled from the ENCLOSING closure's upvalue k - and the enclosing
//     ctjs.func has already lifted, so `ctnative.captures` is on it and
//     k is inside that range. lift() made its upvalue k an entry-block
//     argument at 3 + k, holding the initial of a cell an outer frame
//     proved constant. The index names a CELL there and the argument holds
//     what every read of that cell yields, which is what a capture is read
//     for; passing it on is passing the same value. The capture OPERAND at
//     i is the importer's `undefined` placeholder and is never consulted -
//     ct_aot_make_closure does not consult it either.
//
// NOTHING ELSE. A parameter of the enclosing function (index at or past
// 3 + captures) is not a capture and no index names it - a captured
// parameter is boxed, so its slot is from_parent_local and its operand is
// the cell; %arg0-2 are never captures; and a k the enclosing function's
// capture range does not cover names an upvalue the lift did not carry.
mlir::Value closureLifter::capturedValue(ctjs::CreateClosureOp c, unsigned i) {
    if (auto cell = c.getUpvalues()[i].getDefiningOp<ctjs::CreateCellOp>()) {
        // PHASE 59 SLICE 2 STEP 2: THE BOX ITSELF, when it is carried. The
        // call site takes its ADDRESS - `replace()` does that, from the
        // `ctnative.cell_args` index, through the same `asPointer` the
        // receiver uses - so what has to be in scope and dominating at the
        // call is the variable, and that is what this hands back.
        if (isCarried(cell)) { return cell.getResult(); }
        return constantValueOf(cell);
    }
    const std::int32_t k = enclosingIndex(c, i);
    if (k < 0) { return {}; }
    auto enclosing = c->getParentOfType<ctjs::FuncOp>();
    if (!enclosing || enclosing.getBody().empty()) { return {}; }
    const auto captures = enclosing->getAttrOfType<mlir::IntegerAttr>("ctnative.captures");
    if (!captures || k >= captures.getInt()) { return {}; }
    // 3 + captures ARGUMENTS AT LEAST, which lift() guarantees by inserting
    // them into a block that already had three. Asked anyway, because this
    // reads an argument by number and the claim costs one comparison.
    mlir::Block & entry = enclosing.getBody().front();
    if (entry.getNumArguments() <= captureArgument(static_cast<unsigned>(k))) { return {}; }
    return entry.getArgument(captureArgument(static_cast<unsigned>(k)));
}

// The same, after admission: anything else here is a rule that let one
// through, which this file reports as a named fatal and never as a number.
mlir::Value closureLifter::liftedCapture(ctjs::CreateClosureOp c, unsigned i) {
    if (const mlir::Value value = capturedValue(c, i)) { return value; }
    llvm::report_fatal_error(
        "ctnative lowering: a capture admitted by whyCapturesDoNotLift is neither a constant "
        "cell of its frame nor a lifted capture parameter of the enclosing function - the "
        "admission and the call-site rewrite have drifted apart");
}

// CONDITION 3 OF THE SINGLE-WRITE RULE, AND THE AVAILABILITY OF EVERY OTHER
// CAPTURE, ASKED AT ONE CALL SITE.
//
// lift() prepends `capturedValue(c, i)` at each call it rewrites, and the
// interpreter reads the box when the closure RUNS - so the point that has
// to come after the single write, and the point at which the value has to
// be in scope at all, is the CALL and not the ctjs.create_closure. Both
// halves are one dominance question and this asks it.
//
// IT IS NOT A THEOREM FOR ANY OF THE THREE RULES, and it used to look like
// one for two of them. A plain closure's sites are uses of `c`'s result, so
// `c` dominates them - but `c` is HOISTED for a function declaration and
// the store is not, so the value need not dominate `c` at all and the chain
// through it proves nothing. A method's sites are `obj.m()` calls reached
// through the OBJECT, which nothing orders after the ctjs.set_property that
// bound the field; a receiver that is another method's `%arg0` is not even
// in the same ctjs.func, and there a bare `properlyDominates` answers "yes"
// because builtin.module's body is a GRAPH region in which every operation
// dominates every other. So the function is compared before the dominance
// is, and every rule asks.
std::optional<std::string> closureLifter::whyCapturesDoNotReach(ctjs::CreateClosureOp c,
                                                                mlir::Operation * at) {
    auto here = at->getParentOfType<ctjs::FuncOp>();
    for (unsigned i = 0; i < static_cast<unsigned>(c.getUpvalues().size()); ++i) {
        mlir::Value value = capturedValue(c, i);
        // Null is a slot no rule admits; whyCapturesDoNotLift says which.
        if (!value) { continue; }
        if (value.getParentRegion()->getParentOfType<ctjs::FuncOp>() != here) {
            return "capture " + std::to_string(i) +
                   " is a binding of the frame that built the closure, and this call of it "
                   "is in another function - lifting prepends the captured value at the "
                   "CALL, and there is nothing to prepend here";
        }
        if (!dominance.properlyDominates(value, at)) {
            return "capture " + std::to_string(i) +
                   " is a binding whose value does not reach this call of it - the "
                   "assignment does not dominate the call, so the interpreter reads the "
                   "undefined the binding was hoisted with";
        }
        // AND THE WRITE ITSELF, NOT ONLY THE VALUE IT STORES. These are two
        // different questions and only one of them was being asked.
        //
        // The clause above proves the value is IN SCOPE at the call. It
        // does not prove the write has HAPPENED. store-dominates-call
        // implies value-dominates-call, because the operand dominates its
        // own store; the CONVERSE does not hold, and the gap is exactly the
        // shape this rule exists to admit. In
        //
        //     function pick(k) { var v; var t = k * 2;
        //                        if (k > 0) { v = t; }
        //                        function get() { return v; } return get(); }
        //
        // `t` is computed before the branch, so it dominates every call
        // while the `ctjs.cell_set` inside the `scf.if` dominates none.
        // With only the value asked, `pick(-1)` compiled to -2 where the
        // interpreter says `undefined`: `unboxCells` had replaced the read
        // with `t` and the conditional store was erased. Three independent
        // reviews found this within one program each, through the plain,
        // method and constructor rules alike, and the same hole ate a
        // `while` whose body stores a value computed above the loop.
        //
        // A wrong answer is the one thing this tier may not produce, so the
        // question the comment above and the refusal below both describe -
        // does the ASSIGNMENT dominate the call - is now the question the
        // code asks.
        // AND NOT OF A CARRIED ONE - PHASE 59 SLICE 2 STEP 2. This
        // question is the by-VALUE path's: it asks whether the one value a
        // copy would carry has been stored by the time the call runs. A
        // binding carried BY POINTER copies nothing; the callee reads the
        // variable when it runs, and on a path where nothing was stored it
        // reads the NaN the variable was initialised with, which is the
        // `undefined` the interpreter reads. `writtenOnce` still holds
        // such a cell - it has one ctjs.cell_set - so the test is on the
        // path and not on the map.
        if (auto cell = c.getUpvalues()[i].getDefiningOp<ctjs::CreateCellOp>();
            cell && !isCarried(cell)) {
            if (ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation())) {
                if (!dominance.properlyDominates(write.getOperation(), at)) {
                    return "capture " + std::to_string(i) +
                           " is a binding whose single assignment does not dominate this "
                           "call of it - the call can be reached without the assignment "
                           "having run, and the interpreter reads the undefined the binding "
                           "was hoisted with";
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> closureLifter::whyCapturesDoNotLift(ctjs::CreateClosureOp c) {
    if (const std::optional<std::string> why = whyTargetIsNotLiftable(c)) { return why; }
    const auto captures = static_cast<unsigned>(c.getUpvalues().size());
    auto enclosing = c->getParentOfType<ctjs::FuncOp>();
    // A capture is a constant box in this frame, or a lifted capture
    // parameter of the enclosing function, or it is not liftable.
    for (unsigned i = 0; i < captures; ++i) {
        const mlir::Value operand = c.getUpvalues()[i];
        if (auto cell = operand.getDefiningOp<ctjs::CreateCellOp>()) {
            // PHASE 59 SLICE 2 STEP 2, ASKED SECOND. A cell the
            // immutability proof takes is copied into a parameter; one it
            // does not is a frame-local variable this call passes a
            // POINTER to. Only when neither holds is the capture refused,
            // and then the box itself is the problem.
            if (isConstantCell(cell) || isCarried(cell)) { continue; }
            // THREE SENTENCES WERE HERE AND TWO ARE GONE. "a binding that
            // is reassigned - a shared cell is Phase 59 slice 2" and "its
            // one assignment does not dominate every read" were slice 1's
            // and step 1's refusals for exactly the shapes step 2 carries;
            // both now lift, and native-shared-cell-fixture.js runs them
            // against the interpreter. What is left is the one clause
            // sharedCellCensus can fail, and it has a sentence for every
            // cell isConstantCell refused - so an absent one is a rule
            // that let a cell past both censuses, which is a fatal here
            // and not a number anywhere.
            const auto shared = whyNotCarried.find(cell.getOperation());
            if (shared == whyNotCarried.end()) {
                llvm::report_fatal_error(
                    "ctnative lowering: a cell is neither constant nor carried and the "
                    "shared-cell census wrote no reason for it - sharedCellCensus and "
                    "isConstantCell have drifted apart");
            }
            return "capture " + std::to_string(i) +
                   " is a shared binding this tier cannot make a frame-local variable: " +
                   shared->second;
        }
        if (capturedValue(c, i)) { continue; }
        if (enclosingIndex(c, i) >= 0 && enclosing && !enclosing->hasAttr("ctnative.captures")) {
            chainedThrough[c.getOperation()] = enclosing;
            return "capture " + std::to_string(i) +
                   " is filled from the enclosing closure, which did not lift";
        }
        return "capture " + std::to_string(i) +
               " is neither a cell of this frame nor a capture parameter of the enclosing "
               "function";
    }
    return std::nullopt;
}

// THE TARGET'S OWN CLOSURE FEEDS NOTHING BUT NESTED CLOSURES AND UPVALUE
// READS, which is ResolveGlobals' clause 4 word for word and is here for
// its reason: the lift marks the target `private`, and `private` is the
// claim that EVERY caller is visible. A target that leaks its own closure
// value can be called through that value by something this IR cannot see,
// and the claim would be false.
std::optional<std::string> closureLifter::whyOwnClosureEscapes(ctjs::FuncOp target) {
    for (mlir::OpOperand & use : target.getBody().front().getArgument(2).getUses()) {
        mlir::Operation * user = use.getOwner();
        if (use.getOperandNumber() == 0 &&
            llvm::isa<ctjs::CreateClosureOp, ctjs::LoadUpvalueOp, ctjs::StoreUpvalueOp>(user)) {
            continue;
        }
        return ("its target's own closure escapes into `" + user->getName().getStringRef() +
                "`, so a call of it may come from somewhere this rewrite cannot see")
            .str();
    }
    return std::nullopt;
}

// The target's own upvalue reads have to be the shape the rewrite replaces,
// or a load would be left naming a closure that is gone.
//
// AND ITS WRITES, WHICH IS WHERE SLICE 2 STEP 2 RELAXES EXACTLY ONE
// CLAUSE AND NOT A LINE MORE. This refused ANY `ctjs.store_upvalue`,
// unconditionally - "its target reassigns a captured binding". A write is
// now admitted when, and only when, the SLOT it names is one this closure
// carries by pointer: `slotIsCarried(c, k)`, which is true for a carried
// cell of the creating frame and for a slot filled from an enclosing
// capture that is already a pointer. A write to any other slot is still
// refused, and by name - a by-value capture of a mutated binding would
// give every call its own copy, which is the wrong answer COUNTER in
// closure-refusals.mlir was pinned for.
//
// THE CLOSURE IS A PARAMETER NOW, AND IT HAS TO BE: "is this slot
// carried" is a question about the CREATION SITE's operands, not about
// the target, and the target is what the three callers share.
//
// THE INDEX CHECK IS THE SAME ONE THE READ GETS, and it was not there for
// writes at all. A store naming something other than `%arg2`, or an index
// past the capture list, is a write this rewrite cannot place - and
// leaving it would put a ctjs.store_upvalue in a function whose closure
// operand is about to be erased.
std::optional<std::string> closureLifter::whyUpvalueReadsDoNotLift(ctjs::CreateClosureOp c,
                                                                   ctjs::FuncOp target) {
    mlir::Block & entry = target.getBody().front();
    const auto captures = static_cast<unsigned>(target.getUpvalueCount());
    std::optional<std::string> bad;
    target.getBody().walk([&](mlir::Operation * o) {
        if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(o)) {
            if (read.getClosure() != entry.getArgument(2) ||
                static_cast<unsigned>(read.getIndex()) >= captures) {
                bad = "its target reads an upvalue this rewrite cannot name";
            }
        }
        if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(o)) {
            const auto k = static_cast<unsigned>(write.getIndex());
            if (write.getClosure() != entry.getArgument(2) || k >= captures) {
                bad = "its target writes an upvalue this rewrite cannot name";
            } else if (!slotIsCarried(c, k)) {
                bad = "its target reassigns capture " + std::to_string(k) +
                      ", which is not a binding this tier carries by pointer - copying it "
                      "would give every call its own";
            }
        }
    });
    return bad;
}

// The four admission conditions of slice 1, as one sentence each. The
// reason is written onto the closure so that the function containing it is
// refused by NAME rather than by "`ctjs.create_closure` is not native yet".
std::optional<std::string> closureLifter::whyNotLiftable(ctjs::CreateClosureOp c) {
    if (const std::optional<std::string> why = whyCapturesDoNotLift(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    const unsigned parameters = entry.getNumArguments() - 3;
    // CONDITION 4: every use of the closure VALUE is a call this lowers.
    //
    // A NAME'S CLOSURE MAY HAVE NO USES AT ALL - PHASE 59 SLICE 2 STEP 4.
    // A local binding read only from inside another function has every one
    // of its calls written as a `ctjs.call_direct` in that other frame, and
    // such a site carries `undefined` as its callee value because the
    // closure is not in scope there. So the closure value can be used by
    // nothing and still be called by several things, and
    // `bindLocalFunctions` has already proved at least one call exists
    // ("nothing calls it" is one of its own refusals).
    if (c.getResult().use_empty() && !bindingClosures.contains(c.getOperation())) {
        return "nothing calls it";
    }
    // A NAME FIRST, WHATEVER ELSE THE VALUE REACHES - PHASE 59 SLICE 2
    // STEP 4. A closure put into a local binding IS that binding, and the
    // sentence a reader can act on is the one saying which clause of the
    // binding rule failed. "it reaches `ctjs.cell_set`" named the mechanism
    // and was the terminal of 9 of the 19 chains a ctjs.call_direct reaches
    // on bootstrap; the clause is the next lever.
    //
    // ASKED BEFORE THE LOOP, AND THAT IS NOT TIDINESS. A binding step 4
    // refused keeps its box, the capture of that box is then read as a
    // constant cell and lifted BY VALUE, and the closure acquires a SECOND
    // use - a `ctjs.call_direct` argument. Left to the loop, which returns
    // on the first refusing use, the answer would depend on the order of
    // the use list and would usually be "it is passed as an argument": a
    // true sentence about a consequence, and no help at all about the
    // obstacle. Measured on three of the witnesses in closure-refusals.mlir
    // (BOUNDVALUE, BOUNDDATA and BOUNDDEEP), which named that instead of
    // their own clause until this moved.
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        if (auto into = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
            into && use.getOperandNumber() == 1) {
            return whyNotABoundFunction(into);
        }
    }
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        // A CALL THE CLOSED WORLD ALREADY NAMED, WHICH IS THE SAME CALL
        // SITE ONE OPERAND ALONG.
        //
        // --ctjs-resolve-globals rewrites a ctjs.call whose callee is a
        // ctjs.create_closure result into a ctjs.call_direct, where the
        // closure is `$callee_value` at operand 2 rather than the callee at
        // operand 0. Without this arm that use falls to the "it is passed
        // as an argument" refusal below and the lift is LOST on exactly the
        // closures the closed world just proved - measured, before this arm
        // existed, as 3 lifted closures on bootstrap, 27 on p5 and 3 on
        // phaser going to zero.
        //
        // THE ARITY NEEDS NO CHECK HERE. CallDirectOp::verifySymbolUses
        // holds the operand count equal to the entry block's, so a
        // call_direct that verifies passes exactly `parameters` arguments -
        // the resolver padded a short call and refused a long one.
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user);
            direct && use.getOperandNumber() == 2 && direct.getCallee() == target.getSymName()) {
            auto caller = direct->getParentOfType<ctjs::FuncOp>();
            if (caller && passesNewTarget.contains(caller.getOperation())) {
                return "a call of it sits in a function that passes new.target";
            }
            continue;
        }
        auto call = llvm::dyn_cast<ctjs::CallOp>(user);
        if (!call || use.getOperandNumber() != 0) {
            if (llvm::isa<ctjs::StoreGlobalOp>(user)) {
                return "it is stored to a global - Phase 59 slice 2";
            }
            if (llvm::isa<ctjs::ReturnOp>(user)) { return "it is returned - Phase 59 slice 2"; }
            // A METHOD FIELD THE RECEIVER LIFT DID NOT TAKE. Saying "it is
            // stored into an object" for `{f: function(){}}` names the
            // mechanism and not the obstacle, and the obstacle is always
            // one of two things - the shape is not closed, or the key is
            // written twice - which is exactly what a reader needs.
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                set && use.getOperandNumber() == 2) {
                return whyNotAMethodField(set);
            }
            if (llvm::isa<ctjs::SetPropertyOp, ctjs::CreateObjectOp, ctjs::AppendOp,
                          ctjs::CreateArrayOp>(user)) {
                return "it is stored into an object or an array - Phase 59 slice 2";
            }
            // A REFUSAL WAS HERE AND IS GONE: "it is used as a constructor
            // - Phase 60 owns `new`". Every closure a `ctjs.construct`
            // names is now in `constructorClosures` and dispatches to
            // whyNotLiftableConstructor, so this arm was unreachable - and
            // it was pinned by no test, which is how it stayed reachable-
            // looking. The mixed case it used to describe is named there
            // instead, where the clause that actually failed can be said.
            if (call || llvm::isa<ctjs::CallDirectOp, ctjs::ConstructOp>(user)) {
                // PASSING A CLOSURE IS NOT A LIFT, and this is the one
                // place the brief for this work asked for something the
                // mechanism cannot give. Lifting moves captures to the
                // CALL SITE; a callee that receives a function value has
                // no call site to move them to, and lowering it needs the
                // callee specialised per closure - Phase 63's monomorphism
                // proof, not this.
                return "it is passed as an argument - lifting has no call site to move the "
                       "captures to, so this needs a specialised callee (Phase 63), not a "
                       "lift";
            }
            return ("it reaches `" + user->getName().getStringRef() +
                    "`, which slice 1 does "
                    "not lower")
                .str();
        }
        if (call.getArgs().size() > parameters) {
            return "a call passes " + std::to_string(call.getArgs().size()) + " argument(s) to " +
                   std::to_string(parameters) + " parameter(s) - the surplus has frame semantics";
        }
        auto caller = call->getParentOfType<ctjs::FuncOp>();
        if (caller && passesNewTarget.contains(caller.getOperation())) {
            return "a call of it sits in a function that passes new.target";
        }
    }
    // AND THE CAPTURED VALUES REACH EVERY ONE OF THOSE SITES. The loop above
    // has just established that every use of the closure value IS a call
    // this rewrite lowers, so its users are exactly the sites lift() will
    // prepend the captures at.
    for (mlir::Operation * user : c.getResult().getUsers()) {
        if (const std::optional<std::string> why = whyCapturesDoNotReach(c, user)) { return why; }
    }
    if (const std::optional<std::string> escapes = whyOwnClosureEscapes(target)) { return escapes; }
    // CONDITION 3 is the existing call-graph fixpoint's, not this one's.
    return whyUpvalueReadsDoNotLift(c, target);
}

} // namespace ctcompile::ctnative::lowering_detail
