#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/OpImplementation.h"

// Verifiers, folders and canonicalizer hooks only - never a parser, a printer,
// a builder or an accessor, all of which TableGen produces. There are no
// operations yet; Phase 8 is where this file starts having contents.
#define GET_OP_CLASSES
#include "ctcompile/CTJS/IR/CTJSOps.cpp.inc"
