//===- Cases.hpp - Structured array-contents cases --------------*- C++ -*-===//
//
// Declares StructuredCases, which collects the array-contents rows for
// structured (scf.if and scf.while) control flow across several .cpp files.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Harness.h"
#include <utility>

namespace ctcompile::test::escape::arrays::structured_detail {

/// Accumulates array-contents rows for structured control flow. setup()
/// builds the shared loop fixtures (the std::string members, edited by the
/// later case groups with replace()), each case group appends rows, and
/// finish() parses and checks every row.
struct StructuredCases {
    mlir::MLIRContext & context;
    explicit StructuredCases(mlir::MLIRContext & input) : context(input) {}
    std::vector<contents_row> rows{};
    std::string prefix{};
    std::string original{};
    std::string reversed{};
    std::string negated{};
    std::string negatedReversed{};
    std::string computedStart{};
    std::string alternateStart{};
    std::string makeUnit{};
    std::string computedUnit{};
    std::string savedUnit{};
    std::string carriedUnit{};
    std::string alternateUnit{};
    std::string makeStride{};
    std::string carriedStride{};
    std::string overshoot{};
    std::string maxStride{};
    std::string reloaded{};
    std::string makeNegative{};
    std::string carriedNegative{};
    std::string savedNegative{};
    /// Total analysis work over all rows, summed and printed by finish().
    std::size_t budgets{};

    /// Adds a row whose fixture the analysis must refuse with failure.
    void reject(const char * what, std::string body,
                ArrayContentsFailure failure = ArrayContentsFailure::UnsupportedControlFlow);

    void setup();
    void reloads();
    void invariantArithmetic();
    void signedArithmetic();
    void powersAndProducts();
    void mutationRefusals();
    void finish();
};

} // namespace ctcompile::test::escape::arrays::structured_detail
