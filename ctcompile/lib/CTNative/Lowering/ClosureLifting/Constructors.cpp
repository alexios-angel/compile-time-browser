//===- ClosureLifting/Constructors.cpp - The constructor lift ---*- C++ -*-===//
//
// `new X(...)` of a local closure X becomes an empty frame-scope
// ctjs.create_object plus a direct call of X with that object as its
// receiver. This file holds the census of constructed closures and the
// checks that refuse a constructor this rewrite cannot model.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"
#include "ctcompile/CTNative/Analysis/Instances.h"

#include <ctbrowser/core/number_format.hpp>

#include <cmath>

namespace ctcompile::ctnative::lowering_detail {

/// Records every closure that appears as the callee of a `new`, whatever else
/// it is used for, and indexes each `new` site by the function it constructs.
/// A closure that is also called or passed elsewhere is refused later by
/// whyConstructorSetupDoesNotLift, which can name the actual reason (for
/// example a `prototype` write) instead of a generic escape.
void closureLifter::constructorCensus() {
    llvm::DenseSet<mlir::Operation *> constructed;
    for (ctjs::ConstructOp made : allConstructs) {
        auto closure = constructedClosure(made);
        if (!closure) { continue; }
        constructed.insert(closure.getOperation());
        if (ctjs::FuncOp target = targetOf(closure)) {
            constructsOfTarget[target.getOperation()].push_back(made);
        }
    }
    // Include closures that are also used in other ways, so the constructor
    // rule (not the generic one) reports why they cannot be lifted.
    constructorClosures.insert(constructed.begin(), constructed.end());
}

/// Is this value provably not `is_object_like()`?
///
/// This is the question `new` asks of a constructor's return value.
/// `context::construct` ends `return produced.is_object_like() ? produced :
/// self` (ctbrowser/lib/Script/vm/call/construct.cpp, and the same for a
/// native constructor), so a constructor that returns an object replaces the
/// instance and `new X()` is not the struct this rewrite built.
/// `is_object_like()` is `is_object() || is_array() || is_callable() ||
/// is_kind(proxy)` (ctbrowser/include/ctbrowser/script/value/base.hpp); a
/// string is not one, so `return "done"` still evaluates to the instance.
///
/// The answer is "no" unless the defining operation says otherwise, which
/// keeps this sound: a block argument, a call result, a property read and
/// anything this list does not name are all assumed to be able to carry an
/// object.
bool closureLifter::isNotObjectLike(mlir::Value v) {
    mlir::Operation * definition = v.getDefiningOp();
    if (definition == nullptr) { return false; } // A block argument: unknown.
    // ctjs.constant carries undefined, null, a string, a number or a
    // boolean and nothing else; the arithmetic, predicate and conversion
    // operations all answer primitives.
    return llvm::isa<ctjs::ConstantOp, ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp,
                     ctjs::CompareOp, ctjs::TruthyOp, ctjs::FromBoolOp, ctjs::InstanceOfOp,
                     ctjs::HasPropertyOp, ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(definition);
}

/// Refuses a constructor whose `return` may produce an object, because `new`
/// then evaluates to that object instead of the instance this lift builds.
///
/// Today the object-literal escape rule also refuses these bodies ("an object
/// literal that escapes - it is returned"), so this check never decides the
/// outcome alone. It runs first because its message names what `new` does,
/// and it becomes the only safeguard if returned object literals are ever
/// allowed to escape.
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

/// The constructor function a marked `new` names: a `new this` the static
/// rewrite proved (kNativeClassConstruct, on the construct), or a `new` of a
/// run-once static cell holding the class (kNativeStaticClass, on its read).
static mlir::StringAttr markedConstructor(ctjs::ConstructOp made) {
    if (auto name = made->getAttrOfType<mlir::StringAttr>(kNativeClassConstruct)) { return name; }
    mlir::Operation * read = made.getCallee().getDefiningOp();
    return read ? read->getAttrOfType<mlir::StringAttr>(kNativeStaticClass) : mlir::StringAttr{};
}

ctjs::CreateClosureOp closureLifter::constructedClosure(ctjs::ConstructOp made) {
    if (mlir::StringAttr name = markedConstructor(made)) {
        auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(made, name);
        return target ? uniqueClosureByTarget.lookup(target.getOperation())
                      : ctjs::CreateClosureOp{};
    }
    return made.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
}

bool closureLifter::isClassConstruct(ctjs::ConstructOp made) {
    return static_cast<bool>(markedConstructor(made));
}

bool closureLifter::constructsClosure(ctjs::ConstructOp made, ctjs::CreateClosureOp c) {
    if (made.getCallee() == c.getResult()) { return true; }
    mlir::StringAttr name = markedConstructor(made);
    ctjs::FuncOp target = targetOf(c);
    return name && target && name.getValue() == target.getSymName();
}

bool closureLifter::isSelfConstruct(mlir::OpOperand & use, ctjs::CreateClosureOp c) {
    auto made = llvm::dyn_cast<ctjs::ConstructOp>(use.getOwner());
    return made && use.getOperandNumber() < 2 && made.getCallee() == c.getResult() &&
           made.getNewTarget() == c.getResult();
}

/// Proves that `c`'s prototype is a single object literal, assigned as
/// `C.prototype = {...}` in the constructor closure's own block before every
/// `new`, whose fields are constants or closures set once in that block.
/// Recomputed from live uses. Primitive defaults and immutable methods are
/// safe only when neither the prototype nor property ownership is observable.
// ponytail: only one `C.prototype = {...}` in the constructor's own block is
// supported; prototype chains with inheritance or later mutation are refused.
std::optional<closureLifter::prototypeFields> closureLifter::immutablePrototype(
    ctjs::CreateClosureOp c) {
    prototypeFields proof;
    // The last write of a static variable holding the class, which every
    // read through it follows (markStaticClassReads).
    mlir::Operation * staticWrite = nullptr;
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        // A proved class's setup lowers to nothing (Classes.cpp).
        if (use.getOwner()->hasAttr(kNativeClassSetup)) { continue; }
        if (llvm::isa<ctjs::CellSetOp>(use.getOwner()) &&
            use.getOwner()->hasAttr(kNativeStaticClass)) {
            if (use.getOwner()->getBlock() != c->getBlock()) { return std::nullopt; }
            if (!staticWrite || staticWrite->isBeforeInBlock(use.getOwner())) {
                staticWrite = use.getOwner();
            }
            continue;
        }
        if (llvm::isa<ctjs::ConstructOp>(use.getOwner())) {
            if (!isSelfConstruct(use, c)) { return std::nullopt; }
            continue;
        }
        auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
        if (!set || use.getOperandNumber() != 0 || ctjs::constantKey(set.getKey()) != "prototype" ||
            proof.attachment || set->getBlock() != c->getBlock()) {
            return std::nullopt;
        }
        proof.attachment = set;
    }
    if (!proof.attachment) { return std::nullopt; }
    auto literal = proof.attachment.getValue().getDefiningOp<ctjs::CreateObjectOp>();
    if (!literal || literal->getBlock() != c->getBlock()) { return std::nullopt; }
    llvm::StringSet<> keys;
    for (mlir::OpOperand & use : literal.getResult().getUses()) {
        if (use.getOwner() == proof.attachment && use.getOperandNumber() == 2) { continue; }
        if (use.getOwner()->hasAttr(kNativeClassSetup)) { continue; }
        auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
        if (!set || use.getOperandNumber() != 0 || set->getBlock() != c->getBlock() ||
            !llvm::isa_and_nonnull<ctjs::ConstantOp, ctjs::CreateClosureOp>(
                set.getValue().getDefiningOp())) {
            return std::nullopt;
        }
        const auto key = ctjs::constantKey(set.getKey());
        if (key.empty() || key == "__proto__" || !keys.insert(key).second) { return std::nullopt; }
        proof.fields.push_back(set);
    }
    const auto sites = constructsOfTarget.find(targetOf(c));
    if (sites == constructsOfTarget.end() || sites->second.empty()) { return std::nullopt; }
    for (ctjs::ConstructOp made : sites->second) {
        // A marked `new this` runs in a static member, which only a call
        // through the defined class reaches (ClassStatics.cpp's rule 3).
        if (made->hasAttr(kNativeClassConstruct) && constructsClosure(made, c)) { continue; }
        // A `new` through the static variable runs after its last write, so
        // the prototype must be complete by then.
        if (isClassConstruct(made) && constructsClosure(made, c)) {
            if (!staticWrite || !proof.attachment->isBeforeInBlock(staticWrite) ||
                !llvm::all_of(proof.fields, [&](ctjs::SetPropertyOp field) {
                    return field->isBeforeInBlock(staticWrite);
                })) {
                return std::nullopt;
            }
            continue;
        }
        if (made.getCallee() != c.getResult() || made->getBlock() != c->getBlock() ||
            !proof.attachment->isBeforeInBlock(made) ||
            !llvm::all_of(proof.fields, [&](ctjs::SetPropertyOp field) {
                return field->isBeforeInBlock(made);
            })) {
            return std::nullopt;
        }
    }
    return proof;
}

