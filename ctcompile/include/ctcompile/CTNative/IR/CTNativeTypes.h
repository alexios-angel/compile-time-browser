#pragma once
// The fifteen ctnative types.
//
// CTNativeOpsTypes.h.inc, NOT CTNativeTypes.h.inc: add_mlir_dialect(CTNativeOps
// ctnative) names its outputs after the .td file it was given, and the typedef
// backends run over CTNativeOps.td. The file layout suggests otherwise; the
// CMake is what decides. CTJSTypes.h carries the same note for the same reason.
#include "ctcompile/CTNative/IR/CTNativeEnums.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"

#define GET_TYPEDEF_CLASSES
#include "ctcompile/CTNative/IR/CTNativeOpsTypes.h.inc"
