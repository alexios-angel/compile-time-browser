#include "../Lowering/ClosureLifting/ClosureLifter.h"
#include "../Lowering/Exceptions/Recovery.h"
#include "Analysis.h"
#include "Preparation.h"

#include "mlir/IR/IRMapping.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {

llvm::Error liftDOMClasses(mlir::ModuleOp module, unsigned maxSteps) {
    const auto refuse = [](const llvm::Twine & reason) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), reason);
    };
    // ponytail: quadratic input-size ceiling around the existing finite
    // lift; thread a step budget through its censuses before widening it.
    uint64_t operations = 0, size = 0;
    auto counted = module.walk([&](mlir::Operation * op) {
        ++operations;
        size += uint64_t(1) + op->getNumOperands();
        for (mlir::Region & region : op->getRegions()) {
            for (mlir::Block & block : region) { size += block.getNumArguments(); }
        }
        return operations * size > maxSteps ? mlir::WalkResult::interrupt()
                                            : mlir::WalkResult::advance();
    });
    if (counted.wasInterrupted()) { return refuse("DOM class lifting work budget exhausted"); }
    lowering_detail::closureLifter lifter{module};
    lifter.discardNativeSourceFacts();
    lifter.run();
    for (mlir::Operation * operation : lifter.lifted) {
        auto closure = llvm::cast<ctjs::CreateClosureOp>(operation);
        if (llvm::any_of(closure->getUsers(),
                         [](mlir::Operation * user) { return !llvm::isa<ctjs::RootOp>(user); })) {
            return refuse("DOM class lifted closure retains an observable use");
        }
        for (mlir::Operation * root : llvm::make_early_inc_range(closure->getUsers())) {
            root->erase();
        }
        closure.erase();
    }
    module.walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
    return llvm::Error::success();
}

