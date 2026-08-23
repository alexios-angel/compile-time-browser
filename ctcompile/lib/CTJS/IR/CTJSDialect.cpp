#include "ctcompile/CTJS/IR/CTJSDialect.h"

#include "ctcompile/CTJS/IR/CTJSAttrs.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/IR/CTJSTypes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"

// THE THREE dependentDialects NAME THEMSELVES IN THE GENERATED SOURCE, so this
// translation unit needs their definitions. Declaring a dialect as dependent is
// a promise that loading CTJS loads them too, and the generated
// getDependentDialects() spells each one by its C++ name.
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"

#include "ctcompile/CTJS/IR/CTJSOpsDialect.cpp.inc"

namespace ctcompile::ctjs {

void CTJSDialect::initialize() {
    addOperations<
#define GET_OP_LIST
#include "ctcompile/CTJS/IR/CTJSOps.cpp.inc"
        >();
    registerTypes();
    registerAttributes();
}

// THE POLICY SETS hasConstantMaterializer AND THERE IS NOTHING TO MATERIALIZE.
//
// The flag is in the mandated CTJSBase.td, so it is not dropped; the obligation
// it creates is answered honestly instead. `ctjs.constant` arrives in Phase 8,
// and until it does, folding cannot produce a constant this dialect could
// rebuild - so returning nullptr is the true answer rather than a placeholder.
// MLIR treats nullptr as "I cannot materialize that", which is exactly the
// case.
mlir::Operation * CTJSDialect::materializeConstant(mlir::OpBuilder & builder, mlir::Attribute value,
                                                   mlir::Type type, mlir::Location location) {
    (void)builder;
    (void)value;
    (void)type;
    (void)location;
    return nullptr;
}

} // namespace ctcompile::ctjs
