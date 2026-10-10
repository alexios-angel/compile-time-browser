//===- Diagnostics.h - Diagnostic operations in provider methods ----------===//
//
// Declares providerDiagnosticState, the part of the provider-method
// interpreter that handles globals, key snapshots, strings and callbacks.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Prefix/Analysis.h"

namespace ctcompile::ctnative::host_detail {

/// Models the extra operations a provider method may use while building a
/// diagnostic: global reads, own-field reads, `Array.from(map.keys())`
/// snapshots and their indexing, string concatenation, and scalar callbacks.
/// The state is local to one invocation and nothing here runs or rewrites the
/// real operations. The owning reader commits `globals` together with its Map
/// state only when the method returns normally.
struct providerDiagnosticState {
    prefixAnalysis & prefix;
    providerState & state;
    HostPrefixProviderSummary & proof;
    llvm::StringMap<prefixValue> globals;
    const std::vector<prefixObject> * objects = nullptr;

    providerDiagnosticState(prefixAnalysis & prefix, providerState & state,
                            HostPrefixProviderSummary & proof)
        : prefix(prefix), state(state), proof(proof) {}

    /// Copies the prefix's globals into `globals`; false when out of budget.
    bool initialize();
    /// Interprets `operation`; unknown when it is not one of the modeled forms.
    prefixValue operation(mlir::Operation * operation, prefixAnalysis::environment & values);
    /// Notes a mutation, which invalidates every open keys() iterator.
    void mutated() { ++mutation; }
    /// Records a Map operation's normal result, with the Map or object it
    /// returns, in `proof`.
    bool record(mlir::Operation * operation, unsigned map, llvm::StringRef member,
                prefixValue result);

private:
    /// A `map.keys()` iterator, valid only while `mutation` still matches and
    /// only for its one `Array.from` consumer.
    struct iterator {
        unsigned map;
        unsigned mutation;
        ctjs::CallOp consumer;
        bool consumed = false;
    };
    /// The key array `Array.from(map.keys())` produced.
    struct snapshot {
        unsigned map;
        llvm::SmallVector<providerValue, 0> keys;
    };

    unsigned mutation = 0;
    llvm::SmallVector<iterator, 0> iterators;
    llvm::SmallVector<snapshot, 0> snapshots;

    prefixValue global(ctjs::LoadGlobalOp load);
    prefixValue property(ctjs::GetPropertyOp read, prefixAnalysis::environment & values);
    prefixValue call(ctjs::CallOp call, prefixAnalysis::environment & values);
    /// The single `Array.from(...)` call in the same block that consumes the
    /// result of `keys`, or null when it has any other use.
    ctjs::CallOp consumer(ctjs::CallOp keys);
};

} // namespace ctcompile::ctnative::host_detail
