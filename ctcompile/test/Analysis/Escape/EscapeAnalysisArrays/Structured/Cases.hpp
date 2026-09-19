#pragma once

#include "../Harness.h"
#include <utility>

namespace ctcompile::test::escape::arrays::structured_detail {

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
    std::size_t budgets{};

    static std::string replace(std::string source, const std::string & before,
                               const std::string & after);
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
