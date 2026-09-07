#pragma once

#include "Prefix.h"

namespace ctcompile::ctnative::host_detail {

// Interpret one actual source callback on a disposable provider transaction.
// Unknown completion discards the entire enclosing transaction, including any
// tentative scalar writes. Neither this helper nor its report rewrites IR.
prefixValue providerCallback(prefixAnalysis & prefix, ctjs::CallOp sourceCall, prefixValue callee,
                             prefixValue receiver, llvm::ArrayRef<prefixValue> arguments,
                             llvm::StringMap<prefixValue> & globals,
                             HostPrefixProviderSummary & proof);

} // namespace ctcompile::ctnative::host_detail
