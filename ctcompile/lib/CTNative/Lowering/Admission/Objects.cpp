//===- Admission/Objects.cpp - Object literal and owned global admission --===//
//
// Admission of owned globals and closed-shape object literals, and the
// refusal messages that name why an object literal or a lifted receiver is
// not closed.
//
//===----------------------------------------------------------------------===//

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
    const auto field = typeOf(write.getValue());
    auto table = llvm::dyn_cast_or_null<MethodTableType>(field);
    const bool ownedTable =
        root->methodTable && table && table.getSite() == methodTableName(root->methodTable->table);
    const auto closure = llvm::dyn_cast_or_null<ClosureType>(field);
    auto callbackFunction = root->scalarCallback;
    const bool callback =
        callbackFunction && closure && closure.getTarget() == callbackFunction.getSymName();
    if (!ownedGlobalValue(made.getResult()) ||
        !(root->methodTable ? ownedTable : (callback || carrierOf(field) == carrier::number))) {
        return refuse(
            root->methodTable
                ? "owned global method field needs its proved owning table schema"
                : "owned global root needs a proved owner and one definite numeric field");
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

/// An object with a closed shape lowers to a struct held by value.
/// TypeInference::hasClosedShape is the proof: every use is a get or set
/// through a constant key, so the object never reaches anything that could add
/// or remove a field, and never leaves the frame (a return, a store or a call
/// would all be uses that open it). Each key must be a C identifier, each
/// field a number or a boolean.
bool admission::isClosedObject(mlir::Value v) {
    return TypeInference::hasClosedShape(v);
}

/// The reason the object-parameter rule recorded on `object`'s literal
/// (ctnative.object_reason) when it refused the literal as a call argument, or
/// empty. closureLifter::argumentCensus writes it before any rewrite, because
/// the condition that failed belongs to the callee and the escape walk only
/// sees the use.
std::string admission::argumentReason(mlir::Value object) {
    mlir::Operation * literal = object.getDefiningOp();
    auto why = literal ? literal->getAttrOfType<mlir::StringAttr>(kNativeObjectReason) : nullptr;
    return why ? "an object literal that escapes - " + why.getValue().str() : std::string{};
}

/// Describes a use of a literal that stores, loop-carries or returns it, or
/// returns nullopt for any other use. A loop-carried literal is a real phi:
/// the variable is assigned again inside the loop, or on only one path before
/// it. Two literals have two shapes and two stack slots, and merging them into
/// one value would need a pointer. (--ctjs-lift-to-scf already removes the
/// trivial phi of a variable assigned once before the loop.)
std::optional<std::string> admission::escapeRoute(llvm::StringRef literal, mlir::OpOperand & use) {
    mlir::Operation * user = use.getOwner();
    if (llvm::isa<ctjs::SetPropertyOp>(user) && use.getOperandNumber() == 2) {
        return (literal + " that escapes - it is stored into another object").str();
    }
    if (llvm::isa<mlir::scf::WhileOp, mlir::scf::YieldOp, mlir::scf::ConditionOp>(user)) {
        return (literal + " that is loop-carried - more than one value reaches the variable "
                          "that holds it (assigned again inside a loop, or on only one path "
                          "before it)")
            .str();
    }
    if (llvm::isa<ctjs::ReturnOp>(user)) {
        return (literal + " that escapes - it is returned").str();
    }
    return std::nullopt;
}

std::string admission::escapesTo(llvm::StringRef literal, mlir::Operation * user) {
    return (literal + " that escapes - it reaches `" + user->getName().getStringRef() + "`").str();
}

/// Describes, for a refusal message, the first use of an object literal that
/// is not a get or set through a constant key on the literal itself. Naming
/// the use this program actually has is more useful than listing every
/// possible route.
std::string admission::whyOpen(mlir::Value object) {
    for (mlir::OpOperand & use : object.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(user) &&
            use.getOperandNumber() == 0 && ctjs::constantKey(user->getOperand(1)) == "__proto__") {
            return "an object literal reached through the inherited Object.prototype.__proto__ "
                   "accessor";
        }
        if (isConstantKeyAccess(use)) { continue; }
        if (ctjs::propertyKey(user) && use.getOperandNumber() == 0) {
            return "an object literal reached through a dynamic key";
        }
        if (auto why = escapeRoute("an object literal", use)) { return *why; }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            // A method call the receiver lift did not take. It is still a
            // ctjs.call, so the object is passed to a dispatcher native
            // lowering cannot see through; the reason the lift gave is on
            // the closure the field holds, and this message points at it.
            if (use.getOperandNumber() == 1) {
                return "an object literal whose method call was not lifted - it is still "
                       "dispatched through the callee value, so the object is passed to "
                       "something that could add or remove a field";
            }
            // An argument the object-parameter rule refused: report the
            // rule's own reason, which `argumentCensus` wrote onto the
            // literal because the condition that failed belongs to the
            // callee and this walk only sees the use.
            if (const std::string why = argumentReason(object); !why.empty()) { return why; }
            return "an object literal that escapes - it is passed to a call";
        }
        if (llvm::isa<ctjs::CallDirectOp>(user)) {
            // A direct call carries a receiver and any parameter the lift
            // listed; anything else in an operand is still an escape.
            if (use.getOperandNumber() == 0 && user->hasAttr(kNativeReceiver)) { continue; }
            if (isObjectArg(user, use.getOperandNumber())) { continue; }
            // The argument reason usually surfaces here rather than in the
            // ctjs.call arm above: a callee whose parameter the rule refused
            // is still a closure the plain lift takes (its uses are all
            // calls of it), so by the time admission runs the call is a
            // ctjs.call_direct.
            if (const std::string why = argumentReason(object); !why.empty()) { return why; }
            return "an object literal passed to a direct call as an argument - only a "
                   "receiver and a parameter the lift proved read-only are carried";
        }
        return escapesTo("an object literal", user);
    }
    return "an object literal whose shape is not closed";
}

/// Describes why a lifted method's `this` is no longer closed: the first use
/// of `%arg0` that is not a constant-key access or a lifted method call. In
/// practice there is one route, a `this.other()` whose callee was refused so
/// the call stayed a `ctjs.call`, and the message says that rather than
/// repeating the whole rule.
std::string admission::whyOpenReceiver(mlir::Value self) {
    for (mlir::OpOperand & use : self.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(user) &&
            use.getOperandNumber() == 0) {
            if (ctjs::constantKey(user->getOperand(1)) == "__proto__") {
                return "it reaches the inherited Object.prototype.__proto__ accessor";
            }
            continue;
        }
        if (llvm::isa<ctjs::CallDirectOp>(user) && use.getOperandNumber() == 0 &&
            user->hasAttr(kNativeReceiver)) {
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
