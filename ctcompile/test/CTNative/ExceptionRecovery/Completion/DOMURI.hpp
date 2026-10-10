#pragma once

//===- DOMURI.hpp - DOM URI normalization transaction test ------*- C++ -*-===//
//
// Declares the DOM URI normalization test defined in DOMURI.cpp.
//
//===----------------------------------------------------------------------===//

namespace mlir {
class MLIRContext;
}

namespace ctcompile::test::exception_recovery {
/// Checks DOM URI normalization: successful proofs, exact budgets, and
/// refusals that leave the module unchanged.
void testDOMURITransaction(mlir::MLIRContext & context);
} // namespace ctcompile::test::exception_recovery
