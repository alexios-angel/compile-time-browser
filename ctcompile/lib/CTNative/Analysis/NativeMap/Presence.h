//===- Presence.h - Map key presence proof ----------------------*- C++ -*-===//
//
// Declares provePresence(), the path-sensitive proof of which native Map reads
// definitely find their key. Presence.cpp has the analysis itself.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/PrimitiveAlternatives.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"

#include <string>

namespace ctcompile::ctnative {
class OwnedGlobalRoots;
}

namespace ctcompile::ctnative::map_detail {

/// Proves which Map reads definitely find their key and writes the result as
/// ctnative.map_* attributes. Returns a refusal reason, or empty on success.
///
/// `calls` and `sizes` are every Map method call and `size` read. Every one of
/// `reads` must be proved present or the proof fails. `optionalReads` are
/// marked present only when proved; otherwise they keep their nullable
/// result. `typedReads` get a scalar read type when one is proved.
/// `familyOf` maps a value to its schema family, consulted only to find
/// possible aliases: a fact always describes one runtime instance and one
/// key. `snapshotCopies` are the Array.from copies the caller proved (not IR
/// attributes). `parameters` seeds the primitive kinds of host-method
/// parameters. A `hostTyped` operation is a browser operation proved by the
/// host contract: it cannot reach a local Map, so it keeps every fact an
/// unknown call would erase.
std::string provePresence(mlir::ModuleOp module, llvm::ArrayRef<ctjs::CallOp> calls,
                          llvm::ArrayRef<ctjs::GetPropertyOp> sizes,
                          llvm::ArrayRef<ctjs::CallOp> reads,
                          llvm::ArrayRef<ctjs::CallOp> optionalReads,
                          llvm::ArrayRef<ctjs::CallOp> typedReads,
                          const llvm::DenseSet<mlir::Operation *> & snapshotCopies,
                          llvm::function_ref<mlir::Value(mlir::Value)> familyOf,
                          const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & parameters,
                          const OwnedGlobalRoots * globals,
                          llvm::function_ref<bool(mlir::Operation *)> hostTyped = nullptr);

} // namespace ctcompile::ctnative::map_detail
