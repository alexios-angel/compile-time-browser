//===- Cases.cpp - Induction case table -------------------------*- C++ -*-===//
//
// InductionCases' row runners and checkArrayInduction, which runs every
// induction test group in order.
//
//===----------------------------------------------------------------------===//
#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::induction_detail {

void InductionCases::run(const contents_row & expected, const char * discharged) {
    auto module = parseContents(context, expected);
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
