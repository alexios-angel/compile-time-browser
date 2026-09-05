// ClosureLifting/Methods.cpp - native lowering implementation.
#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

// --- the receiver lift --------------------------------------------------

// CONDITION 1, AS IT WILL READ AFTER THE REWRITE. `hasClosedShape` refuses
// any use that is not a constant-key get or set, and a method call is two
// of those uses - the object as the call's RECEIVER, and the get_property
// that loads the method - so asking it before the rewrite would refuse
// every object with a method on it. This is the same question asked of the
// IR this rewrite is about to produce, where the load is gone and the
// receiver is a call_direct operand `hasClosedShape` now admits by name.
bool closureLifter::closedAfterLift(mlir::Value object) {
    if (!object.getDefiningOp<ctjs::CreateObjectOp>() && !makesAnInstance(object)) { return false; }
    return usesCloseTheShape(object);
}

// A `ctjs.construct` RESULT IS A LITERAL THAT HAS NOT HAPPENED YET.
//
// The constructor lift replaces the construct with an empty
// ctjs.create_object and a receiver call, so by the time anything reads a
// shape the instance IS an object literal. Every census here runs BEFORE
// that rewrite, though, so each would see a `ctjs.construct` and answer
// "not a literal" - which refuses the module for an instance that is
// merely passed to a lifted function. This is the same question asked of
// the IR the rewrite is about to produce, exactly as `closedAfterLift`
// itself is for a method call.
//
// ONLY FOR A CALLEE THIS PASS HAS PROVED, which is what
// `constructorClosures` holds and why that census runs first: a construct
// whose callee is opaque is not going to become a literal, and admitting
// one here would be a shape claim about an object the VM allocates.
bool closureLifter::makesAnInstance(mlir::Value object) {
    auto built = object.getDefiningOp<ctjs::ConstructOp>();
    if (!built) { return false; }
    auto closure = built.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
    if (!closure || !constructorClosures.contains(closure.getOperation())) { return false; }
    // AND NOTHING BUT `new` USES IT. `constructorClosures` is every closure
    // a `new` names, so that the prototype clause can be REACHED and name
    // itself; this predicate is a different claim - that the rewrite will
    // actually happen - and a closure used anywhere else cannot support it.
    return llvm::all_of(closure.getResult().getUsers(),
                        [](mlir::Operation * user) { return llvm::isa<ctjs::ConstructOp>(user); });
}

// THE USE-LIST HALF OF CONDITION 1, ASKED WITHOUT THE QUESTION OF WHAT MADE
// THE VALUE. `closedAfterLift` asks it of a literal. The constructor lift
// asks the identical question of a `ctjs.construct` result, because the
// rewrite turns that result INTO a literal and its use list does not move -
// so a second, drifting copy of this walk is exactly what is not wanted.
bool closureLifter::usesCloseTheShape(mlir::Value object) {
    for (mlir::OpOperand & use : object.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() != 0 || constantKeyOf(get.getKey()).empty()) {
                return false;
            }
            continue;
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() != 0 || constantKeyOf(set.getKey()).empty()) {
                return false;
            }
            continue;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            // The object as the RECEIVER of a call whose callee is a
            // constant-key read of that same object: a method call.
            if (use.getOperandNumber() == 1) {
                auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (!load || load.getObject() != object || constantKeyOf(load.getKey()).empty()) {
                    return false;
                }
                continue;
            }
            // AND THE OBJECT AS AN ARGUMENT, which is the same carrier one
            // operand along: a parameter this rewrite will hand a
            // `ctn_x *`. `argumentCensus` decided that before anything was
            // rewritten, so this is a map lookup and not a second proof.
            if (use.getOperandNumber() >= 2) {
                auto made = call.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
                if (made && slotCarriesAnObject(made, use.getOperandNumber() - 2)) { continue; }
            }
            return false;
        }
        // THE SAME TWO QUESTIONS AT THE POSITIONS call_direct PUTS THEM.
        // Its operands are the callee's entry block in order, so argument i
        // is operand 3 + i rather than 2 + i. Without this arm a literal
        // handed to a call the closed world had already named read as an
        // OPEN shape, and the object-argument lift lost it.
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
            if (use.getOperandNumber() >= 3) {
                auto made = direct.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>();
                if (made && slotCarriesAnObject(made, use.getOperandNumber() - 3)) { continue; }
            }
            return false;
        }
        return false;
    }
    return true;
}

