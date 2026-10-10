//===- Vectors.h - Emission of vectors of one proved element type -*- C++ -*-===//
//
// Arrays the element-vector proof accepts (TypeInference::elementVectorAliases)
// lower to `std::vector<T>` and the helpers in Runtime/Array.hpp, for T a
// String or a two-element row (std::pair). The plan is made while the IR is still ctjs, before any value is
// retyped; the replacement reads it back from each operation.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

/// Records, on every operation of `fn` that builds, reads or calls a method of
/// an array of Strings (or splits a String), what it lowers to, and returns
/// the values that name such an array so retype() can spell them.
llvm::DenseSet<mlir::Value> planStringArrays(lowering & lower, ctjs::FuncOp fn);

/// Replaces one operation planStringArrays marked; false for any other.
bool replaceStringArray(lowering & lower, mlir::Operation * o);

} // namespace ctcompile::ctnative::lowering_detail
