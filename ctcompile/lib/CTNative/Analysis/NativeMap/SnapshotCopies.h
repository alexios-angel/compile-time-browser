//===- SnapshotCopies.h - Map snapshot copy candidates ----------*- C++ -*-===//
//
// Declares the search for `Array.from(m.keys())` / `Array.from(m.values())`
// snapshot copies of native Maps, used by NativeMap.cpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"

#include <string>

namespace ctcompile::ctnative::map_detail {

/// The snapshot copies found in a module.
struct snapshotCopies {
    /// The `Array` loads and `Array.from` property reads they use.
    llvm::DenseSet<mlir::Operation *> builtins;
    /// The `Array.from(...)` calls themselves.
    llvm::DenseSet<mlir::Operation *> calls;
};

/// Collects the snapshot copies of the Map calls in `mapCalls` into `out`, and
/// returns a refusal reason (empty on success). IR attributes are neither
/// read nor written. The caller separately proves the module has no unknown
/// calls or global reflection. Each keys()/values() iterator must have one
/// immediate Array.from consumer, so copying it eagerly into a native vector
/// cannot expose iterator state or move a read across other effects. `spend`
/// charges one unit of work and returns false once the budget is exhausted.
std::string collectSnapshotCopies(
    mlir::Operation * scope, const llvm::DenseSet<mlir::Operation *> & mapCalls,
    snapshotCopies & out, llvm::function_ref<bool()> spend = [] { return true; });

} // namespace ctcompile::ctnative::map_detail