// CONDITION 2: the one function a method call reaches, or null. Every
// literal the receiver may name must bind the key exactly once, and all of
// them must name the SAME ctjs.func - one target is one C++ signature.
ctjs::FuncOp closureLifter::resolveMethod(ctjs::CallOp call, mlir::Value & receiverOut,
                                          ctjs::GetPropertyOp & loadOut) {
    auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    if (!load || !load.getResult().hasOneUse()) { return {}; }
    const mlir::Value receiver = load.getObject();
    if (receiver != call.getReceiver()) { return {}; }
    const llvm::StringRef key = constantKeyOf(load.getKey());
    if (key.empty()) { return {}; }
    const auto objects = behind.find(receiver);
    if (objects == behind.end() || objects->second.empty()) { return {}; }
    ctjs::FuncOp target;
    for (mlir::Value object : objects->second) {
        const auto fields = methodsOf.find(object);
        if (fields == methodsOf.end()) { return {}; }
        const auto field = fields->second.find(key);
        if (field == fields->second.end()) { return {}; }
        ctjs::FuncOp named = targetOf(field->second.closure);
        if (!named || (target && named != target)) { return {}; }
        target = named;
    }
    receiverOut = receiver;
    loadOut = load;
    return target;
}

// WHY A CLOSURE STORED INTO AN OBJECT IS NOT A METHOD FIELD. Three routes,
// and only the third is "this is not a method table at all".
std::string closureLifter::whyNotAMethodField(ctjs::SetPropertyOp set) {
    const mlir::Value object = set.getObject();
    const llvm::StringRef key = constantKeyOf(set.getKey());
    if (!object.getDefiningOp<ctjs::CreateObjectOp>()) {
        return "it is stored into something that is not an object literal made here - "
               "Phase 59 slice 2";
    }
    if (key.empty()) {
        return "it is stored into an object under a key that is not a constant, so which "
               "field holds it is not known here";
    }
    if (!closedAfterLift(object)) {
        return ("it is a method field of an object whose shape is not closed, so `" + key +
                "` cannot become a free function taking that object")
            .str();
    }
    const auto fields = methodsOf.find(object);
    if (fields == methodsOf.end() || fields->second.find(key) == fields->second.end()) {
        return ("the field `" + key +
                "` is written more than once, so which function a call through it reaches "
                "depends on which store ran")
            .str();
    }
    return "it is stored into an object or an array - Phase 59 slice 2";
}

// THE RECEIVER LIFT'S CONDITION 3, ASKED OF A VALUE THAT IS NOT `this`.
// Every use has to be one a `ctn_x *` can carry, and here that is a
// constant-key read or write and nothing else.
//
// DELIBERATELY NARROWER THAN `whyThisLeaks`, WHICH ALSO ADMITS `this.m()`.
// That arm asks `resolveMethod`, whose answer depends on `behind` - a
// fixpoint that is still moving while `methodCensus` runs - so a predicate
// built on it gives one answer early in the pass and another late. This
// one is a use-list walk with no state at all, which is what lets the same
// question be asked before the census, during it, and from the lift, and
// get the same answer every time. A parameter that calls a method on its
// object is refused, by name, and that is Phase 59 slice 2's.
bool closureLifter::onlyConstantKeyAccess(mlir::Value v) {
    for (mlir::OpOperand & use : v.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0 && !constantKeyOf(get.getKey()).empty()) { continue; }
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0 && !constantKeyOf(set.getKey()).empty()) { continue; }
        }
        return false;
    }
    return true;
}

// Conditions 1 and 3, which do not move. Condition 2's `closedAfterLift`
// half is the fixpoint's, and is asked in `argumentCensus` alone.
bool closureLifter::slotIsACandidate(ctjs::CreateClosureOp c, unsigned j) {
    ctjs::FuncOp target = targetOf(c);
    if (!target || target.getBody().empty()) { return false; }
    mlir::Block & entry = target.getBody().front();
    if (3 + j >= entry.getNumArguments()) { return false; }
    bool called = false;
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        const closureCall site = callSiteOf(use, target);
        if (!site) { return false; }
        called = true;
        // A SHORT CALL IS NOT A CANDIDATE, and this half is load-bearing
        // twice over: the lift pads a missing argument with `undefined`,
        // which is not an object, and the fixpoint below would index past
        // the end asking whether it is.
        //
        // AND A CLAUSE FOR "THE ARGUMENT IS AN OBJECT LITERAL" WAS HERE AND
        // IS GONE, MEASURED. The fixpoint subsumes it exactly:
        // `closedAfterLift` returns false for anything whose defining op is
        // not a ctjs.create_object, so a slot passed a number is dropped on
        // the first round anyway. Removed, `take(o) + take(2)` refuses with
        // the same sentence, from the same place, and the whole suite stays
        // green - which is the definition of decoration.
        if (j >= site.args.size()) { return false; }
    }
    const mlir::Value parameter = entry.getArgument(3 + j);
    return called && !parameter.use_empty() && onlyConstantKeyAccess(parameter);
}

