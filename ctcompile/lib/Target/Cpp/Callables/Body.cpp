#include "Body.h"
#include "../Const/Bindings.h"
#include "../Names/SourceNames.h"

#include "llvm/ADT/StringSet.h"

namespace ctcompile::cpp {
namespace {

bool identifier(llvm::StringRef value) {
    return !value.empty() && localIdentifier(value) == value;
}

} // namespace

mlir::FailureOr<CallableBody> callableBodyPlan(mlir::emitc::FuncOp function) {
    CallableBody result;
    if (!function->hasAttr("ctnative.callable_body")) { return result; }
    const auto invalid = [&]() -> mlir::FailureOr<CallableBody> {
        function.emitOpError("invalid native callable body description");
        return mlir::failure();
    };
    auto description = function->getAttrOfType<mlir::DictionaryAttr>("ctnative.callable_body");
    if (!description || function.isExternal() || function.getFunctionType().getNumResults() != 1) {
        return invalid();
    }
    auto type = description.getAs<mlir::StringAttr>("type");
    auto binder = description.getAs<mlir::StringAttr>("binder");
    auto name = description.getAs<mlir::StringAttr>("name");
    auto captures = description.getAs<mlir::ArrayAttr>("captures");
    auto parameters = description.getAs<mlir::ArrayAttr>("parameters");
    if (!type || !binder || !name || !captures || !parameters || description.size() != 5 ||
        captures.size() + parameters.size() != function.getNumArguments() ||
        !type.getValue().starts_with("ctnative::ctn_env_") ||
        !identifier(type.getValue().drop_front(10)) ||
        !binder.getValue().starts_with("ctn_bind_") || !identifier(binder.getValue()) ||
        !name.getValue().starts_with("ctn_") || !identifier(name.getValue())) {
        return invalid();
    }
    result.type = type.str();
    result.binder = binder.str();
    result.name = name.str();
    result.captures = static_cast<unsigned>(captures.size());
    llvm::StringSet<> occupied;
    occupied.insert(result.name);
    for (auto group : {captures, parameters}) {
        for (mlir::Attribute attribute : group) {
            auto value = llvm::dyn_cast<mlir::StringAttr>(attribute);
            if (!value || !identifier(value.getValue()) ||
                !occupied.insert(value.getValue()).second) {
                return invalid();
            }
            result.parameters.push_back(value.str());
        }
    }
    ConstBindings immutable;
    immutable.prepare(function);
    result.inlineBody = true;
    for (auto capture : function.getArguments().take_front(result.captures)) {
        result.inlineBody &= immutable.qualifies(capture);
    }
    result.retainFunction = !result.inlineBody || callableFunctionReferenced(function);
    result.inlineCreation = result.inlineBody && boundedCallableExpansion(function);
    result.retainBinder = !result.inlineCreation || callableBinderNeeded(function, result.binder);
    return result;
}

} // namespace ctcompile::cpp
