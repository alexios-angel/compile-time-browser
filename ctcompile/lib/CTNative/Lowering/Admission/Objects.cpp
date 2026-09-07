// Admission/Objects.cpp - native lowering implementation.
#include "../../Analysis/OwnedGlobalRoots.h"
#include "Admission.h"

namespace ctcompile::ctnative::lowering_detail {

bool admission::ownedGlobalValue(mlir::Value value) const {
    auto * operation = value.getDefiningOp();
    const auto * root = ownedGlobals ? ownedGlobals->lookup(operation) : nullptr;
    if (!root || !llvm::isa<ctjs::CreateObjectOp, ctjs::LoadGlobalOp>(operation)) { return false; }
    auto type = llvm::dyn_cast_or_null<GlobalObjectType>(typeOf(value));
    return type && type.getBinding() == root->binding;
}

bool admission::ownedGlobalOperation(mlir::Operation * operation) {
    const auto * root = ownedGlobals->lookup(operation);
    if (!isCIdentifier(root->binding) || !isCIdentifier(root->property) ||
        isReservedInCpp(root->property)) {
        return refuse("owned global binding and field need supported C++ identifiers");
    }
    auto made = root->owner;
    auto write = root->fieldInitialization;
    if (!ownedGlobalValue(made.getResult()) ||
        carrierOf(typeOf(write.getValue())) != carrier::number) {
        return refuse("owned global root needs a proved owner and one definite numeric field");
    }
    for (ctjs::LoadGlobalOp load : root->loads) {
        if (!ownedGlobalValue(load.getResult())) {
            return refuse("owned global load lacks its definite allocation identity");
        }
    }
    for (ctjs::GetPropertyOp read : root->reads) {
        if (typeOf(read.getResult()) != typeOf(write.getValue())) {
            return refuse("owned global field read lacks its definite initialized type");
        }
    }
    return true;
}

// PHASE 56: A CLOSED SHAPE IS A STRUCT BY VALUE. TypeInference::hasClosedShape
// is the proof - every use is a get or set through a constant key, so the
// object never reaches anything that could add or remove a field, and never
// leaves the frame (a return, a store, a call would all be uses that open
// it). Each key must be a C identifier, each field a number or a boolean.
bool admission::isClosedObject(mlir::Value v) {
    return TypeInference::hasClosedShape(v);
}

// WHY A LITERAL'S SHAPE IS OPEN: the first use that is not a get or a set
// through a constant key on the literal itself, named by what it is. A
// refusal that lists every route there is tells the reader nothing about
// which one this program took; this one names it.
//
// THE LOOP-CARRIED ROW IS THE ONE OBLIGATION O-3 LEAVES. --ctjs-lift-to-scf
// now replaces a loop header's argument for a variable assigned once
// before the loop by the variable (a trivial phi), so a literal updated
// inside a loop is one SSA value and one stack slot. What still reaches
// the scf.while is a REAL phi: the variable is assigned again inside the
// loop, or on only one path before it, and two literals - two shapes,
// two slots - would have to become one value, which is a pointer.
// THE SENTENCE THE ARGUMENT RULE LEFT ON THE LITERAL, or empty. Written by
// `closureLifter::argumentCensus` before any rewrite ran, for the same
// reason `ctnative.closure_reason` and `ctnative.cell_reason` are: every
// condition that can fail is a property of the CALLEE, and a walk that
// meets the escape has only the use.
std::string admission::argumentReason(mlir::Value object) {
    mlir::Operation * literal = object.getDefiningOp();
    auto why =
        literal ? literal->getAttrOfType<mlir::StringAttr>("ctnative.object_reason") : nullptr;
    return why ? "an object literal that escapes - " + why.getValue().str() : std::string{};
}

std::string admission::whyOpen(mlir::Value object) {
    for (mlir::OpOperand & use : object.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0) {
                if (!keyOf(get.getKey()).empty()) { continue; }
                return "an object literal reached through a dynamic key";
            }
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (use.getOperandNumber() == 0) {
                if (!keyOf(set.getKey()).empty()) { continue; }
                return "an object literal reached through a dynamic key";
            }
            if (use.getOperandNumber() == 2) {
                return "an object literal that escapes - it is stored into another object";
            }
        }
        if (llvm::isa<mlir::scf::WhileOp, mlir::scf::YieldOp, mlir::scf::ConditionOp>(user)) {
            return "an object literal that is loop-carried - more than one value reaches "
                   "the variable that holds it (assigned again inside a loop, or on only "
                   "one path before it)";
        }
        if (llvm::isa<ctjs::ReturnOp>(user)) {
            return "an object literal that escapes - it is returned";
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            // A METHOD CALL THE RECEIVER LIFT DID NOT TAKE. It is still a
            // ctjs.call, so the object is passed to a dispatcher this tier
            // cannot see through; the reason the lift gave is on the
            // closure the field holds, and this points at it.
            if (use.getOperandNumber() == 1) {
                return "an object literal whose method call was not lifted - it is still "
                       "dispatched through the callee value, so the object is passed to "
                       "something that could add or remove a field";
            }
            // AN ARGUMENT THE OBJECT-PARAMETER RULE DID NOT TAKE, and the
            // rule's own sentence rather than this one: `argumentCensus`
            // wrote it onto the literal, because the condition that failed
            // is a property of the CALLEE and this walk only has the use.
            if (const std::string why = argumentReason(object); !why.empty()) { return why; }
            return "an object literal that escapes - it is passed to a call";
        }
        if (llvm::isa<ctjs::CallDirectOp>(user)) {
            // A DIRECT CALL CARRIES A RECEIVER AND ANY PARAMETER THE LIFT
            // LISTED; anything else in an operand is still an escape.
            if (use.getOperandNumber() == 0 && user->hasAttr("ctnative.receiver")) { continue; }
            if (isObjectArg(user, use.getOperandNumber())) { continue; }
            // AND THIS IS WHERE THE ARGUMENT REASON USUALLY LANDS, NOT THE
            // ctjs.call ARM ABOVE. A callee whose parameter this rule
            // refused is still a closure the PLAIN lift takes - its uses
            // are all calls of it - so by the time admission asks, the
            // call is a ctjs.call_direct and the ctjs.call is gone. The
            // measured shape: all four negative programs reached here.
            if (const std::string why = argumentReason(object); !why.empty()) { return why; }
            return "an object literal passed to a direct call as an argument - only a "
                   "receiver and a parameter the lift proved read-only are carried";
        }
        return ("an object literal that escapes - it reaches `" + user->getName().getStringRef() +
                "`")
            .str();
    }
    return "an object literal whose shape is not closed";
}

// WHY A LIFTED METHOD'S `this` IS NO LONGER CLOSED: the first use of
// `%arg0` that is not a constant-key access or a lifted method call, named
// by what it is. In practice there is one route - a `this.other()` whose
// callee was refused, so the call stayed a `ctjs.call` - and the sentence
// has to say that rather than repeat the whole rule.
std::string admission::whyOpenReceiver(mlir::Value self) {
    for (mlir::OpOperand & use : self.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(user) &&
            use.getOperandNumber() == 0) {
            continue;
        }
        if (llvm::isa<ctjs::CallDirectOp>(user) && use.getOperandNumber() == 0 &&
            user->hasAttr("ctnative.receiver")) {
            continue;
        }
        if (llvm::isa<ctjs::CallDirectOp>(user) && isObjectArg(user, use.getOperandNumber())) {
            continue;
        }
        if (llvm::isa<ctjs::CallOp>(user)) {
            return "it calls another method on itself that this tier did not lift";
        }
        return ("it reaches `" + user->getName().getStringRef() + "`").str();
    }
    return "no use of it opens the shape, so the lift and this check disagree";
}

} // namespace ctcompile::ctnative::lowering_detail
