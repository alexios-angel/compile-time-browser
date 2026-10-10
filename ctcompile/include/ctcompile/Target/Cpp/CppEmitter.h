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
// The fork lives in `ctcompile::cpp` rather than `mlir::emitc` so a binary can
// link it and upstream's MLIRTargetCpp together without symbol clashes, which
// lets the two emitters be compared in one process.
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
/// the region of 'op' must almost all be in the EmitC dialect. The parameter
/// 'declareVariablesAtTop' enforces that all variables for op results and block
/// arguments are declared at the beginning of the function.
/// If parameter 'fileId' is non-empty, then body of `emitc.file` ops
/// with matching id are emitted.
mlir::LogicalResult translateToCpp(mlir::Operation * op, mlir::raw_ostream & os,
                                   bool declareVariablesAtTop = false, mlir::StringRef fileId = {});

/// Registers `-mlir-to-cpp` against THIS emitter, under upstream's own name.
///
/// Keeping upstream's flag name lets the vendored upstream EmitC lit tests in
/// test/Target/Cpp/upstream/ run unmodified against this fork: their RUN lines
/// spell `mlir-translate -mlir-to-cpp` (lib/Target/Cpp/TranslateToCpp.cpp's
/// header says which tag each is from). Renaming the flag would mean editing
/// every vendored file, and the fork would stop being comparable to upstream.
void registerToCppTranslation();

} // namespace ctcompile::cpp

#endif // CTCOMPILE_TARGET_CPP_CPPEMITTER_H
