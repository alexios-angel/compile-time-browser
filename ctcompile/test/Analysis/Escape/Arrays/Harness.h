//===- Harness.h - Array-contents test harness ------------------*- C++ -*-===//
//
// Row types, fixture helpers and the check* entry points shared by the
// array-contents escape-analysis tests in this directory and its subdirectories.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../Harness.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include <cassert>

namespace ctcompile::test::escape::arrays {

/// One computeArrayContents fixture and its expected result. Sites are named by
/// their storage_test_id. Each string field is the result rendered as text:
/// `arrays` is "a:[x]" per exit, exits joined by " | "; `reads` is "a[0]=x";
/// `exit` is "value -> {sites}" per exit. A nullptr field is not checked.
struct contents_row {
    /// Description printed when the row fails.
    const char * what;
    /// IR placed between kPrologue and the closing brace of @f.
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

/// One array-retention fixture: which Stored escapes the refinement of
/// computeVerdicts should turn into Confined.
struct retention_row {
    const char * what;
    std::string body;
    /// Comma-separated sites whose Stored verdict becomes Confined.
    const char * discharged = "";
    bool complete = true;
    bool withAnalysis = true;
    const char * confinedReceivers = nullptr;
    const char * confinedNested = "";
};

/// A contents row and the retained children its complete proof discharges.
struct discharge_row {
    contents_row contents;
    const char * discharged = "x";
};

/// `source` with its first `before` replaced by `after`, which must be there.
inline std::string replace(std::string source, const std::string & before,
                           const std::string & after) {
    const std::size_t position = source.find(before);
    assert(position != std::string::npos);
    source.replace(position, before.size(), after);
    return source;
}

/// Checks computeArrayContents on one fixture: its result matches `expected`,
/// every smaller work budget fails with no partial evidence, and running it
/// changes none of EscapeAnalysis's existing verdicts or lattices.
void checkArrayContents(mlir::ModuleOp module, const contents_row & expected);
void checkArrayContents(mlir::MLIRContext & context);
/// kPrologue + the row's body + "}\n", parsed.
mlir::OwningOpRef<mlir::ModuleOp> parseContents(mlir::MLIRContext & context,
                                                const contents_row & expected);
/// checkArrayContents, then checkArrayRetention expecting `discharged` only when
/// the row is complete. Returns the retention budget cutoffs.
std::size_t checkDischarge(mlir::ModuleOp module, const contents_row & expected,
                           const char * discharged);
/// Both, failing under `label` (and returning 0) when the fixture does not parse.
std::size_t runDischarge(mlir::MLIRContext & context, const contents_row & expected,
                         const char * discharged, const char * label);
/// Checks the array-retention refinement on one fixture against `expected`,
/// including every smaller budget; returns the retention work it took.
std::size_t checkArrayRetention(mlir::ModuleOp module, const retention_row & expected);
void checkArrayRetention(mlir::MLIRContext & context);
void checkLiteralBigIntIndices(mlir::MLIRContext & context);
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
void checkDenseArrayLength(mlir::MLIRContext & context);
void checkArrayInduction(mlir::MLIRContext & context);
void checkStructuredContents(mlir::MLIRContext & context);

} // namespace ctcompile::test::escape::arrays
