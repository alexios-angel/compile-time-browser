#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::induction_detail {

std::string InductionCases::replace(std::string source, const std::string & before,
                                    const std::string & after) {
    const std::size_t position = source.find(before);
    assert(position != std::string::npos);
    source.replace(position, before.size(), after);
    return source;
}

void InductionCases::run(const contents_row & expected, const char * discharged) {
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + expected.body + "}\n", &context);
    if (!module) {
        fail(row{.what = expected.what, .body = expected.body, .expected = ""},
             "the induction fixture did not parse");
        return;
    }
    checkArrayContents(*module, expected);
    budgets +=
        checkArrayRetention(*module, {.what = expected.what,
                                      .body = expected.body,
                                      .discharged = discharged,
                                      .complete = expected.failure == ArrayContentsFailure::None});
    ++rows;
}

void InductionCases::reject(const char * what, std::string body, ArrayContentsFailure failure) {
    run({.what = what, .body = std::move(body), .failure = failure});
}

} // namespace ctcompile::test::escape::arrays::induction_detail

namespace ctcompile::test::escape::arrays {

void checkArrayInduction(mlir::MLIRContext & context) {
    induction_detail::InductionCases cases(context);
    cases.setup();
    cases.overwritesAndTransport();
    cases.invariantReads();
    cases.signedStrides();
    cases.signedArithmetic();
    cases.powersAndProducts();
    cases.validation();
    cases.finish();
}

} // namespace ctcompile::test::escape::arrays
