#include "ClosureProof.h"

#include "../Lowering/ClosureLifting/ClosureLifter.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"

namespace ctcompile::ctnative::partial_eval {
namespace {

bool canInspect(mlir::ModuleOp module) {
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    llvm::DenseMap<unsigned, unsigned> creations;
    bool valid = true, captured = false;
    module.walk([&](ctjs::FuncOp function) {
        if (function.getBody().empty() || function.getBody().front().getNumArguments() < 3) {
            valid = false;
        }
        if (auto index = functionIndex(function);
            index && !functions.try_emplace(*index, function).second) {
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
    // These operations require inherited environments or mutable shared cells,
    // whose native lifter assumptions are outside this private proof attempt.
    module.walk([&](ctjs::StoreUpvalueOp) { valid = false; });
    return valid && captured;
}

} // namespace

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
        llvm::SmallVector<mlir::StringAttr> remove;
        for (mlir::NamedAttribute attribute : cloned->getAttrs()) {
            if (attribute.getName().getValue().starts_with("ctnative.")) {
                remove.push_back(attribute.getName());
            }
        }
        for (mlir::StringAttr name : remove) { cloned->removeAttr(name); }
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
            auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                call, call.getCalleeAttr());
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
