//===- CTJSOps.td - every CTJS operation ----------------------------------===//
//
// NO OPERATIONS YET, and that is Phase 7's whole shape: the policy says "Build
// ctjs-opt in Phase 7, before defining a single operation", because "a dialect
// defined before its test driver exists cannot be validated, and the first
// fifty operations will then be written without feedback". Phase 8 fills this
// file.
//
// IT INCLUDES THE TYPES, and that is load-bearing rather than tidy:
// add_mlir_dialect(CTJSOps ctjs) is the only TableGen invocation the policy's
// CMakeLists makes for typedefs, and it runs over THIS file. A CTJSTypes.td
// that nothing includes generates nothing at all.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTJS_OPS
#define CTCOMPILE_CTJS_OPS

include "ctcompile/CTJS/IR/CTJSBase.td"
include "ctcompile/CTJS/IR/CTJSTypes.td"
include "ctcompile/CTJS/IR/CTJSAttrs.td"
include "ctcompile/CTJS/IR/CTJSEnums.td"
include "ctcompile/CTJS/IR/CTJSInterfaces.td"

#endif // CTCOMPILE_CTJS_OPS