// Is JS parameter `j` of this closure's target one this rewrite will hand a
// pointer? Read by `closedAfterLift`, so it must answer from the map and
// never recompute - the map IS the fixpoint's result.
bool closureLifter::slotCarriesAnObject(ctjs::CreateClosureOp c, unsigned j) const {
    const auto at = objectSlotsOf.find(c.getOperation());
    return at != objectSlotsOf.end() && llvm::is_contained(at->second, j);
}

void closureLifter::argumentCensus() {
    for (ctjs::CreateClosureOp c : closures) {
        // A METHOD FIELD IS NOT THIS RULE'S, AND NEEDS NO CLAUSE HERE:
        // its closure value is STORED rather than called, which condition
        // 1's "every use is a call at operand 0" already refuses. That is
        // load-bearing for the ORDER - this census runs before
        // `methodCensus`, so `methodClosures` is still empty - and a
        // clause reading it here would have been silently vacuous.
        if (admissionIsDeclaration(c)) { continue; }
        ctjs::FuncOp target = targetOf(c);
        if (!target || target.getBody().empty()) { continue; }
        const unsigned parameters = target.getBody().front().getNumArguments() - 3;
        llvm::SmallVector<unsigned, 2> slots;
        for (unsigned j = 0; j < parameters; ++j) {
            if (slotIsACandidate(c, j)) { slots.push_back(j); }
        }
        if (!slots.empty()) { objectSlotsOf[c.getOperation()] = std::move(slots); }
    }
    // THE FIXPOINT, WHICH ONLY SHRINKS. A slot whose literal turns out to
    // be open is not a slot, and dropping it can open another literal that
    // was relying on it - so this repeats until nothing moves. It
    // terminates because `objectSlotsOf` never grows here.
    for (bool changed = true; changed;) {
        changed = false;
        for (ctjs::CreateClosureOp c : closures) {
            const auto at = objectSlotsOf.find(c.getOperation());
            if (at == objectSlotsOf.end()) { continue; }
            llvm::SmallVector<unsigned, 2> kept;
            for (unsigned j : at->second) {
                bool ok = true;
                for (mlir::OpOperand & use : c.getResult().getUses()) {
                    const closureCall site = callSiteOf(use, targetOf(c));
                    if (site && j < site.args.size() && !closedAfterLift(site.args[j])) {
                        ok = false;
                    }
                }
                if (ok) { kept.push_back(j); }
            }
            if (kept.size() == at->second.size()) { continue; }
            changed = true;
            if (kept.empty()) {
                objectSlotsOf.erase(c.getOperation());
            } else {
                objectSlotsOf[c.getOperation()] = std::move(kept);
            }
        }
    }
    // AND THE REASON, ONTO THE LITERAL, for every object argument this
    // rule did NOT take. It is written here rather than worked out by
    // `admission::whyOpen` because every condition that can fail is a
    // property of the CALLEE - which parameter, read how, called from
    // where - and the use-list walk that meets the escape has none of it.
    // Same idiom as `ctnative.closure_reason` and `ctnative.cell_reason`.
    llvm::SmallVector<mlir::Operation *> sites;
    for (ctjs::CallOp call : allCalls) { sites.push_back(call.getOperation()); }
    for (ctjs::CallDirectOp direct : allDirectCalls) { sites.push_back(direct.getOperation()); }
    for (mlir::Operation * site : sites) {
        ctjs::CreateClosureOp made = closureCalledBy(site);
        for (auto [j, argument] : llvm::enumerate(argsOfCallSite(site))) {
            mlir::Operation * literal = argument.getDefiningOp();
            if (!llvm::isa_and_nonnull<ctjs::CreateObjectOp>(literal)) { continue; }
            if (made && slotCarriesAnObject(made, static_cast<unsigned>(j))) { continue; }
            literal->setAttr("ctnative.object_reason",
                             mlir::StringAttr::get(
                                 context, whyNotAnObjectArgument(site, static_cast<unsigned>(j))));
        }
    }
}

