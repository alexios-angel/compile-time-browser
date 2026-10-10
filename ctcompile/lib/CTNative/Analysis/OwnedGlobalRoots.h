//===- OwnedGlobalRoots.h - Owned global roots ------------------*- C++ -*-===//
//
// Declares OwnedGlobalRoots, the proof that a global binding owns one object
// with one fixed field (optionally a method table), so native code may keep
// that object in place instead of boxing it.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>

namespace ctcompile::ctnative {

/// One method of an owned method table: the closure stored into the table
/// and the function it wraps.
struct OwnedGlobalMethod {
    ctjs::SetPropertyOp initialization;
    ctjs::CreateClosureOp closure;
    ctjs::FuncOp function;
};

/// A table of methods held in the root's field. These record which operations
/// the proof checked; they do not choose a native representation or allow
/// calls from outside the program.
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

/// One allocation, its owning global binding, and a fixed initialized field.
/// This proves storage and identity, not the field's native type; type
/// inference and admission must still check that type.
struct OwnedGlobalRoot {
    ctjs::CreateObjectOp owner;
    ctjs::StoreGlobalOp initialization;
    llvm::SmallVector<ctjs::LoadGlobalOp> loads;
    ctjs::SetPropertyOp fieldInitialization;
    llvm::SmallVector<ctjs::GetPropertyOp> reads;
    std::string binding;
    std::string property;
    std::optional<OwnedGlobalMethodTable> methodTable;
    ctjs::FuncOp scalarCallback{};

    /// The captured Map family of this root's method table, or null.
    [[nodiscard]] const HostCapturedMap * capturedMap() const {
        return methodTable && methodTable->capturedMap ? &*methodTable->capturedMap : nullptr;
    }
};

/// Proves which global bindings own a single object allocation with a fixed,
/// initialized field, so the native lowering may store it without boxing.
///
/// Runs HostContractAnalysis on the borrowed IR first; rebuild after any
/// change to the IR, since neither old operation handles nor IR attributes
/// prove ownership. A plain root needs a script entry with straight-line
/// code; a root holding a method table also allows one uncaptured factory
/// function and the methods it returns (see OwnedGlobalMethods.cpp). Either
/// way there is exactly one root binding and one ordinary field. Escape
/// analysis still reports such objects as StoredGlobal.
class OwnedGlobalRoots {
public:
    OwnedGlobalRoots(mlir::ModuleOp module, const HostContract & contract,
                     unsigned maxSteps = 100000);

    [[nodiscard]] bool proved() const { return refusal.empty(); }
    /// Why the proof failed; empty when proved().
    [[nodiscard]] llvm::StringRef reason() const { return refusal; }
    [[nodiscard]] llvm::ArrayRef<OwnedGlobalRoot> roots() const { return checked; }
    /// The host's inert source-declaration wrapper, which lowering may drop;
    /// null unless the host proof found one. Only the DOM Data session
    /// provider has one.
    [[nodiscard]] ctjs::FuncOp wrapper() const { return declarationWrapper; }
    /// The entry's DOM element parameters (DOM Data session provider only).
    [[nodiscard]] llvm::ArrayRef<mlir::BlockArgument> domInputs() const { return checkedDOMInputs; }
    /// The root an operation belongs to: its allocation, global store or
    /// loads, or field write or reads. Null for any other operation.
    [[nodiscard]] const OwnedGlobalRoot * lookup(mlir::Operation * operation) const;
    /// Reads of globals holding saved scalar values, checked separately from
    /// object ownership: a saved scalar never proves a root, field or native
    /// schema. Empty unless both proofs passed.
    [[nodiscard]] llvm::ArrayRef<HostScalarGlobalRead> scalarReads() const {
        return checkedScalarReads;
    }
    [[nodiscard]] const HostScalarGlobalRead * scalarRead(ctjs::LoadGlobalOp read) const;
    /// Whether `read` loads a global that a scalar callback of a captured Map
    /// may change.
    [[nodiscard]] bool mutableScalarRead(ctjs::LoadGlobalOp read) const;
    /// Globals holding objects used as Map keys, with their store and loads.
    /// These keep their original global operations; only the captured Map
    /// proof supplies them.
    [[nodiscard]] llvm::ArrayRef<HostObjectGlobalRead> objectReads() const {
        return checkedObjectReads;
    }
    [[nodiscard]] const HostObjectGlobalRead * objectGlobal(mlir::Operation * operation) const;
    /// The caller's object that the method call producing `value` returns, or
    /// null. This describes this one call only, not the method's return
    /// schema in general.
    [[nodiscard]] ctjs::CreateObjectOp returnedLeaf(mlir::Value value) const;
    /// The proved primitive kinds of a method call's scalar result.
    [[nodiscard]] PrimitiveAlternatives returnedScalar(mlir::Value value) const;
    [[nodiscard]] unsigned steps() const { return workSteps; }
    [[nodiscard]] bool exhausted() const { return budgetExhausted; }

private:
    void analyzeMethodTable(mlir::ModuleOp module, const HostContract & contract,
                            const HostContractAnalysis & host, unsigned maxSteps);
    /// Spends one step of `maxSteps`; on exhaustion records `why` and fails.
    bool spend(unsigned maxSteps, llvm::StringRef why);
    llvm::SmallVector<OwnedGlobalRoot, 1> checked;
    llvm::DenseMap<mlir::Operation *, unsigned> edges;
    llvm::SmallVector<HostScalarGlobalRead> checkedScalarReads;
    llvm::DenseMap<mlir::Operation *, unsigned> scalarEdges;
    llvm::SmallVector<HostObjectGlobalRead> checkedObjectReads;
    llvm::DenseMap<mlir::Operation *, unsigned> objectEdges;
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> returnedScalarEdges;
    ctjs::FuncOp declarationWrapper;
    llvm::SmallVector<mlir::BlockArgument> checkedDOMInputs;
    std::string refusal;
    unsigned workSteps = 0;
    bool budgetExhausted = false;
};

} // namespace ctcompile::ctnative
