#include "ctcompile/CTNative/IR/CTNativeInterfaces.h"

// THE INTERFACE'S GENERATED DEFINITIONS. -gen-op-interface-defs emits the
// Concept/Model glue and the out-of-line dispatch; the declarations alone link
// right up until something CALLS getStaticType, which is what Phase 54's
// inference and Phase 63's selection both do.
#include "ctcompile/CTNative/IR/CTNativeOpInterfaces.cpp.inc"
