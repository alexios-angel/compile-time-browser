//===- Callbacks.h - Provider callback interpretation -----------*- C++ -*-===//
//
// Declares providerCallback, the interpreter for scalar-only callbacks called
// from inside a provider method.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Prefix/Analysis.h"

namespace ctcompile::ctnative::host_detail {

/// Symbolically runs one callback that a provider method invokes from
/// `sourceCall`. The callback may only compute with primitives and write
/// primitive globals; its writes go to `globals`, which belongs to the
/// enclosing provider-call transaction. An unknown result discards that whole
/// transaction, including those writes. Records the callback in `proof` and
/// returns its result; nothing rewrites IR.
prefixValue providerCallback(prefixAnalysis & prefix, ctjs::CallOp sourceCall, prefixValue callee,
                             prefixValue receiver, llvm::ArrayRef<prefixValue> arguments,
                             llvm::StringMap<prefixValue> & globals,
                             HostPrefixProviderSummary & proof);

} // namespace ctcompile::ctnative::host_detail
