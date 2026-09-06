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

// A narrower proof than HostContractAnalysis: only the executed prefix before
// the first unknown effect is interpreted. Branch/call proofs belong to
// functions with one closed invocation path. Later behavior remains unknown.
// The result borrows current IR; any semantic mutation invalidates it.
class HostEntryPrefixAnalysis {
public:
    HostEntryPrefixAnalysis(mlir::ModuleOp module, const HostContract & contract,
                            unsigned maxSteps = 100000, bool followPublication = false);

    [[nodiscard]] bool valid() const { return refusal.empty(); }
    [[nodiscard]] llvm::StringRef reason() const { return refusal; }
    [[nodiscard]] llvm::StringRef boundary() const { return stopped; }
    [[nodiscard]] llvm::ArrayRef<HostPrefixBranch> branches() const { return branchProofs; }
    [[nodiscard]] llvm::ArrayRef<HostPrefixCall> calls() const { return callProofs; }
    [[nodiscard]] llvm::ArrayRef<HostPrefixFactory> factories() const { return factoryProofs; }
    [[nodiscard]] llvm::ArrayRef<HostPrefixPublication> publications() const {
        return publicationProofs;
    }

private:
    std::string refusal;
    std::string stopped;
    std::vector<HostPrefixBranch> branchProofs;
    std::vector<HostPrefixCall> callProofs;
    std::vector<HostPrefixFactory> factoryProofs;
    std::vector<HostPrefixPublication> publicationProofs;
};

} // namespace ctcompile::ctnative
