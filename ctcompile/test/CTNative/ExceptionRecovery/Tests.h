#pragma once

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

namespace ctcompile::test::exception_recovery {

namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::lowering_detail::ExceptionRecoveryMode;
using ctcompile::ctnative::lowering_detail::recoverPrimitiveExceptionRegion;

extern int failures;

bool check(bool condition, llvm::StringRef label);
std::string printed(mlir::Operation * operation);
ctjs::FuncOp guarded(mlir::ModuleOp module);
ctjs::CallDirectOp firstCall(ctjs::FuncOp function);
unsigned countChecks(ctjs::FuncOp function);
mlir::OwningOpRef<mlir::ModuleOp> import(mlir::MLIRContext & context, llvm::StringRef source,
                                         bool resolve = true);
void testSource(mlir::MLIRContext & context, llvm::StringRef name, llvm::StringRef source,
                unsigned expectedCalls, double saved, double payload, ExceptionRecoveryMode mode);
void testSourceCompletionTypes(mlir::MLIRContext & context, llvm::StringRef name,
                               llvm::StringRef source, llvm::StringRef normalType,
                               llvm::StringRef stateType);
void testSourceCompletionMutations(mlir::MLIRContext & context, llvm::StringRef source);
void testMutations(mlir::MLIRContext & context, llvm::StringRef source);
void testBindings(mlir::MLIRContext & context, llvm::StringRef source);
std::string nestedSource(llvm::StringRef source, unsigned depth, bool payload);
void testTransitive(mlir::MLIRContext & context, llvm::StringRef source);
void testSelectedActuals(mlir::MLIRContext & context);
void testEffects(mlir::MLIRContext & context);
void testGuardedTail(mlir::MLIRContext & context);

} // namespace ctcompile::test::exception_recovery
