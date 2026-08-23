#pragma once
// WHICH DIALECTS THIS COMPILER REGISTERS, in one place.
//
// Deliberately NOT mlir::registerAllDialects. Registering everything MLIR ships
// makes ctjs-opt accept IR the pipeline can never lower, so a test can pass on
// a file the compiler would refuse - and it links dialects nothing here uses.
// The list is the pipeline's, and it grows when a phase needs it to.
namespace mlir {
class DialectRegistry;
} // namespace mlir

namespace ctcompile {

// NAMED FOR PHASE 7's DELIVERABLE 3 rather than the policy's illustrative
// `ctjs::registerAllDialects`, which collides confusingly with
// `mlir::registerAllDialects` - a reader seeing the latter spelling would
// reasonably assume it does what MLIR's does. The deviation is here rather
// than silent.
void registerCTCompileDialects(mlir::DialectRegistry & registry);

} // namespace ctcompile
