// ClosureLifting/Constructors.cpp - native lowering implementation.
#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {

// --- the constructor lift -----------------------------------------------

// WHICH CLOSURES ARE USED AS `new` CALLEES AND AS NOTHING ELSE.
//
// A closure that is BOTH called and constructed is left to
// `whyNotLiftable`, which refuses it by name ("it is used as a
// constructor"): one ctjs.func is one C++ signature, and a body that is a
// free function at one site and a constructor at another would need two.
void closureLifter::constructorCensus() {
    llvm::DenseSet<mlir::Operation *> constructed;
    for (ctjs::ConstructOp made : allConstructs) {
        auto closure = made.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
        if (!closure) { continue; }
        constructed.insert(closure.getOperation());
        if (ctjs::FuncOp target = targetOf(closure)) {
            constructsOfTarget[target.getOperation()].push_back(made);
        }
    }
    // EVERY CLOSURE A `new` NAMES, whatever else it does. The narrower set
    // - only those used as nothing but a `new` callee - was the wrong one:
    // a closure that is ALSO written through (`Shape.prototype = {...}`)
    // then fell to `whyNotLiftable`, which met the new.target operand first
    // and answered "it is passed as an argument". The clause a reader needs
    // is the prototype, and only the constructor rule knows to say so.
    constructorClosures.insert(constructed.begin(), constructed.end());
}

// IS THIS VALUE PROVABLY NOT `is_object_like()`?
//
// THE QUESTION `new` ASKS OF A RETURN, and the reason it has to be asked.
// `context::construct` ends `return produced.is_object_like() ? produced :
// self` (vm/call.cpp:695, and :685 for a native), so a constructor that
// returns an object REPLACES the instance and `new X()` is not the struct
// this rewrite built at all. `is_object_like()` is
// `is_object() || is_array() || is_callable() || is_kind(proxy)`
// (Script/value.hpp:162-164) - note a STRING is not one, so
// `return "done"` is harmless and still evaluates to the instance.
//
// THE ANSWER IS "NO" UNLESS THE DEFINING OPERATION SAYS OTHERWISE, which is
// the direction that keeps this sound: a block argument, a call result, a
// property read and anything this list does not name are all assumed to be
// able to carry an object.
bool closureLifter::isNotObjectLike(mlir::Value v) {
    mlir::Operation * definition = v.getDefiningOp();
    if (definition == nullptr) { return false; } // a block argument: unknown
    // ctjs.constant carries undefined, null, a string, a number or a
    // boolean and nothing else; the arithmetic, predicate and conversion
    // operations all answer primitives.
    return llvm::isa<ctjs::ConstantOp, ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp,
                     ctjs::CompareOp, ctjs::TruthyOp, ctjs::FromBoolOp, ctjs::InstanceOfOp,
                     ctjs::HasPropertyOp, ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(definition);
}

// GUARD 3: the body must not hand back an object.
//
// SHADOWED FOR THE OUTCOME AND NOT FOR THE DIAGNOSTIC, measured. Removing
// this clause does not admit `returns-object.js`: the `{v: 9}` it hands
// back is then refused as "an object literal that escapes - it is
// returned", and an array by the array form of the same sentence. Every
// object-like value this tier can build already has an escape rule, so no
// program could be found where this clause alone decides. It is kept, and
// it runs FIRST, because it is the only one that names what `new` does -
// and because the clause that shadows it is exactly what Phase 59 slice 2
// exists to relax, after which this is the only thing between a returning
// constructor and a wrong answer.
std::optional<std::string> closureLifter::whyConstructorReturnsAnObject(ctjs::FuncOp target) {
    std::optional<std::string> bad;
    target.getBody().walk([&](ctjs::ReturnOp returned) {
        if (!bad && !isNotObjectLike(returned.getValue())) {
            bad = "its constructor returns a value this pass cannot prove is not an object, "
                  "and a constructor that returns one REPLACES the instance "
                  "(context::construct: `produced.is_object_like() ? produced : self`) - so "
                  "`new` would not evaluate to the struct this rewrite builds";
        }
    });
    return bad;
}

// GUARD 5: nothing may touch the constructor function's `prototype`.
//
// SHADOWED THE SAME WAY, AND KEPT FOR THE SAME REASON. Removing it leaves
// `prototype-written.js` refused by the mixed-use clause below - "it is
// used as a constructor and also reaches `ctjs.set_property`" - which is
// true and tells a reader nothing about what to do next. This one names
// Stage 60A.
//
// Refused BY NAME rather than falling through to the generic "used as a
// value elsewhere", because the work item behind it is a specific one: the
// plan's Stage 60A immutability proof, which is what turns a prototype
// table into a base class. Without it an instance here has NO chain, so a
// program that writes `X.prototype.m = ...` and calls `o.m()` would compile
// to a struct with no `m` at all.
std::optional<std::string> closureLifter::whyPrototypeIsTouched(ctjs::CreateClosureOp c) {
    for (mlir::Operation * user : c.getResult().getUsers()) {
        llvm::StringRef key;
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            key = constantKeyOf(get.getKey());
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            key = constantKeyOf(set.getKey());
        }
        if (key == "prototype") {
            return "its constructor's `prototype` is read or written, which is a prototype "
                   "chain this slice does not build - Stage 60A's immutability proof owns it";
        }
    }
    return std::nullopt;
}