// WHY AN OBJECT PASSED TO A CALL IS NOT A PARAMETER, in one sentence per
// condition. Asked only where the object really is an argument, so "it is
// passed to a call" is never the answer on its own.
std::string closureLifter::whyNotAnObjectArgument(mlir::Operation * call, unsigned j) {
    ctjs::CreateClosureOp made = closureCalledBy(call);
    if (!made) {
        return "it is passed to a call whose callee is not one function this rewrite can "
               "name, so there is no parameter to give the object's address to";
    }
    // A METHOD-FIELD CLAUSE WAS HERE AND IS GONE, FOR TWO REASONS. It was
    // VACUOUS - this runs at the end of `argumentCensus`, which is before
    // `methodCensus`, so `methodClosures` is still empty - and it was
    // REDUNDANT: a closure stored into a literal has a use that is not a
    // call, which the loop at the bottom names better ("used as a value
    // elsewhere") than "it is a method field" would. Both were measured on
    // the same program.
    ctjs::FuncOp target = targetOf(made);
    if (!target || target.getBody().empty() ||
        3 + j >= target.getBody().front().getNumArguments()) {
        return "it is passed in an argument position the callee has no parameter for - the "
               "surplus has frame semantics";
    }
    const mlir::Value parameter = target.getBody().front().getArgument(3 + j);
    if (parameter.use_empty()) {
        return "it is passed to a parameter nothing reads, and an object parameter that is "
               "never read is `-Wunused-parameter` in the generated C++";
    }
    if (!onlyConstantKeyAccess(parameter)) {
        return "it is passed to a parameter that reaches it through something other than a "
               "constant key - that needs an owner, and this slice introduces none";
    }
    for (mlir::OpOperand & use : made.getResult().getUses()) {
        const closureCall other = callSiteOf(use, target);
        // A SENTENCE FOR "THE CLOSURE IS USED AS A VALUE ELSEWHERE" WAS
        // HERE AND IS GONE, BECAUSE NO PROGRAM REACHES IT. Every use of a
        // closure that is not a call of it is already refused by
        // `whyNotLiftable`, and that refusal lands on this same function
        // and is reported first: measured on `kept = take; take(o);`,
        // which says "a closure used as a value: it is stored to a global"
        // and never asks this question. The cast still needs an else.
        if (!other) { continue; }
        const mlir::ValueRange args = other.args;
        if (j >= args.size() || !args[j].getDefiningOp<ctjs::CreateObjectOp>()) {
            return "it is passed to a parameter that is an object literal at this call and "
                   "something else at another, so the parameter has no single C++ type";
        }
    }
    return "it is passed to a parameter whose other object literal is not itself a closed "
           "shape, so the two would not agree on one class";
}

