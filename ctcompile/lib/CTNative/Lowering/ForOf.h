//===- ForOf.h - for-of over arrays as index loops --------------*- C++ -*-===//
//
// Folds the for-of loops whose source can only be an array onto their index
// loop, for the native tier.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <utility>

namespace ctcompile::ctnative {

/// The attribute on a read of a folded for-of's source (its `length` and its
/// elements): admission requires that source to be an array this tier holds.
inline constexpr llvm::StringLiteral kForOfArray = "ctnative.for_of_array";

/// Each function foldArrayForOf rewrote, with its body as it was.
using ForOfOriginals =
    llvm::SmallVector<std::pair<ctjs::FuncOp, mlir::OwningOpRef<ctjs::FuncOp>>, 0>;

/// Rewrites every for-of the importer emitted as an iterator open, a test
/// of its record on each path and a close, when the record is used only that
/// way: the open answers undefined for an array (the interpreter's fast path,
/// `__ctbrowser_for_of_open`), so the protocol arm, the open and the close go
/// and the loop indexes the source directly. The source's reads carry
/// kForOfArray, so a function whose source is not proved an array is refused;
/// the caller gives such a function back its body from the returned originals.
ForOfOriginals foldArrayForOf(mlir::ModuleOp module);

} // namespace ctcompile::ctnative
