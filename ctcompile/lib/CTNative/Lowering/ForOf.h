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

/// The attribute on a read a folded array destructuring made (`row[i]` for
/// its i-th binding): admission requires `row` to be a tuple or an array this
/// tier holds.
inline constexpr llvm::StringLiteral kDestructureArray = "ctnative.destructure_array";

/// The attribute on an `__ctbrowser_iter_open` call foldArrayForOf did not
/// fold, and on the load of its helper, holding why; admission refuses the
/// function with it.
inline constexpr llvm::StringLiteral kDestructureRefusal = "ctnative.destructure_refusal";

/// Each function foldArrayForOf rewrote, with its body as it was.
using ForOfOriginals =
    llvm::SmallVector<std::pair<ctjs::FuncOp, mlir::OwningOpRef<ctjs::FuncOp>>, 0>;

/// Rewrites every for-of the importer emitted as an iterator open, a test
/// of its record on each path and a close, when the record is used only that
/// way: the open answers undefined for an array (the interpreter's fast path,
/// `__ctbrowser_for_of_open`), so the protocol arm, the open and the close go
/// and the loop indexes the source directly. The source's reads carry
/// kForOfArray, so a function whose source is not proved an array is refused.
///
/// It also folds a flat array destructuring (`const [k, v] = row`, an
/// `__ctbrowser_iter_open` stepped once per binding and closed) into `row[0]`,
/// `row[1]`, ...: the array iterator yields the elements in order and its
/// close does nothing. Those reads carry kDestructureArray. A default, a rest
/// element or a nested pattern keeps the helpers, with kDestructureRefusal.
///
/// The caller gives a refused function back its body from the returned
/// originals.
ForOfOriginals foldArrayForOf(mlir::ModuleOp module);

} // namespace ctcompile::ctnative
