#pragma once
// Every CTJS operation. None yet - Phase 8 defines them.
#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Interfaces/InferTypeOpInterface.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "ctcompile/CTJS/IR/CTJSAttrs.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSEnums.h"
#include "ctcompile/CTJS/IR/CTJSInterfaces.h"
#include "ctcompile/CTJS/IR/CTJSTraits.h"
#include "ctcompile/CTJS/IR/CTJSTypes.h"

#define GET_OP_CLASSES
#include "ctcompile/CTJS/IR/CTJSOps.h.inc"
