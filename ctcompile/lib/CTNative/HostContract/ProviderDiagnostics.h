#pragma once

#include "Prefix.h"

namespace ctcompile::ctnative::host_detail {

// Invocation-local diagnostic values. Nothing here executes or rewrites the
// runtime lookup, iteration, allocation, string operation or callback. The
// owner commits globals together with its Map state only on normal return.
struct providerDiagnosticState {
    prefixAnalysis & prefix;
    providerState & state;
    HostPrefixProviderSummary & proof;
    llvm::StringMap<prefixValue> globals;
    const std::vector<prefixObject> * objects = nullptr;

    providerDiagnosticState(prefixAnalysis & prefix, providerState & state,
                            HostPrefixProviderSummary & proof)
        : prefix(prefix), state(state), proof(proof) {}

    bool initialize();
    prefixValue operation(mlir::Operation * operation, prefixAnalysis::environment & values);
    void mutated() { ++mutation; }

private:
    struct iterator {
        unsigned map;
        unsigned mutation;
        ctjs::CallOp consumer;
        bool consumed = false;
    };
    struct snapshot {
        unsigned map;
        llvm::SmallVector<providerValue, 0> keys;
    };

    unsigned mutation = 0;
    llvm::SmallVector<iterator, 0> iterators;
    llvm::SmallVector<snapshot, 0> snapshots;

    bool record(mlir::Operation * operation, unsigned map, llvm::StringRef member,
                prefixValue result);
    prefixValue global(ctjs::LoadGlobalOp load);
    prefixValue property(ctjs::GetPropertyOp read, prefixAnalysis::environment & values);
    prefixValue call(ctjs::CallOp call, prefixAnalysis::environment & values);
    ctjs::CallOp consumer(ctjs::CallOp keys);
};

} // namespace ctcompile::ctnative::host_detail
