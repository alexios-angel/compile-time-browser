#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace ctcompile::ctnative {

// A structural storage edge, not a proof of the stored value's type. The
// returned-table census and final admission must still prove its table,
// callable signatures and owning captures. No input annotations are read.
struct OwnedMethodTableSlot {
    ctjs::CreateObjectOp owner;
    ctjs::SetPropertyOp initialization;
    llvm::SmallVector<ctjs::GetPropertyOp> reads;
};

// Borrows current IR. Reconstruct after any semantic mutation; retained
// operation handles are not an authority for a later module snapshot.
class OwnedMethodTableSlots {
public:
    explicit OwnedMethodTableSlots(mlir::ModuleOp module, unsigned maxSteps = 100000);

    [[nodiscard]] llvm::ArrayRef<OwnedMethodTableSlot> slots() const { return checked; }
    [[nodiscard]] const OwnedMethodTableSlot * lookup(ctjs::SetPropertyOp store) const;
    [[nodiscard]] const OwnedMethodTableSlot * lookup(ctjs::GetPropertyOp read) const;
    [[nodiscard]] bool exhausted() const { return budgetExhausted; }

private:
    llvm::SmallVector<OwnedMethodTableSlot> checked;
    llvm::DenseMap<mlir::Operation *, unsigned> stores;
    llvm::DenseMap<mlir::Operation *, unsigned> loads;
    bool budgetExhausted = false;
};

} // namespace ctcompile::ctnative
