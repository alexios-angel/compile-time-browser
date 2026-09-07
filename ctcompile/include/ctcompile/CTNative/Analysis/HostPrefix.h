#pragma once

#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::ctnative {

struct HostPrefixBranch {
    mlir::scf::IfOp operation;
    bool selected;
};

struct HostPrefixCall {
    ctjs::CallOp operation;
    ctjs::FuncOp target;
};

// Normal-completion retention facts, not an execution/purity or native owner
// proof. Each factory invocation has its own returned table and resource.
struct HostPrefixCapture {
    std::string property;
    ctjs::CreateClosureOp closure;
    unsigned index;
    ctjs::CreateCellOp cell;
    ctjs::ConstructOp resource;
};

struct HostPrefixFactory {
    ctjs::CallOp operation;
    ctjs::FuncOp target;
    ctjs::CreateObjectOp table;
    std::vector<ctjs::ConstructOp> resources;
    std::vector<HostPrefixCapture> captures;
};

struct HostPrefixPublication {
    ctjs::SetPropertyOp operation;
    ctjs::CallOp factory;
    std::string binding;
    std::string property;
};

// A normal-return path over still-empty private Maps. Operations and method
// bodies remain runtime. The factory call distinguishes repeated allocations
// from the same source site; these facts do not close future call sites.
struct HostPrefixProviderRead {
    mlir::Operation * operation;
    ctjs::ConstructOp resource;
    std::string member;
};

struct HostPrefixReadSummary {
    ctjs::CallOp operation;
    ctjs::FuncOp target;
    ctjs::CallOp factory;
    mlir::Attribute result;
    std::vector<HostPrefixProviderRead> reads;
};

struct HostPrefixProviderAllocation {
    ctjs::ConstructOp operation;
    ctjs::CallOp invocation;
    unsigned mapId;
};

// A builtin operation's result on normal completion. Map identities are
// one-based and distinct per executed allocation, including repeated sites.
struct HostPrefixProviderOperation {
    mlir::Operation * operation;
    HostPrefixProviderAllocation resource;
    std::string member;
    mlir::Attribute result;
    unsigned resultMapId = 0;
    unsigned resultObjectId = 0;
};

// Identity belongs to an actual source allocation in the one executed entry
// invocation. These normal-path field facts do not establish native ownership.
struct HostPrefixProviderObjectOperation {
    mlir::Operation * operation;
    ctjs::CreateObjectOp allocation;
    mlir::Operation * invocation;
    unsigned objectId;
    std::string member;
    std::string action;
    mlir::Attribute result;
};

struct HostPrefixProviderGlobalWrite {
    ctjs::StoreGlobalOp operation;
    std::string binding;
    mlir::Attribute value;
};

struct HostPrefixProviderCallback {
    ctjs::CallOp operation;
    ctjs::FuncOp target;
    mlir::Attribute result;
    std::vector<HostPrefixProviderGlobalWrite> writes;
};

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

// A narrower proof than HostContractAnalysis: only the executed prefix before
// the first unknown effect is interpreted. Branch/call proofs belong to
// functions with one closed invocation path. Later behavior remains unknown.
// The result borrows current IR; any semantic mutation invalidates it.
class HostEntryPrefixAnalysis {
public:
    HostEntryPrefixAnalysis(mlir::ModuleOp module, const HostContract & contract,
                            unsigned maxSteps = 100000, bool followPublication = false,
                            bool followProviderReads = false, bool followProviderMutations = false,
                            bool followProviderDiagnostics = false,
                            bool followProviderCallbacks = false,
                            bool followProviderObjects = false);

    [[nodiscard]] bool valid() const { return refusal.empty(); }
    [[nodiscard]] llvm::StringRef reason() const { return refusal; }
    [[nodiscard]] llvm::StringRef boundary() const { return stopped; }
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
