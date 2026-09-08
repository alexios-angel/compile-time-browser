#pragma once

#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>

namespace ctcompile::ctnative {

struct OwnedGlobalMethod {
    ctjs::SetPropertyOp initialization;
    ctjs::CreateClosureOp closure;
    ctjs::FuncOp function;
};

// A callable table retained by the ordinary root. These are live
// source edges, not a native carrier or permission for future external calls.
struct OwnedGlobalMethodTable {
    mlir::Operation * factoryCall = nullptr;
    ctjs::FuncOp factory;
    ctjs::CreateObjectOp table;
    llvm::SmallVector<OwnedGlobalMethod> methods;
    llvm::SmallVector<HostCallableEdge, 0> calls;
    ctjs::FuncOp wrapper;
    ctjs::CallDirectOp wrapperCall;
    std::optional<HostCapturedMap> capturedMap;
};

// One allocation, its owning global binding, and a fixed initialized field.
// This is a storage/identity proof, not a proof of the field's native type.
// Type inference and whole-component admission must still check that type.
struct OwnedGlobalRoot {
    ctjs::CreateObjectOp owner;
    ctjs::StoreGlobalOp initialization;
    llvm::SmallVector<ctjs::LoadGlobalOp> loads;
    ctjs::SetPropertyOp fieldInitialization;
    llvm::SmallVector<ctjs::GetPropertyOp> reads;
    std::string binding;
    std::string property;
    std::optional<OwnedGlobalMethodTable> methodTable;
};

// Borrows the current IR and validates the driver's exact fingerprint. Rebuild
// after any semantic change; neither stale handles nor report attributes prove
// ownership. The scalar tier requires one straight-line script entry; the
// method tier adds one uncaptured factory/getter pair. Both require one root
// binding and one ordinary field. Escape analysis still reports StoredGlobal.
class OwnedGlobalRoots {
public:
    OwnedGlobalRoots(mlir::ModuleOp module, const HostContract & contract,
                     unsigned maxSteps = 100000);

    [[nodiscard]] bool proved() const { return refusal.empty(); }
    [[nodiscard]] llvm::StringRef reason() const { return refusal; }
    [[nodiscard]] llvm::ArrayRef<OwnedGlobalRoot> roots() const { return checked; }
    // Covers the allocation, global initialization/loads and field write/reads.
    [[nodiscard]] const OwnedGlobalRoot * lookup(mlir::Operation * operation) const;
    [[nodiscard]] unsigned steps() const { return workSteps; }
    [[nodiscard]] bool exhausted() const { return budgetExhausted; }

private:
    void analyzeMethodTable(mlir::ModuleOp module, const HostContract & contract,
                            const HostContractAnalysis & host, unsigned maxSteps);
    llvm::SmallVector<OwnedGlobalRoot, 1> checked;
    llvm::DenseMap<mlir::Operation *, unsigned> edges;
    std::string refusal;
    unsigned workSteps = 0;
    bool budgetExhausted = false;
};

} // namespace ctcompile::ctnative
