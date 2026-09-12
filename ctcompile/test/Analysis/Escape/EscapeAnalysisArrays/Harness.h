#pragma once

#include "../Harness.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

namespace ctcompile::test::escape::arrays {

struct contents_row {
    const char * what;
    std::string body;
    ArrayContentsFailure failure = ArrayContentsFailure::None;
    const char * arrays = "";
    const char * reads = "";
    const char * exit = "";
    const char * writes = nullptr;
    const char * objects = nullptr;
    const char * propertyReads = nullptr;
    const char * propertyWrites = nullptr;
    const char * propertyDeletions = nullptr;
    const char * propertyCopies = nullptr;
};

struct retention_row {
    const char * what;
    std::string body;
    const char * discharged = "";
    bool complete = true;
    bool withAnalysis = true;
};

void checkArrayContents(mlir::ModuleOp module, const contents_row & expected);
void checkArrayContents(mlir::MLIRContext & context);
std::size_t checkArrayRetention(mlir::ModuleOp module, const retention_row & expected);
void checkArrayRetention(mlir::MLIRContext & context);
void checkObjectContents(mlir::MLIRContext & context);
void checkObjectDeletions(mlir::MLIRContext & context);
void checkObjectCopies(mlir::MLIRContext & context);
void checkOpaqueEntryTransport(mlir::MLIRContext & context);
void checkSelectorProducers(mlir::MLIRContext & context);
void checkLogicalNegation(mlir::MLIRContext & context);
void checkTotalUnaryProducers(mlir::MLIRContext & context);
void checkStaticBinaryProducers(mlir::MLIRContext & context);
void checkArithmeticUnaryProducers(mlir::MLIRContext & context);
void checkBigIntPlusErrors(mlir::MLIRContext & context);
void checkBigIntMixedSubErrors(mlir::MLIRContext & context);
void checkBigIntMixedArithmeticErrors(mlir::MLIRContext & context, ctjs::BinaryKind operation);
void checkBigIntUnaryProducers(mlir::MLIRContext & context);
void checkBigIntBinaryProducers(mlir::MLIRContext & context);
void checkStringBigIntConcatenation(mlir::MLIRContext & context);
void checkBigIntComparison(mlir::MLIRContext & context, ctjs::CompareKind producerKind);
void checkArrayFrames(mlir::MLIRContext & context);
void checkArrayConditionals(mlir::MLIRContext & context);
void checkContainerSwitches(mlir::MLIRContext & context);

} // namespace ctcompile::test::escape::arrays
