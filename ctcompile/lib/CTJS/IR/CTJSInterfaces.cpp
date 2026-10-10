//===- CTJSInterfaces.cpp - ctjs interface definitions --------------------===//
//
// Expands the generated interface definitions: -gen-op-interface-defs emits the
// Concept/Model glue and the out-of-line dispatch methods. Without this file
// the build still links until something calls an interface method such as
// getHelperID (the EmitC lowering does).
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTJS/IR/CTJSInterfaces.h"

#include "ctcompile/CTJS/IR/CTJSOpInterfaces.cpp.inc"
