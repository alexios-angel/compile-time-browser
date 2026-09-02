#pragma once
// Operation interfaces; see CTJSInterfaces.td.
// RuntimeCallOpInterface returns a ctbrowser::aot::helper_id, which is
// generated from aot_helpers.def - so this header is where the dialect and the
// ABI meet. EscapeEffectOpInterface is Phase 55's per-operand escape table;
// its effect and route classes are CTJSEscapeEffects.h's and must be complete
// before the generated interface, whose inline helpers isa<> over them.
#include <ctbrowser/aot/aot.hpp>

#include "ctcompile/CTJS/IR/CTJSEscapeEffects.h"

#include "mlir/IR/OpDefinition.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "ctcompile/CTJS/IR/CTJSOpInterfaces.h.inc"
