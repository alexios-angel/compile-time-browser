#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::structured_detail {

std::string StructuredCases::replace(std::string source, const std::string & before,
                                     const std::string & after) {
    const std::size_t position = source.find(before);
    assert(position != std::string::npos);
    source.replace(position, before.size(), after);
    return source;
}

void StructuredCases::reject(const char * what, std::string body, ArrayContentsFailure failure) {
    rows.push_back({.what = what, .body = std::move(body), .failure = failure});
}

} // namespace ctcompile::test::escape::arrays::structured_detail

namespace ctcompile::test::escape::arrays {

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
