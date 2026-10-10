//===- WorkBudget.h - Bounded analysis work ---------------------*- C++ -*-===//
//
// The one step budget every bounded proof spends, so that an analysis over a
// large or adversarial module stops with a refusal instead of running for
// ever.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_ANALYSIS_WORKBUDGET_H
#define CTCOMPILE_CTNATIVE_ANALYSIS_WORKBUDGET_H

#include "mlir/IR/Operation.h"
#include "llvm/ADT/STLFunctionalExtras.h"

#include <cstdint>

namespace ctcompile::ctnative {

/// Steps left, spent one or many at a time. A charge that does not fit takes
/// nothing and marks the budget exhausted; callers still write the refusal
/// text.
struct WorkBudget {
    uint64_t remaining;
    bool exhausted = false;

    /// Takes `amount` steps; false (and exhausted) when fewer remain.
    bool spend(uint64_t amount = 1) {
        if (amount > remaining) {
            exhausted = true;
            return false;
        }
        remaining -= amount;
        return true;
    }

    /// Precharges every operation `root.walk` visits at `cost(op)`, stopping at
    /// the first charge that does not fit. `root` is an Operation, Region or
    /// Block.
    template <class Root>
    bool spendWalk(Root & root, llvm::function_ref<uint64_t(mlir::Operation *)> cost) {
        return !root.walk([&](mlir::Operation * op) {
                        return spend(cost(op)) ? mlir::WalkResult::advance()
                                               : mlir::WalkResult::interrupt();
                    })
                    .wasInterrupted();
    }
};

} // namespace ctcompile::ctnative

#endif
