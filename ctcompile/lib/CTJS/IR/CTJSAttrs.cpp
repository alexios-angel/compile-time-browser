//===- ctcompile/lib/CTJS/IR/CTJSAttrs.cpp - ctjs attribute definitions ---===//
//
// The generated attribute storage and its dialect hooks.
//
// CTJSBase.td sets `useDefaultAttributePrinterParser = 1`, so the generated
// dialect declares parseAttribute and printAttribute; -gen-attrdef-defs emits
// their bodies, and this file expands them so the dialect links. CTJSTypes.cpp
// does the same for types.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTJS/IR/CTJSAttrs.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSEnums.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

// The enum definitions (the stringification the attributes parse and print
// through) come before the attribute storage that uses them.
#include "ctcompile/CTJS/IR/CTJSEnums.cpp.inc"

#define GET_ATTRDEF_CLASSES
#include "ctcompile/CTJS/IR/CTJSAttrs.cpp.inc"

namespace ctcompile::ctjs {

// Here rather than in CTJSDialect.cpp because addAttributes<> needs the storage
// classes above to be complete (registerTypes is in CTJSTypes.cpp likewise).
void CTJSDialect::registerAttributes() {
    addAttributes<
#define GET_ATTRDEF_LIST
#include "ctcompile/CTJS/IR/CTJSAttrs.cpp.inc"
        >();
}

} // namespace ctcompile::ctjs
