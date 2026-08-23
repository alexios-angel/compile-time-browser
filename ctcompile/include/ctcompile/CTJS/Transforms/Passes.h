#pragma once
// CTJS passes. None yet; the declarations and the registration function are
// generated from Passes.td.
#include "mlir/Pass/Pass.h"

namespace ctcompile::ctjs {

#define GEN_PASS_DECL
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

#define GEN_PASS_REGISTRATION
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

} // namespace ctcompile::ctjs
