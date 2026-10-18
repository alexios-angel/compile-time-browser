//===- Instances.cpp - Instances a registry Map owns ----------------------===//
//
// Lowers the instances of a class a registry Map owns (Analysis/Instances.h):
// each is a `std::shared_ptr<C>`, where C is the class's struct
// (EmitC/Shapes.cpp names it after the JavaScript class). The constructor
// lift (ClosureLifting/Constructors.cpp) has already turned `new C(args)`
// into an empty literal and a receiver call of the constructor, so
//
//     auto self = std::make_shared<Widget>();
//     Widget_constructor(self, key, size);
//
// is that literal made by `std::make_shared` and that call, whose receiver
// parameter is a `const std::shared_ptr<Widget> &`: the constructor passes
// `this` on to the Map, which keeps its own owner. A method receives a
// `Widget *` borrowed from the caller's owner, and a test against null or
// undefined is a test of the pointer.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Analysis/Instances.h"

#include "../Admission/Admission.h"
#include "Emitter.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"

namespace ctcompile::ctnative::lowering_detail {

namespace ec = mlir::emitc;

mlir::Type lowering::instanceType(mlir::Type type) {
    const mlir::Type spelled = instanceCarrierType(type);
    instanceSpellings.insert(opaqueSpelling(spelled));
    return spelled;
}

mlir::Value lowering::ownerOf(mlir::OpBuilder & b, mlir::Location where, mlir::Value borrowed,
                              mlir::Type type) {
    sharedFromThis.insert(instanceClassSpelling(instanceClassName(type)));
    return memberCall(b, where, instanceType(type), borrowed, "shared_from_this");
}

bool lowering::isInstanceValue(mlir::Value v) const {
    return madeInstances.contains(v) || instanceSpellings.contains(opaqueSpelling(v.getType())) ||
           carrierOf(typeOf(v)) == carrier::instance;
}

mlir::Operation * lowering::calleeOf(ctjs::CallDirectOp call) const {
    mlir::Operation * callee =
        mlir::SymbolTable::lookupNearestSymbolFrom(call, call.getCalleeAttr());
    // A callee lowered already is an emptied shell (`shells`) until finish();
    // its emitc.func goes by the name `names` gave it.
    auto shell = llvm::dyn_cast_or_null<ctjs::FuncOp>(callee);
    if (!shell || !shell.getBody().empty()) { return callee; }
    const auto named = names.find(shell.getSymName());
    if (named == names.end()) { return nullptr; }
    return mlir::SymbolTable::lookupNearestSymbolFrom(
        call, mlir::StringAttr::get(call.getContext(), named->second));
}

mlir::Value lowering::receiverOperand(mlir::OpBuilder & b, mlir::Location where,
                                      ctjs::CallDirectOp call) {
    const mlir::Value receiver = call.getReceiver();
    // A borrowed parameter forwards its pointer.
    if (llvm::isa<ec::PointerType>(receiver.getType())) { return receiver; }
    // The callee is still a ctjs.func, or already the emitc.func lowered
    // from it (its symbol then names that), depending on which was visited
    // first.
    mlir::Operation * callee = calleeOf(call);
    mlir::Type parameter;
    if (auto lowered = llvm::dyn_cast_or_null<ec::FuncOp>(callee)) {
        parameter = lowered.getArgumentTypes().front();
    } else if (auto fn = llvm::dyn_cast_or_null<ctjs::FuncOp>(callee)) {
        // The constructor of an identity class takes the owner itself, so
        // that `this` can be handed to the Map.
        if (isIdentityClass(fn)) { return receiver; }
        parameter = receiverType(shapeAt(fn.getBody().front().getArgument(0)));
    }
    if (!llvm::isa_and_nonnull<ec::PointerType>(parameter)) { return receiver; }
    // A method borrows the instance: `owner.get()`. The caller's
    // `std::shared_ptr` is a local or a parameter that lives until the call
    // returns, so the pointer outlives no owner.
    return memberCall(b, where, parameter, receiver, "get");
}

bool lowering::replaceInstance(mlir::Operation * o) {
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    // `new C(...)`'s literal: the instance, made shared at once.
    if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(o);
        object && isInstanceValue(object.getResult())) {
        const auto type = llvm::cast<InstanceType>(typeOf(object.getResult()));
        mlir::Value made =
            callValue(b, where, object.getResult().getType(),
                      "std::make_shared<" + instanceClassSpelling(type.getClassName()) + ">",
                      mlir::ValueRange{});
        madeInstances.insert(made);
        object.getResult().replaceAllUsesWith(made);
        eraseIfUnused(o);
        return true;
    }
    // A test against null or undefined that the registry proof found exact
    // (kNativeInstanceTest): either way, an absent instance is a null pointer.
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(o);
        compare && o->hasAttr(kNativeInstanceTest)) {
        const mlir::Value instance =
            isInstanceValue(compare.getLhs()) ? compare.getLhs() : compare.getRhs();
        mlir::Value null =
            ec::LiteralOp::create(b, where, instance.getType(), b.getStringAttr("nullptr"));
        mlir::Value absent =
            ec::CmpOp::create(b, where, b.getI1Type(), ec::CmpPredicate::eq, instance, null);
        const mlir::Type result = compare.getResult().getType();
        if (!result.isInteger(1)) { absent = convertScalar(b, where, absent, result); }
        compare.getResult().replaceAllUsesWith(absent);
        eraseIfUnused(o);
        return true;
    }
    return false;
}

