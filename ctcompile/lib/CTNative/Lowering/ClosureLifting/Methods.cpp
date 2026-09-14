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
// Ask the complete constructor proof, not just the diagnostic census. Its
// shape check reads objectSlotsOf without recursing here; argumentCensus's
// shrinking fixpoint removes any slots whose constructor dependencies fail.
bool closureLifter::makesAnInstance(mlir::Value object) {
    auto built = object.getDefiningOp<ctjs::ConstructOp>();
    if (!built) { return false; }
    auto closure = built.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
    if (!closure || !constructorClosures.contains(closure.getOperation())) { return false; }
    return !whyNotLiftableConstructor(closure);
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
            if (use.getOperandNumber() != 0 || ctjs::constantKey(get.getKey()).empty()) {
                return false;
            }
            continue;
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() != 0 || ctjs::constantKey(set.getKey()).empty()) {
                return false;
            }
            continue;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            // The object as the RECEIVER of a call whose callee is a
            // constant-key read of that same object: a method call.
            if (use.getOperandNumber() == 1) {
                // Borrowed parameters still have no method-resolution proof.
                if (llvm::isa<mlir::BlockArgument>(object)) { return false; }
                auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (!load || load.getObject() != object ||
                    ctjs::constantKey(load.getKey()).empty()) {
                    return false;
                }
                continue;
            }
            // AND THE OBJECT AS AN ARGUMENT, which is the same carrier one
            // operand along: a parameter this rewrite will hand a
            // `ctn_x *`. `argumentCensus` decided that before anything was
            // rewritten, so this is a map lookup and not a second proof.
            if (use.getOperandNumber() >= 2) {
                auto made = closureCalledBy(call);
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
                auto made = closureCalledBy(direct);
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
    const llvm::StringRef key = ctjs::constantKey(load.getKey());
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
    const llvm::StringRef key = ctjs::constantKey(set.getKey());
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

// Diagnostic distinction between plain field access and a use that needs
// another proof. Forwarding is proved by argumentCensus's shrinking graph;
// this state-free label does not authorize it or resolve borrowed methods.
bool closureLifter::onlyConstantKeyAccess(mlir::Value v) {
    for (mlir::OpOperand & use : v.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0 && !ctjs::constantKey(get.getKey()).empty()) {
                continue;
            }
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0 && !ctjs::constantKey(set.getKey()).empty()) {
                continue;
            }
        }
        return false;
    }
    return true;
}

// Include every symbolic call, not only uses of the closure value. The local
// binding proof can erase that value from calls in another function.
llvm::SmallVector<closureLifter::closureCall> closureLifter::objectArgumentCalls(
    ctjs::CreateClosureOp c) {
    ctjs::FuncOp target = targetOf(c);
    if (!target) { return {}; }
    llvm::SmallVector<closureCall> calls;
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        const auto site = callSiteOf(use, target);
        if (!site) { return {}; }
        calls.push_back(site);
    }
    const auto symbols = mlir::SymbolTable::getSymbolUses(target, module);
    if (!symbols) { return {}; }
    for (const auto & use : *symbols) {
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getUser());
        if (!direct || direct.getTarget() != target || closureCalledBy(direct) != c) { return {}; }
        if (direct.getCalleeValue() != c.getResult()) {
            calls.push_back({direct, direct.getArgs(), direct.getReceiver()});
        }
    }
    return calls;
}

