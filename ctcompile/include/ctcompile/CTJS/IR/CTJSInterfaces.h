#pragma once
// Operation interfaces. Empty until Phase 8; see CTJSInterfaces.td.
// The interface returns a ctbrowser::aot::helper_id, which is generated from
// aot_helpers.def - so this header is where the dialect and the ABI meet.
#include <ctbrowser/aot/aot.hpp>

#include "mlir/IR/OpDefinition.h"

#include "ctcompile/CTJS/IR/CTJSOpInterfaces.h.inc"
