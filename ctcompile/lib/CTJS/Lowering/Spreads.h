//===- Spreads.h - Expand object spreads of known shape ---------*- C++ -*-===//
//
// Rewrites `{...source}` (ctjs.copy_props) into one read and one own-property
// definition per key when the source's own keys at the spread are known.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "mlir/Interfaces/FunctionInterfaces.h"

namespace ctcompile::ctjs {

/// Expands every ctjs.copy_props in \p body whose source's own keys are known
/// at the copy, and returns how many it expanded or erased.
///
/// CopyDataProperties (ECMA-262 7.3.25) reads each own enumerable key of the
/// source in property order and defines it on the target. A copy is expanded
/// when its target is a fresh object literal and its source is either null or
/// undefined (which copy nothing) or an object literal made in the same block
/// whose only uses before the copy are constant-key reads and data-property
/// writes in that block. The keys are then the written keys, in first-write
/// order, and each becomes a `get_property` from the source and a
/// `set_property` marked ctjs.define_own on the target. Any other copy is left
/// as it is.
unsigned expandKnownSpreads(mlir::FunctionOpInterface body);

} // namespace ctcompile::ctjs
