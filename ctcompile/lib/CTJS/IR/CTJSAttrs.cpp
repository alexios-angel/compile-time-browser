#include "ctcompile/CTJS/IR/CTJSAttrs.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"

#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

// THE GENERATED ATTRIBUTE STORAGE, AND THE DIALECT HOOKS THAT COME WITH IT.
//
// There is not one attribute yet, so this looks like a file about nothing - and
// it is not: `useDefaultAttributePrinterParser = 1` in the mandated CTJSBase.td
// makes the generated dialect DECLARE parseAttribute and printAttribute, and
// their definitions are emitted by -gen-attrdef-defs. Without a translation
// unit that expands them the dialect fails to link, which is what happened.
//
// CTJSTypes.cpp is the same shape for the same reason, and the types linked
// only because it already existed.
#define GET_ATTRDEF_CLASSES
#include "ctcompile/CTJS/IR/CTJSAttrs.cpp.inc"
