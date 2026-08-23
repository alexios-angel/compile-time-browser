#pragma once
// The five CTJS types.
//
// CTJSOpsTypes.h.inc, NOT CTJSTypes.h.inc: add_mlir_dialect(CTJSOps ctjs) names
// its outputs after the .td file it was given, and the typedef backends run
// over CTJSOps.td. The file layout suggests otherwise; the CMake is what
// decides.
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"

#define GET_TYPEDEF_CLASSES
#include "ctcompile/CTJS/IR/CTJSOpsTypes.h.inc"
