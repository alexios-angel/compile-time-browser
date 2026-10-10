//===- ClosureProof.cpp - Closure heap facts for partial evaluation -------===//
//
// Implements prepareClosureHeapFacts (see ClosureProof.h). Each operation of
// the private copy gets a NameLoc naming its original, so facts proved on the
// copy can be mapped back after lifting rewrites it.
//
//===----------------------------------------------------------------------===//

#include "ClosureProof.h"

#include "../Lowering/ClosureLifting/ClosureLifter.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative::partial_eval {
namespace {

/// Whether the closure lifter's proof is worth attempting: function indices
/// are unique, every function has its implicit arguments, every closure is
/// created exactly once with immutable captures, and at least one closure
/// captures something.
bool canInspect(mlir::ModuleOp module) {
    const auto indexed = uniqueFunctionIndex(module);
    const auto functions = indexed.value_or(llvm::DenseMap<unsigned, ctjs::FuncOp>{});
    llvm::DenseMap<unsigned, unsigned> creations;
    bool valid = indexed.has_value(), captured = false;
    module.walk([&](ctjs::FuncOp function) {
        if (function.getBody().empty() ||
            function.getBody().front().getNumArguments() < ctjs::implicit_arguments) {
            valid = false;
        }
    });
    module.walk([&](ctjs::CreateClosureOp made) {
        if (made.getFunction() < 0) {
            valid = false;
            return;
        }
        const auto index = static_cast<unsigned>(made.getFunction());
        auto target = functions.lookup(index);
        if (!target || ++creations[index] != 1 || target.getBody().empty() ||
            static_cast<size_t>(target.getUpvalueCount()) != made.getUpvalues().size()) {
            valid = false;
            return;
        }
        if (made.getUpvalues().empty()) { return; }
        captured = true;
        if (!immutableClosureTarget(made, module)) { valid = false; }
        for (mlir::Value input : made.getUpvalues()) {
            auto cell = input.getDefiningOp<ctjs::CreateCellOp>();
            if (!cell || !immutableCaptureCell(cell, module)) { valid = false; }
        }
    });
    // A store to an upvalue needs an inherited environment or a mutable shared
    // cell, which this private proof attempt does not model.
    module.walk([&](ctjs::StoreUpvalueOp) { valid = false; });
    return valid && captured;
}

} // namespace

std::optional<llvm::DenseMap<unsigned, ctjs::FuncOp>> uniqueFunctionIndex(mlir::ModuleOp module) {
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    bool unique = true;
    module.walk([&](ctjs::FuncOp function) {
        if (auto index = functionIndex(function)) {
            unique &= functions.try_emplace(*index, function).second;
        }
    });
    if (!unique) { return std::nullopt; }
    return functions;
}

closureHeapProof prepareClosureHeapFacts(
    mlir::ModuleOp module, llvm::function_ref<bool(mlir::ModuleOp)> checkEnvironment) {
    prepareNativeMaps(module);
    closureHeapProof result;
    if (!canInspect(module)) { return result; }
    mlir::IRMapping mapping;
    mlir::OwningOpRef<mlir::ModuleOp> copy(llvm::cast<mlir::ModuleOp>(module->clone(mapping)));
    llvm::DenseMap<mlir::StringAttr, mlir::Operation *> origins;
    unsigned ordinal = 0;
    module.walk([&](mlir::Operation * original) {
        auto * cloned =
            original == module.getOperation() ? copy->getOperation() : mapping.lookup(original);
        auto name = mlir::StringAttr::get(module.getContext(),
                                          "partial-evaluation-origin-" + std::to_string(ordinal++));
        origins[name] = original;
        cloned->setLoc(mlir::NameLoc::get(name, original->getLoc()));
        removeAttrsWithPrefix(cloned, "ctnative.");
    });
    lowering_detail::closureLifter lifter{*copy, false};
    (void)lifter.run();
    prepareNativeMaps(*copy);
    result.checkedEnvironment = checkEnvironment(*copy);
    copy->walk([&](mlir::Operation * operation) {
        auto location = llvm::dyn_cast<mlir::NameLoc>(operation->getLoc());
        auto * original = location ? origins.lookup(location.getName()) : nullptr;
        if (!original) { return; }
        if (llvm::isa<ctjs::CallDirectOp>(operation) && llvm::isa<ctjs::CallOp>(original)) {
            auto call = llvm::cast<ctjs::CallDirectOp>(operation);
            auto target = call.getTarget();
            if (target && !target.getBody().empty()) { result.closedCalls.insert(original); }
        }
        if (operation->getName() != original->getName()) { return; }
        for (llvm::StringRef name :
             {kNativeMapConstructor, kNativeMapSite, kNativeMapMethod, kNativeMapAction}) {
            if (auto attribute = operation->getAttr(name)) { original->setAttr(name, attribute); }
        }
    });
    return result;
}

} // namespace ctcompile::ctnative::partial_eval
