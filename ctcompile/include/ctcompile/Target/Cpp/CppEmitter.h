//===- CppEmitter.h - ctcompile's forked C++ emitter ------------*- C++ -*-===//
//
// Derived from mlir/include/mlir/Target/Cpp/CppEmitter.h, llvm/llvm-project at
// tag llvmorg-22.1.8 (sha256
// d2a4cc2bab34e4912bdae0f5e48031521493a6badca027e002130ab7df9bd7aa), which is
// Apache-2.0 WITH LLVM-exception - the same licence this project ships under.
// See NOTICE.
//
//===----------------------------------------------------------------------===//
//
// THE DECLARATION IS OURS AND THE IMPLEMENTATION IS UPSTREAM'S, for now.
// `ctcompile::cpp` rather than `mlir::emitc` so that a binary may link this and
// MLIRTargetCpp at once and the two never resolve to each other - which is what
// makes it possible to A/B our emitter against upstream's in one process, and
// is the only reason the namespace differs at all.
//
// The fork's whole rationale, its provenance, and the vendored upstream lit
// suite that keeps it a superset are in lib/Target/Cpp/TranslateToCpp.cpp.
//
//===----------------------------------------------------------------------===//

#ifndef CTCOMPILE_TARGET_CPP_CPPEMITTER_H
#define CTCOMPILE_TARGET_CPP_CPPEMITTER_H

#include "mlir/Support/LLVM.h"
#include "llvm/ADT/StringRef.h"

namespace mlir {
class Operation;
} // namespace mlir

namespace ctcompile::cpp {

/// Translates the given operation to C++ code. The operation or operations in
/// the region of 'op' need almost all be in EmitC dialect. The parameter
/// 'declareVariablesAtTop' enforces that all variables for op results and block
/// arguments are declared at the beginning of the function.
/// If parameter 'fileId' is non-empty, then body of `emitc.file` ops
/// with matching id are emitted.
mlir::LogicalResult translateToCpp(mlir::Operation * op, mlir::raw_ostream & os,
                                   bool declareVariablesAtTop = false, mlir::StringRef fileId = {});

/// Registers `-mlir-to-cpp` against THIS emitter, under upstream's own name.
///
/// The name is deliberate and it is what makes Stage 47A's gate cheap:
/// upstream's 35 EmitC lit tests spell `mlir-translate -mlir-to-cpp` in their
/// RUN lines, and they are vendored unmodified. Changing the flag would mean
/// editing 35 vendored files, which is precisely the thing that makes a fork
/// stop being comparable to what it forked.
void registerToCppTranslation();

} // namespace ctcompile::cpp

#endif // CTCOMPILE_TARGET_CPP_CPPEMITTER_H
