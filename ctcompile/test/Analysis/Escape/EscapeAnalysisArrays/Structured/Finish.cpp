#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::structured_detail {

void StructuredCases::finish() {
    budgets = 0;
    for (const auto & expected : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.body + "}\n", &context);
        if (!module) {
            fail(row{.what = expected.what, .body = expected.body, .expected = ""},
                 "the structured contents fixture did not parse");
            continue;
        }
        checkArrayContents(*module, expected);
        budgets += computeArrayContents(*module->getOps<ctjs::FuncOp>().begin()).work;
    }
    std::printf("structured contents: %zu rows, %zu contents budget cutoffs\n", rows.size(),
                budgets);
}

} // namespace ctcompile::test::escape::arrays::structured_detail
