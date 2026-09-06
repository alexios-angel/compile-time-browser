#include "Prefix.h"

#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/ScopeExit.h"

namespace ctcompile::ctnative::host_detail {

bool prefixAnalysis::initializedGlobal(ctjs::LoadGlobalOp load) {
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
        // A store elsewhere, later, or in a conditional does not characterize
        // an initial host value. This fallback covers source literals created
        // in the entry suffix before their own subsequent reads.
        if (store->getParentOfType<ctjs::FuncOp>() == load->getParentOfType<ctjs::FuncOp>() &&
            dominance.properlyDominates(store.getOperation(), load.getOperation()) &&
            llvm::isa_and_nonnull<ctjs::ConstantOp, ctjs::CreateObjectOp, ctjs::CreateClosureOp>(
                store.getValue().getDefiningOp())) {
            return true;
        }
    }
    return false;
}

bool prefixAnalysis::identitySafeFunction(ctjs::FuncOp function,
                                          llvm::DenseSet<mlir::Operation *> & stack) {
    if (!step() || !function || function.getBody().empty() || function.getUpvalueCount() != 0 ||
        !stack.insert(function).second) {
        return false;
    }
    const llvm::scope_exit leave([&] { stack.erase(function); });
    return identitySafeRegion(function.getBody(), stack);
}

bool prefixAnalysis::identitySafeRegion(mlir::Region & region,
                                        llvm::DenseSet<mlir::Operation *> & stack) {
    if (region.empty()) { return true; }
    if (!llvm::hasSingleElement(region)) { return false; }
    for (mlir::Operation & operation : region.front()) {
        if (!step()) { return false; }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            std::optional<bool> selected;
            for (auto proof : branches) {
                if (proof.operation == branch) {
                    selected = proof.selected;
                    break;
                }
            }
            for (unsigned arm = 0; arm < 2; ++arm) {
                if (selected && arm != (*selected ? 0u : 1u)) { continue; }
                if (!identitySafeRegion(branch->getRegion(arm), stack)) { return false; }
            }
            continue;
        }
        ctjs::FuncOp callee;
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
            for (auto proof : calls) {
                if (proof.operation == call) {
                    callee = proof.target;
                    break;
                }
            }
            if (!callee || !identitySafeFunction(callee, stack)) { return false; }
            continue;
        }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
            auto closure = call.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>();
            auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                call, call.getCalleeAttr());
            if (closure && closure.getFunction() >= 0 &&
                functions.lookup(static_cast<unsigned>(closure.getFunction())) == target) {
                callee = target;
            }
            if (!callee || !identitySafeFunction(callee, stack)) { return false; }
            continue;
        }
        if (auto built = llvm::dyn_cast<ctjs::ConstructOp>(operation)) {
            auto load = built.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
            if (!load || load.getName() != "Map" ||
                !llvm::is_contained(contract.initialIntrinsics, "Map") ||
                built.getNewTarget() != load.getResult() || !built.getArgs().empty()) {
                return false;
            }
            // Standard zero-argument Map construction cannot inspect the
            // caller or invoke JS. Allocation/throw behavior remains runtime.
            continue;
        }
        mlir::Value receiver, propertyKey;
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            receiver = get.getObject();
            propertyKey = get.getKey();
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            receiver = set.getObject();
            propertyKey = set.getKey();
        }
        if (receiver) {
            // The realm is opaque and never joins the fresh-object heap.
            // Only the embedding's finite writable own-data slots exclude a
            // callback here. Their values and publication effects stay live.
            if (observedValues.lookup(receiver).kind == prefixValue::Kind::realm &&
                llvm::is_contained(contract.realmOwnDataProperties, keyOf(propertyKey))) {
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
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
            if (unary.getKind() != ctjs::UnaryKind::TypeOf &&
                unary.getKind() != ctjs::UnaryKind::Not &&
                unary.getKind() != ctjs::UnaryKind::Void) {
                return false;
            }
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