void closureLifter::methodCensus() {
    for (ctjs::CreateObjectOp object : objects) {
        if (!closedAfterLift(object.getResult())) { continue; }
        llvm::StringMap<unsigned> writes;
        llvm::StringMap<methodField> fields;
        for (mlir::Operation * user : object.getResult().getUsers()) {
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            if (!set || set.getObject() != object.getResult()) { continue; }
            ++writes[constantKeyOf(set.getKey())];
            if (auto made = set.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
                fields[constantKeyOf(set.getKey())] = methodField{set, made};
            }
        }
        llvm::StringMap<methodField> & into = methodsOf[object.getResult()];
        for (const auto & entry : fields) {
            // A KEY WRITTEN TWICE IS NOT A METHOD, whatever the second
            // write holds: `o.f = g` after `var o = {f: h}` makes the
            // callee depend on which store ran, which is exactly what
            // condition 2 forbids. The key is simply not admitted, and the
            // call through it stays a ctjs.call - refused by name below.
            if (writes[entry.first()] == 1) { into[entry.first()] = entry.second; }
        }
        behind[object.getResult()].push_back(object.getResult());
    }
    // THE FIXPOINT OVER THE RECEIVER CHAIN. `this.other()` inside a method
    // has `%arg0` for a receiver, and `%arg0` names whatever the call sites
    // pass - which is only known once those call sites resolve. One round
    // per link in the chain, and it terminates because `behind` only grows
    // and is bounded by the literals in the module.
    for (bool changed = true; changed;) {
        changed = false;
        for (ctjs::CallOp call : allCalls) {
            mlir::Value receiver;
            ctjs::GetPropertyOp load;
            ctjs::FuncOp target = resolveMethod(call, receiver, load);
            if (!target || target.getBody().empty() ||
                target.getBody().front().getNumArguments() < 3) {
                continue;
            }
            llvm::SmallVector<mlir::Value, 2> & named =
                behind[target.getBody().front().getArgument(0)];
            for (mlir::Value object : behind.lookup(receiver)) {
                if (!llvm::is_contained(named, object)) {
                    named.push_back(object);
                    changed = true;
                }
            }
        }
    }
    // AND THE CALLS THEMSELVES, once `behind` has stopped moving.
    for (ctjs::CallOp call : allCalls) {
        mlir::Value receiver;
        ctjs::GetPropertyOp load;
        ctjs::FuncOp target = resolveMethod(call, receiver, load);
        if (!target || target.getBody().empty() || target.getBody().front().getNumArguments() < 3) {
            continue;
        }
        callsOfTarget[target.getOperation()].push_back(methodCall{call, load, receiver, target});
    }
    for (const auto & entry : methodsOf) {
        for (const auto & field : entry.second) {
            ctjs::CreateClosureOp bound = field.second.closure;
            methodClosures.insert(bound.getOperation());
        }
    }
    // THE CENSUS LAST, because two of its labels ask questions - "is this
    // call one the rewrite resolves", "does that parameter escape" - whose
    // answers are only settled once `behind` and `methodsOf` have stopped
    // moving. Asking during the first loop undercounted by exactly the
    // chained receivers.
    if (censusOn) {
        for (ctjs::CreateObjectOp object : objects) {
            if (!closedAfterLift(object.getResult())) { censusOpenLiteral(object.getResult()); }
        }
    }
}

// CONDITION 3: what the target does with `this`. Every use has to be
// something the receiver parameter can carry - a constant-key read or
// write, or the receiver of another method call - and every route out of
// the function is named, because "it leaks `this`" is not a work item and
// "it is returned" is.
std::optional<std::string> closureLifter::whyThisLeaks(ctjs::FuncOp target) {
    mlir::Block & entry = target.getBody().front();
    for (mlir::OpOperand & use : entry.getArgument(0).getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0 && !constantKeyOf(get.getKey()).empty()) { continue; }
            return "it reads `this` through a dynamic key";
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0 && !constantKeyOf(set.getKey()).empty()) { continue; }
            if (use.getOperandNumber() == 2) {
                return "it stores `this` into another object - that needs an owner, and this "
                       "slice introduces none";
            }
            return "it writes `this` through a dynamic key";
        }
        if (llvm::isa<ctjs::ReturnOp>(user)) {
            return "it returns `this` - the receiver is the caller's frame, so returning it "
                   "would outlive the object";
        }
        if (llvm::isa<ctjs::StoreGlobalOp>(user)) { return "it stores `this` into a global"; }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            // `this.other()`: the receiver of a call this rewrite also
            // makes direct. Any other position is `this` passed as an
            // argument, which has no call site to move it to.
            mlir::Value receiver;
            ctjs::GetPropertyOp load;
            if (use.getOperandNumber() == 1 && resolveMethod(call, receiver, load)) { continue; }
            return "it passes `this` to a call this rewrite cannot make direct";
        }
        // AND A CALL --ctjs-resolve-globals ALREADY MADE DIRECT, which is
        // what `f(this)` is by the time this rewrite runs: the closed world
        // named that callee long before, so the operand sits on a
        // ctjs.call_direct and not on a ctjs.call. Without this arm the
        // commonest way there is to leak a receiver got the default
        // sentence - "it reaches `ctjs.call_direct`" - which names the
        // operation and not the mistake.
        if (llvm::isa<ctjs::CallDirectOp>(user)) {
            return "it passes `this` as an argument to another function - a receiver moves "
                   "to the CALL SITE, and an argument position has none to move to (that "
                   "needs a specialised callee, Phase 63, not a lift)";
        }
        return ("it reaches `" + user->getName().getStringRef() +
                "`, which slice 1 does not carry a receiver through")
            .str();
    }
    return std::nullopt;
}

