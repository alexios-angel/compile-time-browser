//===- ctcompile/lib/CTJS/IR/CTJSDialect.cpp - ctjs dialect registration --===//
//
// Registers the ctjs dialect's operations, types and attributes, and its
// constant materializer hook.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTJS/IR/CTJSDialect.h"

#include "ctcompile/CTJS/IR/CTJSAttrs.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/IR/CTJSTypes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"

// The three dependentDialects are named by their C++ types in the generated
// source, so this translation unit needs their definitions. Declaring a dialect as dependent is
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

// CTJSBase.td sets hasConstantMaterializer, but this dialect does not yet
// rebuild folded attributes as ctjs.constant operations. Returning nullptr
// tells MLIR "cannot materialize this value", so a fold that would need a new
// constant is simply not applied.
mlir::Operation * CTJSDialect::materializeConstant(mlir::OpBuilder & builder, mlir::Attribute value,
                                                   mlir::Type type, mlir::Location location) {
    (void)builder;
    (void)value;
    (void)type;
    (void)location;
    return nullptr;
}

} // namespace ctcompile::ctjs