// The callable and arity census does not move. All object use and origin
// dependencies are checked by the shrinking argumentCensus fixpoint.
bool closureLifter::slotIsACandidate(ctjs::CreateClosureOp c, unsigned j) {
    ctjs::FuncOp target = targetOf(c);
    if (!target || target.getBody().empty()) { return false; }
    mlir::Block & entry = target.getBody().front();
    if (3 + j >= entry.getNumArguments()) { return false; }
    const auto calls = objectArgumentCalls(c);
    for (const closureCall & site : calls) {
        // A SHORT CALL IS NOT A CANDIDATE, and this half is load-bearing
        // twice over: the lift pads a missing argument with `undefined`,
        // which is not an object, and the fixpoint below would index past
        // the end asking whether it is.
        if (j >= site.args.size()) { return false; }
    }
    const mlir::Value parameter = entry.getArgument(3 + j);
    return !calls.empty() && !parameter.use_empty();
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
        const unsigned parameters =
            target.getBody().front().getNumArguments() - ctjs::implicit_arguments;
        llvm::SmallVector<unsigned, 2> slots;
        for (unsigned j = 0; j < parameters; ++j) {
            if (slotIsACandidate(c, j)) { slots.push_back(j); }
        }
        if (!slots.empty()) { objectSlotsOf[c.getOperation()] = std::move(slots); }
    }
    const auto closedArgument = [&](mlir::Value value) {
        if (closedAfterLift(value)) { return true; }
        auto parameter = llvm::dyn_cast<mlir::BlockArgument>(value);
        if (!parameter || !parameter.getOwner()->isEntryBlock() ||
            parameter.getArgNumber() < ctjs::implicit_arguments) {
            return false;
        }
        auto owner = llvm::dyn_cast<ctjs::FuncOp>(parameter.getOwner()->getParentOp());
        bool found = false;
        for (ctjs::CreateClosureOp made : closures) {
            if (targetOf(made) != owner) { continue; }
            if (!slotCarriesAnObject(made, parameter.getArgNumber() - ctjs::implicit_arguments)) {
                return false;
            }
            found = true;
        }
        return found && usesCloseTheShape(value);
    };
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
                const auto parameter = targetOf(c).getBody().front().getArgument(3 + j);
                const auto calls = objectArgumentCalls(c);
                bool ok = !calls.empty() && usesCloseTheShape(parameter);
                for (const closureCall & site : calls) {
                    if (j >= site.args.size() || !closedArgument(site.args[j])) { ok = false; }
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
    llvm::DenseSet<mlir::Operation *> instanceStores;
    for (ctjs::ConstructOp built : allConstructs) {
        if (!closedAfterLift(built.getResult())) { continue; }
        for (mlir::Operation * user : built.getResult().getUsers()) {
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            if (!set || set.getObject() != built.getResult() ||
                !set.getValue().getDefiningOp<ctjs::CreateClosureOp>() ||
                set->getBlock() != built->getBlock()) {
                continue;
            }
            // Every method binding precedes every observation. This also
            // excludes argument aliases, whose writes need a separate proof.
            const bool initialized =
                llvm::all_of(built.getResult().getUses(), [&](mlir::OpOperand & use) {
                    auto * op = use.getOwner();
                    if (use.getOperandNumber() == 0 && llvm::isa<ctjs::SetPropertyOp>(op)) {
                        return true;
                    }
                    return op->getBlock() == set->getBlock() && set->isBeforeInBlock(op) &&
                           ((use.getOperandNumber() == 0 && llvm::isa<ctjs::GetPropertyOp>(op)) ||
                            (use.getOperandNumber() == 1 && llvm::isa<ctjs::CallOp>(op)));
                });
            if (initialized) { instanceStores.insert(set); }
        }
    }
    const auto collect = [&](mlir::Value object, bool constructed) {
        if (!closedAfterLift(object)) { return; }
        llvm::StringMap<unsigned> writes;
        llvm::StringMap<methodField> fields;
        for (mlir::Operation * user : object.getUsers()) {
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            if (!set || set.getObject() != object) { continue; }
            ++writes[ctjs::constantKey(set.getKey())];
            if (auto made = set.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
                if (constructed && !instanceStores.contains(set)) { continue; }
                fields[ctjs::constantKey(set.getKey())] = methodField{set, made};
            }
        }
        llvm::StringMap<methodField> & into = methodsOf[object];
        for (const auto & entry : fields) {
            // A KEY WRITTEN TWICE IS NOT A METHOD, whatever the second
            // write holds: `o.f = g` after `var o = {f: h}` makes the
            // callee depend on which store ran, which is exactly what
            // condition 2 forbids. The key is simply not admitted, and the
            // call through it stays a ctjs.call - refused by name below.
            if (writes[entry.first()] != 1) { continue; }
            bool stable = true;
            if (constructed) {
                // ponytail: a whole-module key census conservatively includes
                // unrelated objects; use an alias proof if that ceiling matters.
                // In particular, constructor and borrowed receiver writes count.
                module.walk([&](ctjs::SetPropertyOp set) {
                    const auto key = ctjs::constantKey(set.getKey());
                    if (key.empty() || (key == entry.first() && !instanceStores.contains(set))) {
                        stable = false;
                    }
                });
            }
            if (stable) { into[entry.first()] = entry.second; }
        }
        behind[object].push_back(object);
    };
    for (ctjs::CreateObjectOp object : objects) { collect(object.getResult(), false); }
    for (ctjs::ConstructOp built : allConstructs) { collect(built.getResult(), true); }
    // Constructor calls precede every store to the constructed result. Seed
    // their receiver from the proved prototype alone, never from those stores.
    // The structural check omits only receiver resolution; full constructor
    // admission rechecks that after this census reaches its fixpoint.
    for (ctjs::ConstructOp built : allConstructs) {
        auto closure = built.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
        if (!closure || whyConstructorSetupDoesNotLift(closure)) { continue; }
        auto prototype = immutablePrototype(closure);
        if (!prototype) { continue; }
        mlir::Value origin = prototype->attachment.getValue();
        for (ctjs::SetPropertyOp field : prototype->fields) {
            auto method = field.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            if (!method) { continue; }
            const auto key = ctjs::constantKey(field.getKey());
            bool stable = true;
            // As with post-construction methods, all receiver writes count.
            module.walk([&](ctjs::SetPropertyOp set) {
                auto written = ctjs::constantKey(set.getKey());
                if (written.empty() || (written == key && set != field)) { stable = false; }
            });
            if (!stable) { continue; }
            methodsOf[origin][key] = methodField{field, method};
            methodsOf[built.getResult()][key] = methodField{field, method};
        }
        behind[built.getResult()] = {built.getResult()};
        auto receiver = targetOf(closure).getBody().front().getArgument(ctjs::arg_receiver);
        if (!llvm::is_contained(behind[receiver], origin)) { behind[receiver].push_back(origin); }
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
            if (use.getOperandNumber() == 0 && !ctjs::constantKey(get.getKey()).empty()) {
                continue;
            }
            return "it reads `this` through a dynamic key";
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0 && !ctjs::constantKey(set.getKey()).empty()) {
                continue;
            }
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
            fields->second.lookup(ctjs::constantKey(set.getKey())).closure != c) {
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
            const llvm::StringRef key = ctjs::constantKey(set.getKey());
            for (mlir::Operation * user : set.getObject().getUsers()) {
                auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                if (get && ctjs::constantKey(get.getKey()) == key) {
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
    const unsigned parameters = entry.getNumArguments() - ctjs::implicit_arguments;
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
