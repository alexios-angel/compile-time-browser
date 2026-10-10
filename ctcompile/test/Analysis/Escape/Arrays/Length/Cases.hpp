//===- Cases.hpp - Dense array length test fixture --------------*- C++ -*-===//
//
// LengthCases holds the IR fragments and counters shared by the dense array
// `length` tests. Each member function in Setup.cpp, IndexSnapshots.cpp, ...,
// Finish.cpp adds one group of rows; later groups reuse fragments and rows
// that earlier groups stored here, so Cases.cpp calls them in a fixed order.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Harness.h"
#include <utility>

namespace ctcompile::test::escape::arrays::length_detail {

/// Shared state for the dense array `length` tests. String members are ctjs IR
/// fragments that rows concatenate into a function body; contents_row members
/// are whole rows that LiveMutations.cpp and ShrinkRetention.cpp mutate again.
struct LengthCases {
    mlir::MLIRContext & context;
    explicit LengthCases(mlir::MLIRContext & input) : context(input) {}
    /// Constants %zero and %key ("length"), object %x and array %a = [%x].
    std::string values{};
    /// `%length = a.length`.
    std::string read{};
    /// `a[0] = 0`, which overwrites the only element.
    std::string overwrite{};
    /// `return %length`.
    std::string done{};
    /// Number of rows run so far.
    unsigned rows{};
    /// Sum of the retention budget cutoffs that checkDischarge reported.
    std::size_t budgets{};
    /// Like `values` but with an empty %a and no %x.
    std::string privateValues{};
    /// A row that sends %length and %key down two branches, each ending in its
    /// own frame exit; IndexSnapshots.cpp and ShrinkRetention.cpp derive variants.
    contents_row branch{};
    /// `%one = 1.0`.
    std::string one{};
    /// `%index = %length - %one`.
    std::string subtract{};
    /// `a[%index] = 0; return %a`.
    std::string indexed{};
    /// The `a[length - 1] = 0` row that LiveMutations.cpp edits in place.
    contents_row originalIndex{};
    /// `a.length = 0`.
    std::string shrink{};
    /// Rows that write a computed new length into `a.length`; LiveMutations.cpp
    /// re-runs each of them after editing its IR.
    contents_row originalShrink{};
    contents_row computedShrink{};
    contents_row literalShrink{};
    contents_row productShrink{};
    contents_row quotientShrink{};
    contents_row remainderShrink{};
    contents_row shiftShrink{};
    contents_row signedShiftShrink{};
    contents_row leftShiftShrink{};
    contents_row maskShrink{};
    contents_row orShrink{};
    contents_row xorShrink{};
    contents_row heldOffsetShrink{};
    contents_row unaryShrink{};
    contents_row negatedShrink{};
    /// The row LiveMutations.cpp edits to check that verdicts follow live IR.
    contents_row mutation{};
    /// Number of live IR edits checked by LiveMutations.cpp.
    unsigned liveStates{};

    /// Runs checkDischarge on `module` and adds its budget cutoffs to `budgets`.
    void check(mlir::ModuleOp module, const contents_row & expected, const char * discharged);
    /// Parses `expected.body` inside the standard test prologue.
    mlir::OwningOpRef<mlir::ModuleOp> parse(const contents_row & expected);
    /// Parses and checks one row; `discharged` lists the children a complete
    /// proof may release. A non-null `receiverVerdict` also checks the verdict
    /// of array `a` (see Cases.cpp).
    void run(const contents_row & expected, const char * discharged = "x",
             const char * receiverVerdict = nullptr);

    void setup();
    void indexSnapshots();
    void arithmetic();
    void shrinkArithmetic();
    void shrinkRetention();
    void liveMutations();
    void finish();
};

} // namespace ctcompile::test::escape::arrays::length_detail
