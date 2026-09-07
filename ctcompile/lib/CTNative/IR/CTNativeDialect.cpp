#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "ctcompile/CTNative/IR/CTNativeOps.h"
#include "ctcompile/CTNative/IR/CTNativeTypes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"

#include "ctcompile/CTNative/IR/CTNativeOpsDialect.cpp.inc"

namespace ctcompile::ctnative {

void CTNativeDialect::initialize() {
    addOperations<
#define GET_OP_LIST
#include "ctcompile/CTNative/IR/CTNativeOps.cpp.inc"
        >();
    registerTypes();
}

} // namespace ctcompile::ctnative
