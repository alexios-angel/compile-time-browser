#pragma once

#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#define GET_OP_CLASSES
#include "ctcompile/CTNative/IR/CTNativeOps.h.inc"
