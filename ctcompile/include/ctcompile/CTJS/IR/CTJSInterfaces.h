//===- CTJSInterfaces.h - CTJS operation interfaces -------------*- C++ -*-===//
//
// Operation interfaces; see CTJSInterfaces.td.
//
// RuntimeCallOpInterface returns a ctbrowser::aot::helper_id, which is
// generated from aot_helpers.def, so this header is where the dialect and the
// ABI meet. EscapeEffectOpInterface gives each operation a per-operand escape
// table for escape analysis; its effect and route classes come from
// CTJSEscapeEffects.h and must be complete before the generated interface,
// whose inline helpers use isa<> on them.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <ctbrowser/aot/aot.hpp>

#include "ctcompile/CTJS/IR/CTJSEscapeEffects.h"

#include "mlir/IR/OpDefinition.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "ctcompile/CTJS/IR/CTJSOpInterfaces.h.inc"