/// Refuses a constructor whose `prototype` is read or written, unless
/// immutablePrototype() proves it is a single same-block object literal that
/// is never changed.
std::optional<std::string> closureLifter::whyPrototypeIsTouched(ctjs::CreateClosureOp c) {
    if (immutablePrototype(c)) { return std::nullopt; }
    for (mlir::Operation * user : c.getResult().getUsers()) {
        llvm::StringRef key;
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
            key = ctjs::constantKey(get.getKey());
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            key = ctjs::constantKey(set.getKey());
        }
        if (key == "prototype") {
            return "its constructor's `prototype` is read or written, which is a prototype "
                   "chain this slice does not build - Stage 60A's immutability proof owns it";
        }
    }
    return std::nullopt;
}

/// The constructor form of whyNotLiftable, one message per check. Every check
/// it shares with the method lift is called rather than duplicated: the
/// instance is a literal and the constructor is a receiver carrier, so the
/// two rules are the same rule reached through `new`.
std::optional<std::string> closureLifter::whyNotLiftableConstructor(ctjs::CreateClosureOp c) {
    if (auto why = whyConstructorSetupDoesNotLift(c)) { return why; }
    if (auto prototype = immutablePrototype(c)) {
        // Closed instance uses cannot store, return or merge the receiver;
        // only method/borrowed parameters can alias it. ponytail: a complete
        // per-receiver proof could narrow this further; keep all formals
        // conservative. Prototype observations retain the whole-module
        // census, including aliases derived from those observations.
        bool prototypeObserved = false;
        module.walk([&](ctjs::GetPropertyOp read) {
            const auto key = ctjs::constantKey(read.getKey());
            prototypeObserved |= key == "__proto__" || key == "constructor" || key == "prototype";
        });
        const auto constructSites = constructsOfTarget.lookup(targetOf(c));
        for (ctjs::SetPropertyOp field : prototype->fields) {
            auto method = field.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            if (!method) { continue; }
            if (!methodClosures.contains(method)) {
                return "its prototype method is not an immutable direct-call binding";
            }
            bool calledOnly = true;
            module.walk([&](ctjs::GetPropertyOp read) {
                const auto object = read.getObject();
                if (!prototypeObserved && !llvm::isa<mlir::BlockArgument>(object) &&
                    object != prototype->attachment.getValue() &&
                    llvm::none_of(constructSites, [&](ctjs::ConstructOp made) {
                        return made.getResult() == object ||
                               llvm::is_contained(behind.lookup(object), made.getResult());
                    })) {
                    return;
                }
                const auto key = ctjs::constantKey(field.getKey());
                if (auto constant = read.getKey().getDefiningOp<ctjs::ConstantOp>()) {
                    if (auto text = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
                        text && text.getValue() != key) {
                        return;
                    }
                    // ponytail: keep large/nonfinite keys conservative until
                    // the shared formatter guards its int64 fast-path cast.
                    if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue());
                        number && std::fabs(number.getDouble()) < 1e15 &&
                        ctbrowser::number_to_string(number.getDouble()) != key) {
                        return;
                    }
                }
                auto call = read.getResult().hasOneUse()
                                ? llvm::dyn_cast<ctjs::CallOp>(*read.getResult().getUsers().begin())
                                : ctjs::CallOp{};
                if (!call || call.getCallee() != read.getResult() ||
                    call.getReceiver() != read.getObject()) {
                    calledOnly = false;
                }
            });
            if (!calledOnly) { return "its prototype method is also observed as a value"; }
            if (!lifted.contains(method)) {
                if (auto why = whyNotLiftableMethod(method)) { return why; }
            }
        }
    }
    // A registry Map's class passes `this` to the Map, which the registry
    // proof (Analysis/Instances.h) checked instead.
    if (isIdentityClass(targetOf(c))) { return std::nullopt; }
    return whyThisLeaks(targetOf(c));
}

