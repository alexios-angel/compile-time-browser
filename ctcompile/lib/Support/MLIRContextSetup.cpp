#include <ctcompile/Support/MLIRContextSetup.hpp>

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/DialectRegistry.h"

namespace ctcompile {

void registerCTCompileDialects(mlir::DialectRegistry & registry) {
    registry.insert<ctjs::CTJSDialect>();
    // AND ctnative, WHICH IS WHAT THIS HEADER MEANT BY "the list is the
    // pipeline's, and it grows when a phase needs it to". Phase 53's dialect:
    // the static types that sit between CTJS and the C++ emitter.
    //
    // PART 24's STAGE 53A ASKS FOR A SEPARATE `ctnative-opt` DRIVER AND THIS IS
    // THE DEVIATION. Standing one up means a new tools/ subdirectory, a line in
    // tools/CMakeLists.txt, and then two MID-FILE edits: the tool's build
    // directory into CTCOMPILE_TOOLS_DIRS and its name into lit.cfg.py's tool
    // list. Appendix A.3 of part 23 names test/CMakeLists.txt as the most
    // collided-on file in the tree and asks agents to APPEND ONE BLOCK AT THE
    // END rather than edit it in the middle. Registering the dialect here
    // instead is two additive lines, gives ctjs-opt the same round-trip the
    // plan wanted ctnative-opt for, and costs nothing: a driver that can parse
    // ctnative is the requirement, not a second executable.
    registry.insert<ctnative::CTNativeDialect>();
    // THE THREE THE DIALECT DECLARES AS dependentDialects, and no more. A
    // compiled function is a func.func with cf branches between its blocks and
    // arith on the integers that are genuinely integers - shift counts, block
    // arguments, the ordering a comparison returns - and everything else is a
    // !ctjs.value.
    registry.insert<mlir::func::FuncDialect>();
    registry.insert<mlir::cf::ControlFlowDialect>();
    registry.insert<mlir::arith::ArithDialect>();
    // AND EmitC, WHICH IS THE PRIMARY BACKEND'S OUTPUT. It is registered for
    // PARSING, not only for the pass that produces it: a pass declaring it as a
    // dependent dialect gets it loaded when the pass runs, which is too late
    // for ctjs-opt to read a file that already contains emitc operations. That
    // is exactly what a lit test of --emitc-eliminate-block-arguments does, and
    // without this it fails with "Dialect `emitc' not found".
    registry.insert<mlir::emitc::EmitCDialect>();

    // AND THE TWO THE CFG LIFTING PASS CREATES.
    //
    // --lift-cf-to-scf rewrites cf.br/cf.cond_br into scf.if, scf.while and
    // scf.index_switch, and it materialises ub.poison on the paths its edge
    // multiplexers prove unreachable. Neither dialect is ever produced by this
    // project's own code, which is why they were absent - but a pass cannot
    // build an operation of a dialect the context has not loaded, and the
    // failure is a crash inside OperationName rather than a diagnostic.
    registry.insert<mlir::scf::SCFDialect>();
    registry.insert<mlir::ub::UBDialect>();
    // WHAT IS NOT HERE: the LLVM dialect and its translation registrations,
    // which Phase 7's deliverable list names. They belong to the SECOND backend
    // (Phases 11-12A), and registering them now would link LLVM's translation
    // machinery into a driver that has nothing to translate - which is the same
    // mistake as registerAllDialects, one dialect at a time.
}

} // namespace ctcompile
