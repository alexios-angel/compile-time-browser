#include "Body.h"
#include "../Const/Bindings.h"
#include "../Names/SourceNames.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/StringSet.h"

namespace ctcompile::cpp {
namespace {

bool identifier(llvm::StringRef value) {
    return !value.empty() && localIdentifier(value) == value;
}

bool containsIdentifier(llvm::StringRef text, llvm::StringRef name) {
    const auto part = [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
               ch == '_';
    };
    for (std::size_t at = text.find(name); at != llvm::StringRef::npos;
         at = text.find(name, at + name.size())) {
        if ((at == 0 || !part(text[at - 1])) &&
            (at + name.size() == text.size() || !part(text[at + name.size()]))) {
            return true;
        }
    }
    return false;
}

bool observedFunction(mlir::emitc::FuncOp function) {
    auto module = function->getParentOfType<mlir::ModuleOp>();
    if (!module) { return true; }
    const auto uses = mlir::SymbolTable::getSymbolUses(function, module);
    if (!uses) { return true; }
    for (const auto & use : *uses) {
        if (!llvm::isa<mlir::emitc::DeclareFuncOp>(use.getUser())) { return true; }
    }
    bool observed = false;
    const auto inspectType = [&](mlir::Type type) {
        type.walk([&](mlir::Type nested) {
            if (auto opaque = llvm::dyn_cast<mlir::emitc::OpaqueType>(nested)) {
                observed |= containsIdentifier(opaque.getValue(), function.getSymName());
            }
        });
    };
    module.walk([&](mlir::Operation * op) {
        if (auto text = llvm::dyn_cast<mlir::emitc::VerbatimOp>(op)) {
            observed |= containsIdentifier(text.getValue(), function.getSymName());
        }
        if (auto call = llvm::dyn_cast<mlir::emitc::CallOpaqueOp>(op)) {
            observed |= containsIdentifier(call.getCallee(), function.getSymName());
        }
        if (auto literal = llvm::dyn_cast<mlir::emitc::LiteralOp>(op)) {
            observed |= containsIdentifier(literal.getValue(), function.getSymName());
        }
        // Function pointers and decltype expressions may live in emitted text
        // without an MLIR symbol reference. Keep their original definition.
        for (mlir::NamedAttribute entry : op->getAttrs()) {
            entry.getValue().walk([&](mlir::Attribute attribute) {
                if (auto opaque = llvm::dyn_cast<mlir::emitc::OpaqueAttr>(attribute)) {
                    observed |= containsIdentifier(opaque.getValue(), function.getSymName());
                } else if (auto type = llvm::dyn_cast<mlir::TypeAttr>(attribute)) {
                    inspectType(type.getValue());
                }
            });
        }
        for (mlir::Type type : op->getResultTypes()) { inspectType(type); }
        for (mlir::Region & region : op->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument argument : block.getArguments()) {
                    inspectType(argument.getType());
                }
            }
        }
    });
    return observed;
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
    result.retainFunction = !result.inlineBody || observedFunction(function);
    return result;
}

} // namespace ctcompile::cpp
