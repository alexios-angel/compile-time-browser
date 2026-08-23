#include "ctcompile/CTJS/IR/CTJSInterfaces.h"

// THE INTERFACE'S GENERATED DEFINITIONS. -gen-op-interface-defs emits the
// Concept/Model glue and the out-of-line method that dispatches through it; the
// declarations alone link right up until something CALLS getHelperID, which is
// exactly what Phase 10's one conversion pattern does.
#include "ctcompile/CTJS/IR/CTJSOpInterfaces.cpp.inc"
