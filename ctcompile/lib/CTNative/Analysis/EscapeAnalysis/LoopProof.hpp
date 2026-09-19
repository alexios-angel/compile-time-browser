#pragma once

#include "Contents.hpp"
#include "llvm/ADT/STLFunctionalExtras.h"

namespace ctcompile::ctnative::escape_detail {

struct LoopProof {
    ctjs::FuncOp function;
    State & state;
    llvm::function_ref<bool(std::size_t)> spendWork;

    bool spend(std::size_t count = 1) { return spendWork(count); }
    ContentsValue held(mlir::Value value) { return state.values.lookup(value); }
    mlir::Value origin(mlir::Value value) { return held(value).origin(); }

    ArrayContentsFailure countedLoop(mlir::Block * header, mlir::Block * body,
                                     mlir::ValueRange initial, mlir::Value condition,
                                     mlir::ValueRange intoBody, mlir::ValueRange backedge);
    ArrayContentsFailure cfgCountedLoop(mlir::cf::CondBranchOp branch, mlir::cf::BranchOp latch);
    std::optional<bool> loopContinues();
};

} // namespace ctcompile::ctnative::escape_detail
