//===- Helpers.hpp - Leaf object checks -------------------------*- C++ -*-===//
//
// Declarations of the leaf-object ownership checks defined in Children.cpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../Tests.h"

namespace ctcompile::test::owned_global_shared_map {

/// Checks that OwnedGlobalRoots proves a captured outer Map owns the fresh
/// child Map stored into it and the child's saved readback, in four variants
/// (seeded or cleared, unconditional or guarded by `has`), and that budget
/// cuts, forged `ctnative.*` attributes and IR mutations withdraw the proof.
/// \p lifted selects the prepared (lifted) form of \p source.
void checkChildMapOwner(mlir::MLIRContext & context, const std::string & source, bool lifted);
/// Checks ownership when the Map value is an object the caller creates
/// (`{value: 64}`) and passes to `put`, rather than one `put` allocates.
void checkCallerPayloadOwner(mlir::MLIRContext & context, const std::string & source,
                             bool prepared);

} // namespace ctcompile::test::owned_global_shared_map
