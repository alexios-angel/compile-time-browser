//===- ctcompile/lib/Support/MLIRContextSetup.cpp - Dialect registry ------===//
//
// Registers the MLIR dialects the ctcompile pipeline reads and writes, for
// every driver (ctjs-opt and the compiler) that builds an MLIRContext. The
// list is deliberately short: only what the pipeline produces or parses.
//
//===----------------------------------------------------------------------===//

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
    // ctjs, and ctnative: the statically typed dialect between CTJS and the C++
    // emitter. ctnative is registered here, rather than in a separate
    // ctnative-opt tool, so ctjs-opt can parse and round-trip ctnative IR
    // without a second driver executable and its extra lit/CMake wiring.
    //
    // func, cf and arith: the three dialects CTJS declares as
    // dependentDialects. A compiled function is a func.func with cf branches
    // between blocks and arith on values that are genuinely integers (shift
    // counts, block arguments, comparison orderings); everything else is a
    // !ctjs.value.
    //
    // emitc, the C++ backend's output. It is registered for parsing, not only
    // for the pass that produces it: a pass's dependent dialects are loaded
    // when the pass runs, which is too late for ctjs-opt to read a file that
    // already contains emitc operations (a lit test of
    // --emitc-eliminate-block-arguments does exactly that, and without this
    // fails with "Dialect `emitc' not found").
    //
    // scf and ub, which --lift-cf-to-scf creates: it rewrites cf.br/cf.cond_br
    // into scf.if, scf.while and scf.index_switch, and materialises ub.poison
    // on paths its edge multiplexers prove unreachable. No other code here
    // produces them, but a pass cannot build an operation of a dialect the
    // context has not loaded, and the failure is a crash inside OperationName
    // rather than a diagnostic.
    registry.insert<ctjs::CTJSDialect, ctnative::CTNativeDialect, mlir::func::FuncDialect,
                    mlir::cf::ControlFlowDialect, mlir::arith::ArithDialect,
                    mlir::emitc::EmitCDialect, mlir::scf::SCFDialect, mlir::ub::UBDialect>();
    // The LLVM dialect and its translations are deliberately not registered:
    // nothing in this pipeline lowers to LLVM IR, and registering them would
    // link LLVM's translation machinery into a driver with nothing to
    // translate (the same mistake as registerAllDialects, one dialect at a
    // time).
}

} // namespace ctcompile