/// The constructor checks that do not depend on the prototype method census.
/// Receiver resolution needs that census, so these run before seeding it;
/// full admission (whyNotLiftableConstructor) still checks both.
std::optional<std::string> closureLifter::whyConstructorSetupDoesNotLift(ctjs::CreateClosureOp c) {
    // The arrow check first, as in the method form: an arrow's `this` is
    // lexical, so every use of it looks like a legal constant-key access to
    // the receiver check below, and admitting one would give a wrong answer
    // rather than a refusal. (An arrow is not a constructor in the VM either:
    // `ensure_prototype` returns undefined for one.) captureLexicalThis leaves
    // an arrow's `$enclosing_this` undefined and marks the function instead.
    if (const std::optional<std::string> why = whyTargetIsNotLiftable(c)) { return why; }
    ctjs::FuncOp target = targetOf(c);
    if (!ctjs::isUndefinedConstant(c.getEnclosingThis()) || target->hasAttr(kNativeArrow)) {
        return "it is an arrow function, which cannot be constructed even without lexical this "
               "reads";
    }
    mlir::Block & entry = target.getBody().front();
    const unsigned parameters = entry.getNumArguments() - ctjs::implicit_arguments;
    // Check `prototype` access before the generic use checks, so the
    // diagnostic names the prototype rather than whatever else the read
    // happens to be.
    if (const std::optional<std::string> why = whyPrototypeIsTouched(c)) { return why; }
    // Aside from the proved prototype attachment, every operand use must be
    // its own callee/new.target pair. Passing the closure as an argument to
    // another constructor is still an escape.
    const auto prototype = immutablePrototype(c);
    for (mlir::OpOperand & use : c.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        if (prototype && user == prototype->attachment) { continue; }
        if (user->hasAttr(kNativeClassSetup)) { continue; }
        // A write of the static variable whose every read is a marked `new`
        // (markStaticClassReads); the lift erases it with those reads.
        if (llvm::isa<ctjs::CellSetOp>(user) && user->hasAttr(kNativeStaticClass)) { continue; }
        if (!isSelfConstruct(use, c)) {
            return ("it is used as a constructor and also reaches `" +
                    user->getName().getStringRef() +
                    "` - one ctjs.func is one C++ signature, and a body that is a free "
                    "function at one site and a constructor at another needs two")
                .str();
        }
    }
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) {
        return "its constructor uses new.target, whose function identity this lift does not carry";
    }
    // The constructor must not return an object (see
    // whyConstructorReturnsAnObject).
    if (const std::optional<std::string> why = whyConstructorReturnsAnObject(target)) {
        return why;
    }
    const auto sites = constructsOfTarget.find(target.getOperation());
    if (sites == constructsOfTarget.end() || sites->second.empty()) {
        return "nothing constructs it";
    }
    for (ctjs::ConstructOp made : sites->second) {
        // Every `new` of this function must name this closure directly: the
        // target becomes one C++ function, so a `new` through some other
        // callee would be left dispatching through a closure that is about to
        // lower to nothing.
        if (made.getCallee().getDefiningOp<ctjs::CreateClosureOp>() != c &&
            !(isClassConstruct(made) && constructsClosure(made, c))) {
            return "it is constructed at a site whose callee this pass cannot name";
        }
        // A marked `new` (a static member's `new this`, or a `new` through the
        // class's run-once static binding) builds the instance outside the
        // frame that made the constructor's closure, so nothing from that
        // frame can reach it: no capture and no prototype field the rewrite
        // copies onto the instance (only methods, which become direct calls).
        if (isClassConstruct(made)) {
            const std::string site =
                std::string(made->hasAttr(kNativeClassConstruct)
                                ? "it is constructed by `new this` in `"
                                : "it is constructed through its static binding in `") +
                made->getParentOfType<ctjs::FuncOp>().getSymName().str() +
                "`, outside the frame that defines it, ";
            if (!c.getUpvalues().empty()) {
                return site + "and its constructor captures " +
                       std::to_string(c.getUpvalues().size()) + " value(s) from that frame";
            }
            if (prototype) {
                for (ctjs::SetPropertyOp field : prototype->fields) {
                    if (!field.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
                        return site + "and its prototype field `" +
                               ctjs::constantKey(field.getKey()).str() +
                               "` is a value from that frame";
                    }
                }
            }
        }
        // The instance must have a closed shape under the same rule an object
        // literal uses, because the rewrite turns it into one. That rule
        // accepts a return when every caller keeps the instance as its only
        // reference (returnsAnInstance).
        // A `new this` in a static member may also return the instance: the
        // record rule (Analysis/Inference/Records.cpp) then returns it by
        // value as its class after the lift, and admission refuses the
        // function where that rule does not hold.
        const bool returned =
            isClassConstruct(made) &&
            llvm::all_of(made.getResult().getUses(), [&](mlir::OpOperand & use) {
                return llvm::isa<ctjs::ReturnOp>(use.getOwner()) || isConstantKeyAccess(use);
            });
        if (!returned && !usesCloseTheShape(made.getResult())) {
            return "the instance `new` makes does not have a closed shape - " +
                   whyUsesOpenTheShape(made.getResult());
        }
        // Reading a key the constructor never writes is allowed: admission
        // already refuses the names Object.prototype answers (such as
        // `constructor`), and any other unwritten key is undefined in the VM
        // too, as for a literal.
        if (auto why = whyCallSiteDoesNotLift(made, made.getArgs().size(), parameters, "a `new`")) {
            return why;
        }
    }
    if (const std::optional<std::string> why = whyCapturesDoNotLift(c)) { return why; }
    // The captured values must also reach every `new` site; the check above
    // established that those are the only users of the closure value.
    return whyLiftTailFails(c, target, c.getResult().getUsers());
}

} // namespace ctcompile::ctnative::lowering_detail
