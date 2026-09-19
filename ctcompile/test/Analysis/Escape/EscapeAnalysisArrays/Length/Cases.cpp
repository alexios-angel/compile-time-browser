#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::length_detail {

void LengthCases::check(mlir::ModuleOp module, const contents_row & expected,
                        const char * discharged) {
    checkArrayContents(module, expected);
    const bool complete = expected.failure == ArrayContentsFailure::None;
    budgets += checkArrayRetention(module, {.what = expected.what,
                                            .body = expected.body,
                                            .discharged = complete ? discharged : "",
                                            .complete = complete});
}

mlir::OwningOpRef<mlir::ModuleOp> LengthCases::parse(const contents_row & expected) {
    return mlir::parseSourceString<mlir::ModuleOp>(std::string{kPrologue} + expected.body + "}\n",
                                                   &context);
}

void LengthCases::run(const contents_row & expected, const char * discharged,
                      const char * receiverVerdict) {
    if (auto module = parse(expected)) {
        check(*module, expected, discharged);
        if (receiverVerdict != nullptr) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            mlir::DataFlowSolver solver;
            solver.load<mlir::dataflow::DeadCodeAnalysis>();
            solver.load<mlir::dataflow::SparseConstantPropagation>();
            solver.load<EscapeAnalysis>();
            const row r{.what = expected.what, .body = expected.body, .expected = receiverVerdict};
            if (failed(solver.initializeAndRun(*module))) {
                fail(r, "the private receiver solver did not converge");
            } else {
                ctjs::CreateArrayOp receiver;
                module->walk([&](ctjs::CreateArrayOp op) {
                    if (contentsLabel(op) == "a") { receiver = op; }
                });
                if (!receiver ||
                    verdictString(computeVerdicts(solver, function, 0), receiver) !=
                        "escapes:passed" ||
                    verdictString(computeVerdicts(solver, function), receiver) != receiverVerdict) {
                    fail(r, "the private receiver verdict or original Passed witness differs");
                }
            }
        }
    } else {
        fail(row{.what = expected.what, .body = expected.body, .expected = ""},
             "the dense length fixture did not parse");
    }
    ++rows;
}

} // namespace ctcompile::test::escape::arrays::length_detail

namespace ctcompile::test::escape::arrays {

void checkDenseArrayLength(mlir::MLIRContext & context) {
    length_detail::LengthCases cases(context);
    cases.setup();
    cases.indexSnapshots();
    cases.arithmetic();
    cases.shrinkArithmetic();
    cases.shrinkRetention();
    cases.liveMutations();
    cases.finish();
}

} // namespace ctcompile::test::escape::arrays
