#pragma once

#include "../Harness.h"
#include <utility>

namespace ctcompile::test::escape::arrays::induction_detail {

struct InductionCases {
    mlir::MLIRContext & context;
    explicit InductionCases(mlir::MLIRContext & input) : context(input) {}
    std::string prefix{};
    std::string loop{};
    std::string original{};
    unsigned rows{};
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
    unsigned liveStates{};

    static std::string replace(std::string source, const std::string & before,
                               const std::string & after);
    void run(const contents_row & expected, const char * discharged = "");
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
