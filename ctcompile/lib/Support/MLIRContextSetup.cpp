#include <ctcompile/Support/MLIRContextSetup.hpp>

#include "ctcompile/CTJS/IR/CTJSDialect.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/DialectRegistry.h"

namespace ctcompile {

void registerCTCompileDialects(mlir::DialectRegistry & registry) {
    registry.insert<ctjs::CTJSDialect>();
    // THE THREE THE DIALECT DECLARES AS dependentDialects, and no more. A
    // compiled function is a func.func with cf branches between its blocks and
    // arith on the integers that are genuinely integers - shift counts, block
    // arguments, the ordering a comparison returns - and everything else is a
    // !ctjs.value.
    registry.insert<mlir::func::FuncDialect>();
    registry.insert<mlir::cf::ControlFlowDialect>();
    registry.insert<mlir::arith::ArithDialect>();
    // WHAT IS NOT HERE: the LLVM dialect and its translation registrations,
    // which Phase 7's deliverable list names. They belong to the SECOND backend
    // (Phases 11-12A), and registering them now would link LLVM's translation
    // machinery into a driver that has nothing to translate - which is the same
    // mistake as registerAllDialects, one dialect at a time.
}

} // namespace ctcompile