// THE CONSTRUCTOR FORM OF whyNotLiftable, one sentence per clause. Every
// clause it shares with the method lift is CALLED rather than re-typed:
// the instance is a literal and the constructor is a receiver carrier, so
// the two rules are the same rule reached through `new`.
std::optional<std::string> closureLifter::whyNotLiftableConstructor(ctjs::CreateClosureOp c) {
    // THE ARROW GUARD FIRST, as the method form does: an arrow's `this` is
    // lexical, so every use of it reads as a legal constant-key access to
    // the receiver clause below and admitting one would answer wrongly
    // rather than refuse. (An arrow is not a constructor in the VM either -
    // `ensure_prototype` returns undefined for one.)
    if (const std::optional<std::string> why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    mlir::Block & entry = target.getBody().front();
    const unsigned parameters = entry.getNumArguments() - 3;
    // GUARD 5, before the generic clauses, so the diagnostic names the
    // prototype rather than whatever else the read happens to be.
    if (const std::optional<std::string> why = whyPrototypeIsTouched(c)) { return why; }
    // AND THE CLOSURE IS USED FOR NOTHING BUT `new`. One ctjs.func is one
    // C++ signature, so a body that is a free function at one site and a
    // constructor at another needs two.
    //
    // `new X(a)` USES THE CLOSURE TWICE ON ONE OPERATION - as the callee
    // AND as new.target, which is what the VM pushes for a construct - so
    // this counts operations and not operands.
    for (mlir::Operation * user : c.getResult().getUsers()) {
        if (!llvm::isa<ctjs::ConstructOp>(user)) {
            return ("it is used as a constructor and also reaches `" +
                    user->getName().getStringRef() +
                    "` - one ctjs.func is one C++ signature, and a body that is a free "
                    "function at one site and a constructor at another needs two")
                .str();
        }
    }
    // GUARD 2: `this` never escapes the constructor, and every use of it is
    // a constant-key access. Word for word the receiver carrier's
    // condition 3, because it IS that condition.
    if (const std::optional<std::string> leak = whyThisLeaks(target)) { return leak; }
    // GUARD 3.
    if (const std::optional<std::string> why = whyConstructorReturnsAnObject(target)) {
        return why;
    }
    const auto sites = constructsOfTarget.find(target.getOperation());
    if (sites == constructsOfTarget.end() || sites->second.empty()) {
        return "nothing constructs it";
    }
    for (ctjs::ConstructOp made : sites->second) {
        // GUARD 1 AT EVERY SITE, not only at this one: a target is one C++
        // signature, so a second `new` through a callee this pass cannot
        // name would leave that site dispatching through a closure which is
        // about to lower to nothing.
        if (made.getCallee().getDefiningOp<ctjs::CreateClosureOp>() != c) {
            return "it is constructed at a site whose callee this pass cannot name";
        }
        // GUARD 4: the instance's own uses close its shape, by the literal
        // rule, asked of the value the rewrite is about to make a literal.
        if (!usesCloseTheShape(made.getResult())) {
            return "the instance `new` makes does not have a closed shape - every use of it "
                   "has to be a constant-key read or write, or a receiver this lift carries";
        }
        // A CLAUSE WAS HERE AND IS GONE, MEASURED: "the instance's `k` is
        // read but never assigned". It refused every instance that reads a
        // key the constructor does not write, on the grounds that the VM
        // would find it on the prototype. THE HAZARD IS ALREADY COVERED
        // AND MORE PRECISELY: admission's own clause names the keys
        // Object.prototype answers ("field `constructor` is read but never
        // written, and Object.prototype answers that name - the interpreter
        // finds a function where this would find undefined"), and every
        // OTHER unwritten key is undefined in the VM too, exactly as it is
        // for a literal. Removing it, `unwritten-key.js` compiles with no
        // refusal at all and agrees with the interpreter; the clause was
        // strictly narrowing the tier for nothing.
        if (made.getArgs().size() > parameters) {
            return "a `new` passes " + std::to_string(made.getArgs().size()) + " argument(s) to " +
                   std::to_string(parameters) + " parameter(s) - the surplus has frame semantics";
        }
        auto caller = made->getParentOfType<ctjs::FuncOp>();
        if (caller && passesNewTarget.contains(caller.getOperation())) {
            return "a `new` of it sits in a function that passes new.target";
        }
    }
    if (const std::optional<std::string> why = whyCapturesDoNotLift(c)) { return why; }
    // AND THE CAPTURED VALUES REACH EVERY `new` SITE, which the clause
    // above has just established are the only users of the closure value.
    for (mlir::Operation * user : c.getResult().getUsers()) {
        if (const std::optional<std::string> why = whyCapturesDoNotReach(c, user)) { return why; }
    }
    if (const std::optional<std::string> escapes = whyOwnClosureEscapes(target)) { return escapes; }
    return whyUpvalueReadsDoNotLift(c, target);
}

} // namespace ctcompile::ctnative::lowering_detail
