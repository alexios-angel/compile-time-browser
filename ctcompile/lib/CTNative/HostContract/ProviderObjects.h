#pragma once

#include "Prefix.h"

namespace ctcompile::ctnative::host_detail {

// Disposable current-field heap. Tokens still name the original allocation;
// copying this analysis state neither copies nor creates a runtime object.
struct providerObjectState {
    prefixAnalysis & prefix;
    HostPrefixProviderSummary & proof;
    std::vector<prefixObject> objects;

    bool initialize();
    bool accepts(unsigned id);
    bool retain(unsigned id, mlir::Operation * operation);
    prefixValue operation(mlir::Operation * operation, prefixAnalysis::environment & values);

private:
    bool record(mlir::Operation * operation, unsigned id, llvm::StringRef member,
                llvm::StringRef action, mlir::Attribute result);
};

// A source global alias is known prefix state. A declared host root or a
// published object's fields are external publication boundaries instead.
bool providerPublicationOwner(prefixAnalysis & prefix, prefixValue owner);
bool providerPublishObject(prefixAnalysis & prefix, prefixValue value);

} // namespace ctcompile::ctnative::host_detail