void lowering::joinInstanceValues(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    // Per class, every value of it in the accepted functions.
    llvm::MapVector<llvm::StringRef, llvm::SetVector<mlir::Value>> byClass;
    const auto add = [&](mlir::Value v) {
        mlir::Type type = typeOf(v);
        if (auto optional = llvm::dyn_cast_or_null<OptType>(type)) {
            type = optional.getElementType();
        }
        if (auto instance = llvm::dyn_cast_or_null<InstanceType>(type)) {
            byClass[instance.getClassName()].insert(v);
        }
    };
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](mlir::Block * block) {
            for (mlir::BlockArgument argument : block->getArguments()) { add(argument); }
            for (mlir::Operation & op : *block) {
                for (mlir::Value result : op.getResults()) { add(result); }
            }
        });
    }
    if (byClass.empty()) { return; }
    if (groups) { instanceGroups = *groups; }
    for (auto & [name, values] : byClass) {
        // The class's existing group (its `new` objects, constructor and
        // methods; TypeInference::joinInstanceReceivers) and every value.
        llvm::SetVector<mlir::Value> members;
        for (mlir::Value v : values) {
            for (mlir::Value alias : aliasesOf(groups, v)) { members.insert(alias); }
        }
        const llvm::SmallVector<mlir::Value, 2> all(members.begin(), members.end());
        for (mlir::Value member : all) { instanceGroups[member] = all; }
    }
    groups = &instanceGroups;
}

bool lowering::borrowsInstance(mlir::Value parameter) const {
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(parameter);
    if (!argument || !argument.getOwner()->isEntryBlock() ||
        argument.getArgNumber() < ctjs::implicit_arguments ||
        !llvm::isa<ctjs::FuncOp>(argument.getOwner()->getParentOp()) ||
        carrierOf(typeOf(parameter)) != carrier::instance) {
        return false;
    }
    // A host callback is called by the host, through the callable it bound
    // to the captures' owners (EmitC/HostCallbacks.cpp): no caller lends it
    // a pointer.
    if (argument.getOwner()->getParentOp()->hasAttr(kNativeHostCallback)) { return false; }
    return llvm::all_of(parameter.getUses(), [](mlir::OpOperand & use) {
        mlir::Operation * user = use.getOwner();
        if (llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(user)) {
            return use.getOperandNumber() == 0;
        }
        if (llvm::isa<ctjs::TruthyOp>(user) || user->hasAttr(kNativeInstanceTest)) { return true; }
        // A method of the class, not its constructor, which keeps `this`.
        auto call = llvm::dyn_cast<ctjs::CallDirectOp>(user);
        return call && call->hasAttr(kNativeReceiver) && use.getOperandNumber() == 0 &&
               call.getTarget() && !isIdentityClass(call.getTarget());
    });
}

mlir::Value lowering::instanceArgument(mlir::OpBuilder & b, mlir::Location where,
                                       ctjs::CallDirectOp call, unsigned index) {
    const mlir::Value owner = call->getOperand(index);
    if (llvm::isa<ec::PointerType>(owner.getType())) { return owner; }
    // As for the receiver: the callee is a ctjs.func still, or its emitc.func.
    mlir::Operation * callee = calleeOf(call);
    mlir::Type parameter;
    if (auto fn = llvm::dyn_cast_or_null<ctjs::FuncOp>(callee)) {
        const mlir::Value declared = fn.getBody().front().getArgument(index);
        if (!borrowsInstance(declared)) { return owner; }
        parameter = ec::PointerType::get(ec::OpaqueType::get(
            context, instanceClassSpelling(instanceClassName(typeOf(declared)))));
    } else if (auto lowered = llvm::dyn_cast_or_null<ec::FuncOp>(callee)) {
        // The implicit operands are dropped; a receiver comes first.
        const unsigned position =
            index - ctjs::implicit_arguments + (call->hasAttr(kNativeReceiver) ? 1U : 0U);
        parameter = lowered.getArgumentTypes()[position];
    }
    if (!llvm::isa_and_nonnull<ec::PointerType>(parameter)) { return owner; }
    return memberCall(b, where, parameter, owner, "get");
}

} // namespace ctcompile::ctnative::lowering_detail
