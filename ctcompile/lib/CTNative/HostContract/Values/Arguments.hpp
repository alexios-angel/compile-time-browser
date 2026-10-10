//===- Arguments.hpp - Explicit call arguments by entry index ---*- C++ -*-===//
//
// Maps a callee entry-block argument index to the call operand that supplies
// it, for the host-contract value analyses.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Analysis.h"

namespace ctcompile::ctnative::host_detail {

/// The JS argument of call `operation` at `index`, where indices 0-2 are the
/// implicit arguments (receiver, new.target, callee) and 3 is the first
/// declared one. Null for an implicit slot, a non-call operation, or an index
/// past the end.
inline mlir::Value explicitArgument(mlir::Operation * operation, unsigned index) {
    auto js = jsCallOf(operation);
    if (index < ctjs::implicit_arguments || !js) { return {}; }
    index -= ctjs::implicit_arguments;
    return index < js->args.size() ? js->args[index] : mlir::Value{};
}

} // namespace ctcompile::ctnative::host_detail