// The method form of whyNotLiftable: conditions 1 to 4, one sentence each.
// The capture and own-closure clauses are the closure lift's, unchanged -
// a method IS a closure in the IR, and the two rules compose.
std::optional<std::string> closureLifter::whyNotLiftableMethod(ctjs::CreateClosureOp c) {
    // THE ARROW GUARD FIRST, and the rest of the target's validity with it:
    // an arrow's `this` is lexical, every use of it reads as a legal
    // constant-key access to condition 3, and admitting one would rebind
    // `this` to the object and answer wrongly rather than refuse.
    if (const std::optional<std::string> why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    // CONDITION 2, the other half: the closure value is used for method
    // stores and nothing else. `methodClosures` says at least one store is
    // one; this says none of them is anything else.
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
        if (!set || use.getOperandNumber() != 2) {
            return "it is a method field that is also used as a value elsewhere - Phase 59 "
                   "slice 2";
        }
        const auto fields = methodsOf.find(set.getObject());
        if (fields == methodsOf.end() ||
            fields->second.lookup(constantKeyOf(set.getKey())).closure != c) {
            return whyNotAMethodField(set);
        }
    }
    // CONDITION 3.
    if (const std::optional<std::string> leak = whyThisLeaks(target)) { return leak; }
    // AND EVERY CALL OF IT IS ONE THIS RESOLVES. A method field nothing
    // calls has nowhere to move the receiver to, and a call the resolution
    // above could not name would be left dispatching through a closure
    // that is about to lower to nothing.
    const auto calls = callsOfTarget.find(target.getOperation());
    if (calls == callsOfTarget.end()) {
        // A METHOD READ AS A VALUE. `var g = o.m;` loads the field and does
        // not call it, so there is no call site for the receiver to move
        // to and the closure would have to become a value that carries one
        // - a bound function, which is an owner this slice does not build.
        for (mlir::OpOperand & use : c.getResult().getUses()) {
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            if (!set) { continue; }
            const llvm::StringRef key = constantKeyOf(set.getKey());
            for (mlir::Operation * user : set.getObject().getUsers()) {
                auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                if (get && constantKeyOf(get.getKey()) == key) {
                    return ("its field `" + key +
                            "` is read as a value rather than called - a method used as a "
                            "function value has to carry its receiver, which is a bound "
                            "function and an owner this slice does not build")
                        .str();
                }
            }
        }
        return "nothing calls it";
    }
    const unsigned parameters = entry.getNumArguments() - 3;
    for (methodCall at : calls->second) {
        if (at.call.getArgs().size() > parameters) {
            return "a call passes " + std::to_string(at.call.getArgs().size()) +
                   " argument(s) to " + std::to_string(parameters) +
                   " parameter(s) - the surplus has frame semantics";
        }
        auto caller = at.call->getParentOfType<ctjs::FuncOp>();
        if (caller && passesNewTarget.contains(caller.getOperation())) {
            return "a call of it sits in a function that passes new.target";
        }
        // CONDITION 1 AT THE CALL SITE, not only at the literal: the
        // receiver is either a literal this proved closed, or the `%arg0`
        // of a method whose own receivers are.
        if (behind.lookup(at.receiver).empty()) {
            return "it is called on a receiver whose shape is not a proved closed literal";
        }
    }
    // CONDITION 4: the target is otherwise native - which is the existing
    // call-graph fixpoint's question - and its captures lift as Phase 59
    // slice 1 already lifts them. LAST, so that a leaked `this` is reported
    // as a leaked `this` and not as whatever the enclosing frame happened
    // to box for it: `box.held = this` inside a method captures `box`, and
    // a hoisted `var` is always a cell that is written, so asking the
    // capture clause first answered "capture 0 is a binding that is
    // reassigned" for a program whose actual problem is the receiver.
    if (const std::optional<std::string> why = whyCapturesDoNotLift(c)) { return why; }
    // AND THE CAPTURED VALUES REACH THE SITES THIS LIFT IS ABOUT TO
    // REWRITE - which for a method are the calls through the object, not
    // the uses of the closure value.
    for (methodCall at : calls->second) {
        if (const std::optional<std::string> why = whyCapturesDoNotReach(c, at.call)) {
            return why;
        }
    }
    if (const std::optional<std::string> escapes = whyOwnClosureEscapes(target)) { return escapes; }
    return whyUpvalueReadsDoNotLift(c, target);
}

} // namespace ctcompile::ctnative::lowering_detail
