//===- MLIRContextSetup.hpp - Dialect registration --------------*- C++ -*-===//
//
// The dialects this compiler registers, in one place.
//
// Deliberately not mlir::registerAllDialects. Registering everything MLIR ships
// makes ctjs-opt accept IR the pipeline can never lower, so a test can pass on
// a file the compiler would refuse, and it links dialects nothing here uses.
// The list is exactly the pipeline's; add a dialect when a pass starts using it.
//
//===----------------------------------------------------------------------===//
#pragma once
namespace mlir {
class DialectRegistry;
} // namespace mlir

namespace ctcompile {

/// Registers every dialect the ctcompile pipeline uses. Deliberately not named
/// `registerAllDialects`, which a reader would take to mean MLIR's function
/// that registers everything.
void registerCTCompileDialects(mlir::DialectRegistry & registry);

} // namespace ctcompile
