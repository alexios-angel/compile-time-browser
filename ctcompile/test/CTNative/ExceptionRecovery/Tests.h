#pragma once

//===- Tests.h - Shared exception-recovery test helpers ---------*- C++ -*-===//
//
// Declarations shared by the exception-recovery tests. The helpers are
// defined in Completion.cpp; each test entry point is defined in Completion.cpp
// or Guards.cpp and called from Main.cpp.
//
//===----------------------------------------------------------------------===//

#include "../../../lib/CTNative/Lowering/Exceptions/Recovery.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/Import/BytecodeImport.hpp"
#include "ctcompile/CTJS/Transforms/Passes.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "ctbrowser/script/compile.hpp"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <tuple>

#include "check.hpp"

namespace ctcompile::test::exception_recovery {

namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::lowering_detail::ExceptionRecoveryMode;
using ctcompile::ctnative::lowering_detail::recoverPrimitiveExceptionRegion;

/// Records a test failure under `label` when `condition` is false, and returns
/// `condition`.
bool check(bool condition, llvm::StringRef label);
/// Replaces the first occurrence of `from`, which must be present.
inline void replaceOnce(std::string & text, llvm::StringRef from, llvm::StringRef to) {
    text.replace(text.find(from.str()), from.size(), to.str());
}
/// Returns the printed form of `operation`, used to compare IR byte for byte.
std::string printed(mlir::Operation * operation);
/// Returns the module's `guarded$...` function, or null.
ctjs::FuncOp guarded(mlir::ModuleOp module);
/// Returns the first direct call in `function`, or null.
ctjs::CallDirectOp firstCall(ctjs::FuncOp function);
/// Counts the ctjs.check ops (status edges of fallible operations).
unsigned countChecks(ctjs::FuncOp function);
/// Compiles and imports JavaScript `source`. With `resolve`, also runs
/// global resolution and structured-control lifting.
mlir::OwningOpRef<mlir::ModuleOp> import(mlir::MLIRContext & context, llvm::StringRef source,
                                         bool resolve = true);
void testSource(mlir::MLIRContext & context, llvm::StringRef name, llvm::StringRef source,
                unsigned expectedCalls, double saved, double payload, ExceptionRecoveryMode mode,
                bool resolve = true);
void testSourceCompletionTypes(mlir::MLIRContext & context, llvm::StringRef name,
                               llvm::StringRef source, llvm::StringRef normalType,
                               llvm::StringRef stateType);
void testSourceCompletionMutations(mlir::MLIRContext & context, llvm::StringRef source);
void testOrdinaryCompletionTypes(mlir::MLIRContext & context);
void testMutations(mlir::MLIRContext & context, llvm::StringRef source, bool resolve = true);
void testBindings(mlir::MLIRContext & context, llvm::StringRef source);
std::string nestedSource(llvm::StringRef source, unsigned depth, bool payload);
void testTransitive(mlir::MLIRContext & context, llvm::StringRef source);
void testSelectedActuals(mlir::MLIRContext & context);
void testEffects(mlir::MLIRContext & context);
std::string guardedPrefix(ctjs::FuncOp function);
void testGuardedTail(mlir::MLIRContext & context);

} // namespace ctcompile::test::exception_recovery
