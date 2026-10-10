//===- Cases.cpp - Structured array-contents driver -------------*- C++ -*-===//
//
// Runs every structured array-contents case group in order.
//
//===----------------------------------------------------------------------===//

#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::structured_detail {

void StructuredCases::reject(const char * what, std::string body, ArrayContentsFailure failure) {
    rows.push_back({.what = what, .body = std::move(body), .failure = failure});
}

} // namespace ctcompile::test::escape::arrays::structured_detail

namespace ctcompile::test::escape::arrays {

/// Builds the structured fixtures, adds every case group's rows, and checks
/// them all.
void checkStructuredContents(mlir::MLIRContext & context) {
    structured_detail::StructuredCases cases(context);
    cases.setup();
    cases.reloads();
    cases.invariantArithmetic();
    cases.signedArithmetic();
    cases.powersAndProducts();
    cases.mutationRefusals();
    cases.finish();
}

} // namespace ctcompile::test::escape::arrays
