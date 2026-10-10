//===- HostPrefix.h - Host entry prefix evaluation --------------*- C++ -*-===//
//
// HostEntryPrefixAnalysis follows a closed-source host entry symbolically up
// to the first value or effect it cannot model, and records what that prefix
// proved: which branches it took, which indirect calls resolve to which
// function, the factory tables it built, the properties it published, and
// (optionally) what the provider's Maps and objects answered. Each Host*
// struct below is one such recorded fact. Nothing after the stopping point is
// assumed. ctcompile/docs/native-host-prefix.md describes the proof boundary.
//
// A "provider" is the library factory the entry calls (for example a UMD
// wrapper's factory); "normal completion" means the path that does not throw.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::ctnative {

/// An `scf.if` the prefix evaluated, and which arm it took.
struct HostPrefixBranch {
    mlir::scf::IfOp operation;
    bool selected;
};

/// An indirect call the prefix resolved to exactly one function.
struct HostPrefixCall {
    ctjs::CallOp operation;
    ctjs::FuncOp target;
};

/// A closure that a factory call stores in the table it returns, on the normal
/// path, and the capture cell and resource it keeps alive. It says what the
/// table retains; it does not prove the code is pure or that native code may
/// own the object. Each factory call has its own table and resource.
struct HostPrefixCapture {
    std::string property;
    ctjs::CreateClosureOp closure;
    unsigned index;
    ctjs::CreateCellOp cell;
    ctjs::ConstructOp resource;
};

/// A factory call that returned a fresh method table, with the resources it
/// allocated and the closures the table captures.
struct HostPrefixFactory {
    ctjs::CallOp operation;
    ctjs::FuncOp target;
    ctjs::CreateObjectOp table;
    std::vector<ctjs::ConstructOp> resources;
    std::vector<HostPrefixCapture> captures;
};

/// A store of a factory's result into the host slot `binding`.`property`.
struct HostPrefixPublication {
    ctjs::SetPropertyOp operation;
    ctjs::CallOp factory;
    std::string binding;
    std::string property;
};

/// A read of `member` from a private Map that is still empty on the normal
/// return path. The Map operations and method bodies still run at run time.
/// The factory call tells apart allocations from the same source site; the
/// fact says nothing about call sites the analysis did not see.
struct HostPrefixProviderRead {
    mlir::Operation * operation;
    ctjs::ConstructOp resource;
    std::string member;
};

/// A provider call whose result the prefix computed from reads of empty Maps.
struct HostPrefixReadSummary {
    ctjs::CallOp operation;
    ctjs::FuncOp target;
    ctjs::CallOp factory;
    mlir::Attribute result;
    std::vector<HostPrefixProviderRead> reads;
};

/// A Map the provider allocated during the prefix. `mapId` numbers each
/// executed allocation from one, so repeated sites get distinct ids.
struct HostPrefixProviderAllocation {
    ctjs::ConstructOp operation;
    ctjs::CallOp invocation;
    unsigned mapId;
};

/// The result of a builtin Map operation on the normal path. A result that is
/// itself a Map or object names it by its one-based id (0 = not one).
struct HostPrefixProviderOperation {
    mlir::Operation * operation;
    HostPrefixProviderAllocation resource;
    std::string member;
    mlir::Attribute result;
    unsigned resultMapId = 0;
    unsigned resultObjectId = 0;
};

/// A field operation on a source object allocated during the single executed
/// entry call, and its result on the normal path. This does not establish
/// native ownership.
struct HostPrefixProviderObjectOperation {
    mlir::Operation * operation;
    ctjs::CreateObjectOp allocation;
    mlir::Operation * invocation;
    unsigned objectId;
    std::string member;
    std::string action;
    mlir::Attribute result;
};

/// A global the provider wrote during a callback, and the value written.
struct HostPrefixProviderGlobalWrite {
    ctjs::StoreGlobalOp operation;
    std::string binding;
    mlir::Attribute value;
};

