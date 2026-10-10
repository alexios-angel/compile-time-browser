//===- References.cpp - Callable references ---------------------*- C++ -*-===//
//
// Decides whether a lifted callable function or its binder is still referred
// to, by symbol or by name inside opaque C++ text, and so must keep its own
// definition.
//
//===----------------------------------------------------------------------===//

#include "Body.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::cpp {
namespace {

/// True when `name` occurs in `text` as a whole identifier, not as part of a
/// longer one.
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

/// True when opaque C++ anywhere in `module` mentions `name`: verbatim text,
/// opaque callees, literals, opaque attributes and types. A creation call of
/// `creationTarget` does not count as a mention.
bool textReferences(mlir::ModuleOp module, llvm::StringRef name,
                    mlir::emitc::FuncOp creationTarget = {}) {
    bool observed = false;
    const auto inspectType = [&](mlir::Type type) {
        type.walk([&](mlir::Type nested) {
            if (auto opaque = llvm::dyn_cast<mlir::emitc::OpaqueType>(nested)) {
                observed |= containsIdentifier(opaque.getValue(), name);
            }
        });
    };
    module.walk([&](mlir::Operation * op) {
        if (auto text = llvm::dyn_cast<mlir::emitc::VerbatimOp>(op)) {
            observed |= containsIdentifier(text.getValue(), name);
        }
        if (auto call = llvm::dyn_cast<mlir::emitc::CallOpaqueOp>(op)) {
            if (!creationTarget || callableCreationSymbol(call) != creationTarget) {
                observed |= containsIdentifier(call.getCallee(), name);
            }
        }
        if (auto literal = llvm::dyn_cast<mlir::emitc::LiteralOp>(op)) {
            observed |= containsIdentifier(literal.getValue(), name);
        }
        // Function pointers and decltype expressions may live in emitted text
        // without an MLIR symbol reference. Keep their original definition.
        for (mlir::NamedAttribute entry : op->getAttrs()) {
            entry.getValue().walk([&](mlir::Attribute attribute) {
                if (auto opaque = llvm::dyn_cast<mlir::emitc::OpaqueAttr>(attribute)) {
                    observed |= containsIdentifier(opaque.getValue(), name);
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

bool callableFunctionReferenced(mlir::emitc::FuncOp function) {
    auto module = function->getParentOfType<mlir::ModuleOp>();
    if (!module) { return true; }
    const auto uses = mlir::SymbolTable::getSymbolUses(function, module);
    if (!uses) { return true; }
    for (const auto & use : *uses) {
        if (llvm::isa<mlir::emitc::DeclareFuncOp>(use.getUser())) { continue; }
        if (auto call = llvm::dyn_cast<mlir::emitc::CallOpaqueOp>(use.getUser())) {
            if (call->getAttr(ctnative::kNativeCallableCreate) == use.getSymbolRef()) { continue; }
        }
        return true;
    }
    return textReferences(module, function.getSymName());
}

bool callableBinderNeeded(mlir::emitc::FuncOp function, llvm::StringRef binder) {
    auto module = function->getParentOfType<mlir::ModuleOp>();
    if (!module) { return true; }
    bool creation = false;
    module.walk([&](mlir::emitc::CallOpaqueOp call) {
        creation |= callableCreationSymbol(call) == function;
    });
    // With no marked creation, the binder may still be called by unmarked
    // EmitC code, so keep it. Opaque address or template references also keep
    // it, just as they keep lifted functions.
    return !creation || textReferences(module, binder, function);
}

} // namespace ctcompile::cpp
