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

// A narrower proof than HostContractAnalysis: only the executed prefix before
// the first unknown effect is interpreted. Branch/call proofs belong to
// functions with one closed invocation path. Later behavior remains unknown.
// The result borrows current IR; any semantic mutation invalidates it.
class HostEntryPrefixAnalysis {
public:
    HostEntryPrefixAnalysis(mlir::ModuleOp module, const HostContract & contract,
                            unsigned maxSteps = 100000);

    [[nodiscard]] bool valid() const { return refusal.empty(); }
    [[nodiscard]] llvm::StringRef reason() const { return refusal; }
    [[nodiscard]] llvm::StringRef boundary() const { return stopped; }
    [[nodiscard]] llvm::ArrayRef<HostPrefixBranch> branches() const { return branchProofs; }
    [[nodiscard]] llvm::ArrayRef<HostPrefixCall> calls() const { return callProofs; }

private:
    std::string refusal;
    std::string stopped;
    std::vector<HostPrefixBranch> branchProofs;
    std::vector<HostPrefixCall> callProofs;
};

} // namespace ctcompile::ctnative
