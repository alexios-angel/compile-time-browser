//===- Cases.hpp - Induction case table -------------------------*- C++ -*-===//
//
// State shared by the array-induction tests, which are split across one .cpp
// per group (Setup, OverwritesAndTransport, InvariantReads, ...).
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../Harness.h"
#include <utility>

namespace ctcompile::test::escape::arrays::induction_detail {

/// The array-induction test groups and the IR fragments they share. setup()
/// builds `prefix`, `loop` and `original` (the base counted loop over
/// [1, 2, 3]); the remaining string members are variants that one group builds
/// and a later group reuses, so the group methods must run in declaration
/// order, as checkArrayInduction does.
struct InductionCases {
    mlir::MLIRContext & context;
    explicit InductionCases(mlir::MLIRContext & input) : context(input) {}
    /// Constants and the array `a`.
    std::string prefix{};
    /// The counted loop that sums `a`, rereading `length` in its header.
    std::string loop{};
    /// prefix + loop.
    std::string original{};
    /// Rows run so far, printed by finish().
    unsigned rows{};
    /// Retention budget cutoffs checked so far, printed by finish().
    std::size_t budgets{};
    std::string savedChild{};
    std::string reversed{};
    std::string negated{};
    std::string negatedReversed{};
    std::string computed{};
    std::string computedChild{};
    std::string alternateStart{};
    std::string nonzeroChild{};
    std::string makeUnit{};
    std::string computedUnit{};
    std::string computedUnitChild{};
    std::string savedUnit{};
    std::string carriedUnit{};
    std::string alternateUnit{};
    std::string stride{};
    std::string carriedStride{};
    std::string computedStrideChild{};
    std::string maxStride{};
    std::string nonzeroStride{};
    std::string negativeLiteral{};
    std::string subtract{};
    std::string makeNegative{};
    std::string carriedNegative{};
    std::string makeNegativeUnit{};
    std::string negativeChild{};
    std::string savedSub{};
    /// Live-IR states checked so far, printed by finish().
    unsigned liveStates{};

    /// Checks one row's contents, then its retention with `discharged` as the
    /// expected Confined sites.
    void run(const contents_row & expected, const char * discharged = "");
    /// Runs a row that computeArrayContents must refuse with `failure`.
    void reject(const char * what, std::string body,
                ArrayContentsFailure failure = ArrayContentsFailure::UnsupportedControlFlow);

    void setup();
    void overwritesAndTransport();
    void invariantReads();
    void signedStrides();
    void signedArithmetic();
    void powersAndProducts();
    void validation();
    void finish();
};

} // namespace ctcompile::test::escape::arrays::induction_detail
