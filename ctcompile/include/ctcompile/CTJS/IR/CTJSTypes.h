//===- CTJSTypes.h - The CTJS types -----------------------------*- C++ -*-===//
//
// The five CTJS types (value, context, program, closure, coroutine).
//
// The generated file is CTJSOpsTypes.h.inc, not CTJSTypes.h.inc:
// add_mlir_dialect(CTJSOps ctjs) names its outputs after the .td file it was
// given, and the typedef backends run over CTJSOps.td.
//
//===----------------------------------------------------------------------===//

#pragma once
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"

#define GET_TYPEDEF_CLASSES
#include "ctcompile/CTJS/IR/CTJSOpsTypes.h.inc"
