#pragma once

#include "../Tests.h"

namespace ctcompile::test::owned_global_shared_map {

void checkCapturedStringSnapshots(mlir::MLIRContext & context, const std::string & source,
                                  bool prepared);
void checkCapturedSnapshots(mlir::MLIRContext & context, const std::string & source, bool prepared);
void checkScalarCallbacks(mlir::MLIRContext & context, const std::string & source, bool prepared);

} // namespace ctcompile::test::owned_global_shared_map
