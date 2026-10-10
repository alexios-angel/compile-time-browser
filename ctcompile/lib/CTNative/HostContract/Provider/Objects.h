//===- Objects.h - Objects seen by provider methods -------------*- C++ -*-===//
//
// Declares providerObjectState, which lets provider methods read and write
// primitive fields of objects the entry function created, and the helpers
// that track which objects have been published to the host.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Prefix/Analysis.h"

namespace ctcompile::ctnative::host_detail {

/// A throwaway copy of the prefix's object fields for one provider-method
/// call. Object ids still name the original allocations: copying this state
/// neither copies nor creates a runtime object.
struct providerObjectState {
    prefixAnalysis & prefix;
    HostPrefixProviderSummary & proof;
    std::vector<prefixObject> objects;

    /// Copies the prefix's objects, charging for every field; false when out
    /// of budget.
    bool initialize();
    /// True when object `id` may be handled by a provider method: created by
    /// the entry function, never published, and holding only primitive
    /// fields with ordinary keys.
    bool accepts(unsigned id);
    /// Marks an accepted object as retained by a Map, recording `operation`.
    bool retain(unsigned id, mlir::Operation * operation);
    /// Interprets a property read, write or delete on an accepted object;
    /// unknown for anything else.
    prefixValue operation(mlir::Operation * operation, prefixAnalysis::environment & values);

private:
    bool record(mlir::Operation * operation, unsigned id, llvm::StringRef member,
                llvm::StringRef action, mlir::Attribute result);
};

/// True when writing a field of `owner` publishes the value to the host: the
/// owner is the realm, an already published object, a factory's method table
/// or the value of a contract root binding. A global that only aliases an
/// object is ordinary prefix state, not a publication. Answers true when the
/// budget runs out.
bool providerPublicationOwner(prefixAnalysis & prefix, prefixValue owner);
/// Marks `value` and every object reachable through its fields as published.
/// False when one of them is retained by a provider Map or the budget runs out.
bool providerPublishObject(prefixAnalysis & prefix, prefixValue value);

} // namespace ctcompile::ctnative::host_detail
