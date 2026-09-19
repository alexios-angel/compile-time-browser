#pragma once

#include "../Tests.h"

namespace ctcompile::test::owned_global_shared_map {

void checkChildMapOwner(mlir::MLIRContext & context, const std::string & source, bool lifted);
void checkCallerPayloadOwner(mlir::MLIRContext & context, const std::string & source,
                             bool prepared);

} // namespace ctcompile::test::owned_global_shared_map