/// A source callback the provider invoked, its result and its global writes.
struct HostPrefixProviderCallback {
    ctjs::CallOp operation;
    ctjs::FuncOp target;
    mlir::Attribute result;
    std::vector<HostPrefixProviderGlobalWrite> writes;
};

/// Everything the prefix computed for one provider call.
struct HostPrefixProviderSummary {
    ctjs::CallOp operation;
    ctjs::FuncOp target;
    ctjs::CallOp factory;
    mlir::Attribute result;
    std::vector<HostPrefixProviderAllocation> allocations;
    std::vector<HostPrefixProviderOperation> operations;
    std::vector<HostPrefixProviderCallback> callbacks;
    unsigned resultObjectId = 0;
    std::vector<HostPrefixProviderObjectOperation> objectOperations{};
};

/// Which provider effects the prefix proof may follow past the entry. Each flag
/// needs the ones before it (HostEntryPrefixAnalysis refuses otherwise).
struct HostPrefixFollow {
    bool publication = false;
    bool providerReads = false;
    bool providerMutations = false;
    bool providerDiagnostics = false;
    bool providerCallbacks = false;
    bool providerObjects = false;
};

/// A narrower proof than HostContractAnalysis: only the prefix executed before
/// the first unknown effect is interpreted. Branch and call proofs are made
/// only for functions reachable by exactly one invocation path from the entry.
/// What happens after the prefix stays unknown. The result points into the
/// current IR, so any semantic change to the module invalidates it.
class HostEntryPrefixAnalysis {
public:
    HostEntryPrefixAnalysis(mlir::ModuleOp module, const HostContract & contract,
                            unsigned maxSteps = 100000, bool followPublication = false,
                            bool followProviderReads = false, bool followProviderMutations = false,
                            bool followProviderDiagnostics = false,
                            bool followProviderCallbacks = false,
                            bool followProviderObjects = false);
    HostEntryPrefixAnalysis(mlir::ModuleOp module, const HostContract & contract, unsigned maxSteps,
                            HostPrefixFollow follow)
        : HostEntryPrefixAnalysis(module, contract, maxSteps, follow.publication,
                                  follow.providerReads, follow.providerMutations,
                                  follow.providerDiagnostics, follow.providerCallbacks,
                                  follow.providerObjects) {}

    [[nodiscard]] bool valid() const { return refusal.empty(); }
    [[nodiscard]] llvm::StringRef reason() const { return refusal; }
    /// Why the prefix stopped (the first unmodeled value or effect).
    [[nodiscard]] llvm::StringRef boundary() const { return stopped; }
    /// Why following the provider stopped.
    [[nodiscard]] llvm::StringRef providerBoundary() const { return providerStopped; }
    [[nodiscard]] llvm::ArrayRef<HostPrefixBranch> branches() const { return branchProofs; }
    [[nodiscard]] llvm::ArrayRef<HostPrefixCall> calls() const { return callProofs; }
    [[nodiscard]] llvm::ArrayRef<HostPrefixFactory> factories() const { return factoryProofs; }
    [[nodiscard]] llvm::ArrayRef<HostPrefixPublication> publications() const {
        return publicationProofs;
    }
    [[nodiscard]] llvm::ArrayRef<HostPrefixReadSummary> providerReads() const { return readProofs; }
    [[nodiscard]] llvm::ArrayRef<HostPrefixProviderSummary> providerCalls() const {
        return providerProofs;
    }

private:
    std::string refusal;
    std::string stopped;
    std::string providerStopped;
    std::vector<HostPrefixBranch> branchProofs;
    std::vector<HostPrefixCall> callProofs;
    std::vector<HostPrefixFactory> factoryProofs;
    std::vector<HostPrefixPublication> publicationProofs;
    std::vector<HostPrefixReadSummary> readProofs;
    std::vector<HostPrefixProviderSummary> providerProofs;
};

} // namespace ctcompile::ctnative
