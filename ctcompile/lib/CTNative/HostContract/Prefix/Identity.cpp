//===- Identity.cpp - Callable-identity checks for the host prefix --------===//
//
// Checks run after the host-prefix analysis has proved branch arms and call
// targets. A function whose body gets specialized must not be able to observe
// which callable is active, so every operation in it must be one the analysis
// models: no unknown calls, no reads of objects it did not create, and only
// global reads whose binding is known to be initialized.
//
//===----------------------------------------------------------------------===//

#include "Analysis.h"

#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/ScopeExit.h"

namespace ctcompile::ctnative::host_detail {

/// True when `load` reads a global binding that is known to exist: a source
/// declaration, a contract intrinsic or realm property, a global the analysis
/// tracked, or a literal stored earlier in the same function.
bool prefixAnalysis::initializedGlobal(ctjs::LoadGlobalOp load) {
    // A source declaration proves the binding exists, not what it holds: a
    // bare `var` keeps any preexisting global value and emits no store.
    if (declaredGlobals.contains(load.getName())) { return true; }
    if (llvm::is_contained(contract.initialIntrinsics, load.getName()) ||
        llvm::is_contained(contract.realmOwnDataProperties, load.getName()) ||
        (contract.realmGlobalThis && load.getName() == "globalThis")) {
        return true;
    }
    const auto present = globals.find(load.getName());
    if (present != globals.end() && present->second.kind != prefixValue::Kind::unknown) {
        return true;
    }
    for (ctjs::StoreGlobalOp store : initializers[load.getName()]) {
        if (!step()) { return false; }
        // Only a literal stored earlier in the same function, on every path to
        // the read, counts. A store in another function, a later one, or one in
        // a conditional says nothing about the value the read sees.
        if (store->getParentOfType<ctjs::FuncOp>() == load->getParentOfType<ctjs::FuncOp>() &&
            dominance.properlyDominates(store.getOperation(), load.getOperation()) &&
            llvm::isa_and_nonnull<ctjs::ConstantOp, ctjs::CreateObjectOp, ctjs::CreateClosureOp>(
                store.getValue().getDefiningOp())) {
            return true;
        }
    }
    return false;
}

/// True when `function` captures nothing and its body passes
/// identitySafeRegion. `stack` holds the functions being checked, so recursion
/// refuses.
bool prefixAnalysis::identitySafeFunction(ctjs::FuncOp function,
                                          llvm::DenseSet<mlir::Operation *> & stack) {
    if (!step() || !function || function.getBody().empty() || function.getUpvalueCount() != 0 ||
        !stack.insert(function).second) {
        return false;
    }
    const llvm::scope_exit leave([&] { stack.erase(function); });
    return identitySafeRegion(function.getBody(), stack);
}

/// True when every operation in `region` is one the analysis models: proved
/// branches and calls (followed recursively), `new Map()`, property access on
/// fresh objects or the realm's own data properties, initialized global reads,
/// throws of constants, and side-effect-free arithmetic and bookkeeping.
bool prefixAnalysis::identitySafeRegion(mlir::Region & region,
                                        llvm::DenseSet<mlir::Operation *> & stack) {
    if (region.empty()) { return true; }
    if (!llvm::hasSingleElement(region)) { return false; }
    for (mlir::Operation & operation : region.front()) {
        if (!step()) { return false; }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            const auto proof = llvm::find_if(
                branches, [&](const auto & candidate) { return candidate.operation == branch; });
            const auto selected =
                proof == branches.end() ? std::nullopt : std::optional(proof->selected);
            for (unsigned arm = 0; arm < 2; ++arm) {
                if (selected && arm != (*selected ? 0u : 1u)) { continue; }
                if (!identitySafeRegion(branch->getRegion(arm), stack)) { return false; }
            }
            continue;
        }
        ctjs::FuncOp callee;
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
            const auto proof = llvm::find_if(
                calls, [&](const auto & candidate) { return candidate.operation == call; });
            if (proof != calls.end()) { callee = proof->target; }
            if (!callee || !identitySafeFunction(callee, stack)) { return false; }
            continue;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
            auto closure = call.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>();
            auto target = call.getTarget();
            if (closure && closure.getFunction() >= 0 &&
                functions.lookup(static_cast<unsigned>(closure.getFunction())) == target) {
                callee = target;
            }
            if (!callee || !identitySafeFunction(callee, stack)) { return false; }
            continue;
        }
        if (auto built = llvm::dyn_cast<ctjs::ConstructOp>(operation)) {
            if (!plainMapConstruction(built) ||
                !llvm::is_contained(contract.initialIntrinsics, "Map")) {
                return false;
            }
            // A zero-argument `new Map()` cannot inspect the caller or run
            // script. Whether it allocates or throws is still decided at run time.
            continue;
        }
        if (auto key = ctjs::propertyKey(&operation)) {
            const auto receiver = operation.getOperand(0); // get/set_property's object
            // The realm object is never modeled as a fresh object. Only its
            // writable own data properties listed in the contract are safe,
            // because accessing them cannot run a callback. Their values and
            // the effects of writing them are still checked at run time.
            if (observedValues.lookup(receiver).kind == prefixValue::Kind::realm &&
                llvm::is_contained(contract.realmOwnDataProperties, ctjs::constantKey(key))) {
                continue;
            }
            if (!receiver.getDefiningOp<ctjs::CreateObjectOp>() &&
                observedValues.lookup(receiver).kind != prefixValue::Kind::object) {
                return false;
            }
            continue;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            if (!initializedGlobal(load)) { return false; }
            continue;
        }
        if (auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(operation)) {
            if (!thrown.getValue().getDefiningOp<ctjs::ConstantOp>()) { return false; }
            continue;
        }
        if (llvm::isa<ctjs::UnaryOp>(operation)) {
            if (!ctjs::nonCoercing(&operation)) { return false; }
            continue;
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
            if (compare.getKind() != ctjs::CompareKind::StrictEq &&
                observedValues.lookup(compare.getResult()).kind == prefixValue::Kind::unknown) {
                return false;
            }
            continue;
        }
        if (llvm::isa<ctjs::ConstantOp, ctjs::CreateObjectOp, ctjs::CreateClosureOp,
                      ctjs::CreateCellOp, ctjs::CellGetOp, ctjs::CellSetOp, ctjs::StoreGlobalOp,
                      ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, ctjs::TruthyOp,
                      ctjs::ReturnOp, mlir::scf::YieldOp>(operation) ||
            operation.getName().getDialectNamespace() == "arith") {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace ctcompile::ctnative::host_detail
