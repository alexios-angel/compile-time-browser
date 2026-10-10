//===- Helpers.hpp - Leaf readback test helpers -----------------*- C++ -*-===//
//
// Declarations shared between LeafReadbacks.cpp and LeafReadbacks/Mixed.cpp.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Tests.h"

namespace ctcompile::test::owned_global_shared_map {

/// Checks that exact Number/Null results are proved per getter invocation:
/// a later store, a mutation, an incomplete budget or a forged `ctnative.*`
/// attribute must not let one invocation borrow another's evidence.
void checkExactScalarReturns(mlir::MLIRContext & context, const std::string & distinct,
                             bool prepared);
/// Checks a Map of child Maps seeded with a caller payload and read back
/// through `has`/`get` on both levels, where one getter invocation yields a
/// Number and another the caller's object.
void checkMixedChildReadbacks(mlir::MLIRContext & context, const std::string & source,
                              bool prepared);

} // namespace ctcompile::test::owned_global_shared_map
