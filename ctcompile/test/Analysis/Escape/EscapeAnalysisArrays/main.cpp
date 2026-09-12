// Array/object contents and retention rows share one executable and run in their
// original order. Topic files keep each matrix beside its live mutation checks.
#include "Harness.h"
#include "PrimitiveBinary.h"

using namespace ctcompile::test::escape;
using namespace ctcompile::test::escape::arrays;

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<CTNativeDialect>();
    // For the DEFAULT-RULE rows: an operation from no dialect at all has no
    // interface and is not a branch, which is exactly the case the rule is for.
    context.allowUnregisteredDialects();

    checkArrayContents(context);
    checkArrayRetention(context);
    checkObjectContents(context);
    checkObjectDeletions(context);
    checkObjectCopies(context);
    checkOpaqueEntryTransport(context);
    checkSelectorProducers(context);
    checkLogicalNegation(context);
    checkTotalUnaryProducers(context);
    checkStaticBinaryProducers(context);
    checkArithmeticUnaryProducers(context);
    checkBigIntPlusErrors(context);
    checkBigIntMixedSubErrors(context);
    checkBigIntMixedArithmeticErrors(context, ctjs::BinaryKind::Mul);
    checkBigIntMixedArithmeticErrors(context, ctjs::BinaryKind::Div);
    checkBigIntMixedArithmeticErrors(context, ctjs::BinaryKind::Mod);
    checkBigIntMixedArithmeticErrors(context, ctjs::BinaryKind::Pow);
    checkBigIntUnaryProducers(context);
    checkBigIntBinaryProducers(context);
    checkStringBigIntConcatenation(context);
    for (const auto kind : {ctjs::CompareKind::Eq, ctjs::CompareKind::Lt, ctjs::CompareKind::Le,
                            ctjs::CompareKind::Gt, ctjs::CompareKind::Ge}) {
        checkBigIntComparison(context, kind);
        checkPrimitiveBinaryProducer<ctjs::CompareOp>(context, kind);
    }
    for (const auto kind : {ctjs::BinaryKind::Sub, ctjs::BinaryKind::Mul, ctjs::BinaryKind::Div,
                            ctjs::BinaryKind::Mod, ctjs::BinaryKind::Pow, ctjs::BinaryKind::Add,
                            ctjs::BinaryKind::Concat}) {
        checkPrimitiveBinaryProducer<ctjs::BinaryOp>(context, kind);
    }
    checkArrayFrames(context);
    checkArrayConditionals(context);
    checkContainerSwitches(context);

    if (failures != 0) {
        std::printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
