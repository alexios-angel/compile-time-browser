//===- ClassInitialization.cpp - Class init proof -------------------------===//
//
// The ctnative-specialize-class-initialization pass and normalizeDOMClasses.
// Both bind the module to its host contract, prove that local class setup
// (descriptor helpers, prototype, constructor and home-object edges) is
// unobservable, and only then rewrite it away. The proof itself lives in
// ClassInitialization/; the pass works on a clone so a refusal leaves the
// module untouched.
//
//===----------------------------------------------------------------------===//

#include "../Lowering/Exceptions/Recovery.h"
#include "Analysis.h"
#include "ClassInitialization/Proof.hpp"
#include "Preparation.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Transforms/Passes.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/MemoryBuffer.h"

namespace ctcompile::ctnative {
using class_detail::classInitialization;
#define GEN_PASS_DEF_CTNATIVESPECIALIZECLASSINITIALIZATION
#include "ctcompile/CTNative/Transforms/Passes.h.inc"
namespace {

/// Proves `proof`'s module, checks `binding` against `bindingModule`, then
/// normalizes methods and rewrites. Returns the refusal reason from the first
/// step that fails, or nullopt on success.
std::optional<std::string> proveAndRewrite(classInitialization & proof,
                                           mlir::ModuleOp bindingModule,
                                           const HostContract & contract,
                                           const HostContract & binding, bool domEntry) {
    if (!proof.prove(contract, domEntry)) { return proof.reason; }
    if (auto problem = host_detail::initialBindingProblem(bindingModule, binding);
        !problem.empty()) {
        return problem;
    }
    if (!proof.normalizeMethods()) { return proof.reason; }
    proof.rewrite();
    return std::nullopt;
}

struct CTNativeSpecializeClassInitializationPass
    : impl::CTNativeSpecializeClassInitializationBase<CTNativeSpecializeClassInitializationPass> {
    using CTNativeSpecializeClassInitializationBase::CTNativeSpecializeClassInitializationBase;

    void runOnOperation() override {
        auto module = getOperation();
        auto buffer = llvm::MemoryBuffer::getFile(manifest);
        if (!buffer) {
            module.emitError("class initialization requires a readable host manifest");
            return signalPassFailure();
        }
        auto contract = parseHostContract((*buffer)->getBuffer());
        if (!contract) {
            module.emitError() << llvm::toString(contract.takeError());
            return signalPassFailure();
        }
        if (hostContractFingerprint(module) != contract->moduleSha256) {
            module.emitError("class initialization host fingerprint mismatch");
            return signalPassFailure();
        }
        if ((contract->provider != HostContract::Provider::closedSource &&
             contract->provider != HostContract::Provider::ctbrowserDOMDataSession) ||
            !llvm::is_contained(contract->initialIntrinsics, host_detail::classDefinedIntrinsic) ||
            llvm::any_of(contract->initialIntrinsics,
                         [](const auto & name) {
                             return !host_detail::classIntrinsicArity(name) &&
                                    !llvm::is_contained(
                                        {"Error", "Function", "Object", "Array", "Map"}, name) &&
                                    !host_detail::iteratorIntrinsicArity(name);
                         }) ||
            contract->realmGlobalThis || contract->classicScriptRealm ||
            !contract->absentBindings.empty() || !contract->undefinedBindings.empty()) {
            module.emitError("class initialization requires standard class helper identities and "
                             "optional Error/Object/Array/Map/Function iterator identities");
            return signalPassFailure();
        }
        // Super normalization is speculative, so the proof runs on a clone and
        // a refusal publishes no partial body.
        WorkBudget budget{maxSteps};
        if (!budget.spendWalk(module, class_detail::cloneCost)) {
            module.emitError("class initialization work budget exhausted");
            return signalPassFailure();
        }
        mlir::OwningOpRef<mlir::ModuleOp> candidate(module.clone());
        classInitialization proof{*candidate, static_cast<unsigned>(budget.remaining)};
        if (auto refusal = proveAndRewrite(proof, module, *contract, *contract, false)) {
            module.emitError() << *refusal;
            return signalPassFailure();
        }
        module->setAttrs((*candidate)->getAttrs());
        module.getBodyRegion().takeBody(candidate->getBodyRegion());
    }
};

} // namespace

llvm::Error normalizeDOMClasses(mlir::ModuleOp module, HostContract & contract, unsigned maxSteps) {
    const auto refuse = [](const llvm::Twine & reason) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), reason);
    };
    if ((contract.provider != HostContract::Provider::ctbrowserDOM &&
         contract.provider != HostContract::Provider::ctbrowserDOMSession) ||
        llvm::count(contract.initialIntrinsics, host_detail::classDefinedIntrinsic) != 1 ||
        llvm::count(contract.initialIntrinsics, "Error") > 1 || contract.realmGlobalThis ||
        contract.classicScriptRealm || !contract.absentBindings.empty() ||
        !contract.undefinedBindings.empty() || !contract.realmOwnDataProperties.empty()) {
        return refuse("DOM class initialization requires the standard class helper and optional "
                      "Error identity");
    }
    classInitialization proof{module, maxSteps};
    // Reuse the binding proof before consuming its declaration. This projected
    // contract proves the helper and optional Error identities; DOM
    // declarations and parameters are proved later. Referenced throwing getters
    // keep their original bodies and must pass the final typed DOM proof after
    // rewriting.

    if (auto refusal = proveAndRewrite(proof, module, contract,
                                       class_detail::classBindingContract(contract), true)) {
        return refuse(*refusal);
    }
    llvm::erase_if(contract.initialIntrinsics, [](const auto & name) {
        // The class proof normalized these helpers after checking their
        // identities. Any residual use must pass the complete DOM proof.
        return host_detail::classIntrinsicArity(name) || name == "Error";
    });
    if (!proof.proveDOMMethods(contract)) { return refuse(proof.reason); }
    return llvm::Error::success();
}

} // namespace ctcompile::ctnative
