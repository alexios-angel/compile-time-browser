#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "ctcompile/CTNative/IR/CTNativeTypes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"

#include "ctcompile/CTNative/IR/CTNativeOpsDialect.cpp.inc"

namespace ctcompile::ctnative {

void CTNativeDialect::initialize() {
    // NO addOperations<>, because Phase 53 is types only and GET_OP_LIST over
    // an operation-free .td expands to nothing - an `addOperations<>()` with an
    // empty list is not valid C++. The day the first operation lands, the call
    // and the include come back together, which is exactly how CTJSDialect.cpp
    // is written.
    registerTypes();
}

} // namespace ctcompile::ctnative
