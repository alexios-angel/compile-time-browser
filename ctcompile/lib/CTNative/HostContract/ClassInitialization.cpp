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
                             return !host_detail::classIntrinsicArity(name) && name != "Error" &&
                                    name != "Function" && name != "Object" && name != "Array" &&
                                    name != "Map" && !host_detail::iteratorIntrinsicArity(name);
                         }) ||
            contract->realmGlobalThis || contract->classicScriptRealm ||
            !contract->absentBindings.empty() || !contract->undefinedBindings.empty()) {
            module.emitError("class initialization requires standard class helper identities and "
                             "optional Error/Object/Array/Map/Function iterator identities");
            return signalPassFailure();
        }
        // Super normalization is speculative; refusal publishes no partial body.
        unsigned remaining = maxSteps;
        const auto counted = module.walk([&](mlir::Operation * op) {
            const uint64_t cost = uint64_t(1) + op->getNumOperands() + op->getNumResults();
            if (cost > remaining) { return mlir::WalkResult::interrupt(); }
            remaining -= static_cast<unsigned>(cost);
            return mlir::WalkResult::advance();
        });
        if (counted.wasInterrupted()) {
            module.emitError("class initialization work budget exhausted");
            return signalPassFailure();
        }
        mlir::OwningOpRef<mlir::ModuleOp> candidate(module.clone());
        classInitialization proof{*candidate, remaining};
        if (!proof.prove(*contract)) {
            module.emitError() << proof.reason;
            return signalPassFailure();
        }
        if (auto problem = host_detail::initialBindingProblem(module, *contract);
            !problem.empty()) {
            module.emitError() << problem;
            return signalPassFailure();
        }
        if (!proof.normalizeMethods()) {
            module.emitError() << proof.reason;
            return signalPassFailure();
        }
        proof.rewrite();
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
    if (!proof.prove(contract, true)) { return refuse(proof.reason); }
    // Reuse the binding proof before consuming its declaration. This projected
    // contract proves helper and optional Error identity; DOM declarations and
    // parameters are proved later. Referenced throwing getters retain their
    // original bodies and must pass the final typed DOM proof after rewriting.
    HostContract binding = contract;
    binding.provider = HostContract::Provider::closedSource;
    binding.elementParameters.clear();
    llvm::erase_if(binding.initialIntrinsics, [](const auto & name) {
        return !host_detail::classIntrinsicArity(name) && name != "Error";
    });
    if (auto problem = host_detail::initialBindingProblem(module, binding); !problem.empty()) {
        return refuse(problem);
    }
    if (!proof.normalizeMethods()) { return refuse(proof.reason); }
    proof.rewrite();
    llvm::erase_if(contract.initialIntrinsics, [](const auto & name) {
        // Heritage and super helpers remain declared until their operations
        // have a semantic normalization; binding identity alone consumes none.
        return name == host_detail::classDefinedIntrinsic || name == "Error";
    });
    if (!proof.proveDOMMethods(contract)) { return refuse(proof.reason); }
    return llvm::Error::success();
}

} // namespace ctcompile::ctnative
