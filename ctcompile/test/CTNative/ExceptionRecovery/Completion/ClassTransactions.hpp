#pragma once

//===- ClassTransactions.hpp - Class/DOM preparation rollback tests -------===//
//
// Declares the class/DOM preparation transaction test.
//
//===----------------------------------------------------------------------===//

#include "../Tests.h"

namespace ctcompile::test::exception_recovery {

/// Runs the class/DOM preparation tests. Returns false if a fixture fails to
/// import.
bool testClassTransactions(mlir::MLIRContext & context);

} // namespace ctcompile::test::exception_recovery
