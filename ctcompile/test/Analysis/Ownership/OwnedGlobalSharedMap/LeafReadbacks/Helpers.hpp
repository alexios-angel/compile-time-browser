#pragma once

#include "../Tests.h"

namespace ctcompile::test::owned_global_shared_map {

void checkExactScalarReturns(mlir::MLIRContext & context, const std::string & distinct,
                             bool prepared);
void checkMixedChildReadbacks(mlir::MLIRContext & context, const std::string & source,
                              bool prepared);

} // namespace ctcompile::test::owned_global_shared_map
