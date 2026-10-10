//===- LoopProof.hpp - Counted-loop proof for the contents query ----------===//
//
// Declares LoopProof, which lets computeArrayContents step through a bounded
// `for` loop over one tracked array instead of refusing the loop.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Contents.hpp"
#include "llvm/ADT/STLFunctionalExtras.h"

namespace ctcompile::ctnative::escape_detail {

/// Certifies counted loops for computeArrayContents. Shares the caller's replay
/// State and work budget.
struct LoopProof {
    ctjs::FuncOp function;
    State & state;
    llvm::function_ref<bool(std::size_t)> spendWork;

    bool spend(std::size_t count = 1) { return spendWork(count); }
    ContentsValue held(mlir::Value value) { return state.values.lookup(value); }
    mlir::Value origin(mlir::Value value) { return held(value).origin(); }

    /// Proves that a loop of the form `for (i = start; i < arr.length; i += step)`
    /// (also `>`, `-=` and the negated `!(i >= len)` form) visits only in-bounds
    /// own elements of one tracked array, and that every operation in the header,
    /// body and single-visit if-arms is pure or a store whose key stays inside
    /// that array. On success records state.loop so replay can step the loop.
    /// Returns UnsupportedControlFlow for any shape it cannot prove and WorkLimit
    /// when the shared budget runs out.
    ArrayContentsFailure countedLoop(mlir::Block * header, mlir::Block * body,
                                     mlir::ValueRange initial, mlir::Value condition,
                                     mlir::ValueRange intoBody, mlir::ValueRange backedge);
    /// countedLoop for an unstructured loop: a cond_br header whose true
    /// successor branches straight back to it.
    ArrayContentsFailure cfgCountedLoop(mlir::cf::CondBranchOp branch, mlir::cf::BranchOp latch);
    /// Whether the certified loop runs another iteration from the current state;
    /// null if the state no longer matches the proof.
    std::optional<bool> loopContinues();
};

} // namespace ctcompile::ctnative::escape_detail
