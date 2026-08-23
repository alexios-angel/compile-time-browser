#include "ctcompile/CTJS/IR/CTJSTypes.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"

#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

// The generated storage, parsers and printers for the five types. Nothing
// hand-written: the dialect sets useDefaultTypePrinterParser, and none of the
// five carries a parameter.
#define GET_TYPEDEF_CLASSES
#include "ctcompile/CTJS/IR/CTJSOpsTypes.cpp.inc"
