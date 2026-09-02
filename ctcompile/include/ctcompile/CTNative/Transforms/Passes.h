#ifndef CTCOMPILE_CTNATIVE_TRANSFORMS_PASSES_H
#define CTCOMPILE_CTNATIVE_TRANSFORMS_PASSES_H

// The generated declarations name every dependent dialect by type, so this
// header has to include them.
#include "ctcompile/CTNative/IR/CTNativeDialect.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Pass/Pass.h"

namespace ctcompile::ctnative {

#define GEN_PASS_DECL
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

#define GEN_PASS_REGISTRATION
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

} // namespace ctcompile::ctnative

#endif
