//===- Passes.td - CTJS passes --------------------------------------------===//
//
// EMPTY AND REAL, like the enums and attributes. -gen-pass-decls -name CTJS
// over a pass-less file still emits a valid registerCTJSPasses(), which is what
// ctjs-opt calls - so the driver the policy asks for in Phase 7 can be built
// before any pass exists.
//
// "Never hand-write registerCTJSPasses(), an options struct, or a
// PassRegistration<> global. -gen-pass-decls -name CTJS produces all of it."
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTJS_PASSES
#define CTCOMPILE_CTJS_PASSES

include "mlir/Pass/PassBase.td"

#endif // CTCOMPILE_CTJS_PASSES