llvm::Error prepareDOMEntry(mlir::ModuleOp module, HostContract & contract, unsigned maxSteps) {
    const auto refuse = [](const llvm::Twine & reason) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), reason);
    };
    if (contract.moduleSha256 != hostContractFingerprint(module) ||
        module->hasAttr("ctjs.skipped")) {
        return refuse("native DOM entry: fingerprint mismatch or incomplete source");
    }
    mlir::OwningOpRef<mlir::ModuleOp> composed(module.clone());
    HostContract transformed = contract;
    if (llvm::is_contained(contract.initialIntrinsics, host_detail::classDefinedIntrinsic)) {
        if (auto error = normalizeDOMClasses(*composed, transformed, maxSteps)) {
            return refuse("native DOM class: " + llvm::toString(std::move(error)));
        }
        if (auto error = liftDOMClasses(*composed, maxSteps)) { return error; }
        transformed.moduleSha256 = hostContractFingerprint(*composed);
    }
    // A handler in the entry is normalized in place. A handler
    // owned by a local helper is normalized first, under the
    // same fingerprinted proof, so helper expansion then
    // inlines one structured invoke.
    llvm::SmallVector<std::string> handlers;
    for (auto function : composed->getOps<ctjs::FuncOp>()) {
        bool hasHandler = false;
        function.walk([&](ctjs::PushHandlerOp) { hasHandler = true; });
        if (hasHandler) { handlers.push_back(function.getSymName().str()); }
    }
    const bool entryHandler = llvm::is_contained(handlers, contract.entry);
    llvm::Error sourceError = llvm::Error::success();
    for (const std::string & handler : handlers) {
        if (sourceError) { break; }
        sourceError = lowering_detail::normalizeDOMURI(*composed, transformed, maxSteps, handler);
        transformed.moduleSha256 = hostContractFingerprint(*composed);
    }
    if (!sourceError && !entryHandler) {
        sourceError = expandDOMHelpers(*composed, contract.entry, maxSteps);
    }
    if (sourceError) {
        return refuse("native DOM source: " + llvm::toString(std::move(sourceError)));
    }
    transformed.moduleSha256 = hostContractFingerprint(*composed);
    // Helper expansion resolves proved local cells and maps
    // helper formals back to the validated entry parameters.
    if (auto error = normalizeDOMElementGuards(*composed, transformed, maxSteps)) {
        return refuse("native DOM element guard: " + llvm::toString(std::move(error)));
    }
    transformed.moduleSha256 = hostContractFingerprint(*composed);
    if (auto error = normalizeDOMIteration(*composed, transformed, maxSteps)) {
        return refuse("native DOM iteration: " + llvm::toString(std::move(error)));
    }
    transformed.moduleSha256 = hostContractFingerprint(*composed);
    // An explicit library entry may replace only its proved
    // inert declaration wrapper. Prepare privately, discard
    // supplied native reports, and reprove before publishing
    // the exported function. `source` borrows `composed`, so
    // the swap waits until it is gone.
    mlir::OwningOpRef<mlir::ModuleOp> prepared;
    {
        const DOMEntryAnalysis source(*composed, transformed, maxSteps);
        if (!source.proved()) { return refuse("native DOM entry: " + source.reason()); }
        mlir::IRMapping mapping;
        prepared = llvm::cast<mlir::ModuleOp>((*composed)->clone(mapping));
        if (auto wrapper = source.wrapper()) {
            prepared->lookupSymbol<ctjs::FuncOp>(wrapper.getSymName()).erase();
        }
        // Make proved callback bodies reachable to sparse dataflow;
        // they still emit as ordinary internal C++ functions.
        for (ctjs::FuncOp callback : source.callbacks()) {
            prepared->lookupSymbol<ctjs::FuncOp>(callback.getSymName()).setPublic();
        }
        // Only the initialized DOM provider and complete source
        // proof authorize this binding. Normalize in the private
        // clone, then reprove it; no VM lookup or C++ global
        // survives emission.
        prepared->walk([](ctjs::LoadGlobalOp load) {
            if (load.getName() != "undefined") { return; }
            mlir::OpBuilder at(load);
            auto constant = ctjs::ConstantOp::create(at, load.getLoc(),
                                                     ctjs::UndefinedAttr::get(load.getContext()));
            load.getResult().replaceAllUsesWith(constant.getResult());
            load.erase();
        });
        // Normalize optional force while the original method
        // proof is available. Token lists omit undefined;
        // Element coerces it to false. The emitter then needs
        // only ordinary Boolean arguments.
        source.entry().walk([&](ctjs::CallOp original) {
            const auto * edge = source.call(original);
            if (!edge || (edge->kind != HostDOMMethod::toggleClass &&
                          edge->kind != HostDOMMethod::toggleAttribute)) {
                return;
            }
            auto call = llvm::cast<ctjs::CallOp>(mapping.lookup(original.getOperation()));
            if (call.getArgs().size() != 2) { return; }
            auto force = call.getArgs()[1].getDefiningOp<ctjs::ConstantOp>();
            if (!force || !llvm::isa<ctjs::UndefinedAttr>(force.getValue())) { return; }
            if (edge->kind == HostDOMMethod::toggleClass) {
                call.getArgsMutable().erase(1);
            } else {
                mlir::OpBuilder at(call);
                auto value = ctjs::ConstantOp::create(
                    at, call.getLoc(), ctjs::BooleanAttr::get(call.getContext(), false));
                call.getArgsMutable().slice(1, 1).assign(value.getResult());
            }
        });
    }
    prepared->walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
    prepared->lookupSymbol<ctjs::FuncOp>(contract.entry).setPublic();

    transformed.moduleSha256 = hostContractFingerprint(*prepared);
    const DOMEntryAnalysis checked(*prepared, transformed, maxSteps);
    if (!checked.proved()) { return refuse("native DOM entry preparation: " + checked.reason()); }
    module->setAttrs((*prepared)->getAttrs());
    module.getBodyRegion().takeBody(prepared->getBodyRegion());
    contract = std::move(transformed);
    return llvm::Error::success();
}

} // namespace ctcompile::ctnative
