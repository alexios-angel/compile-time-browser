//===- OwnedMethodTableSlots.h - Method table slots -------------*- C++ -*-===//
//
// Declares OwnedMethodTableSlots: object fields written once at creation and
// then only read, the storage shape of a method table.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/WorkBudget.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace ctcompile::ctnative {

/// One field of a local object that is written once, right after the object
/// is created, and then only read. This describes storage only, not the
/// stored value's type: later checks must still prove the table, its callable
/// signatures and their captures. No IR attributes are read.
struct OwnedMethodTableSlot {
    ctjs::CreateObjectOp owner;
    ctjs::SetPropertyOp initialization;
    llvm::SmallVector<ctjs::GetPropertyOp> reads;
};

/// Finds every OwnedMethodTableSlot in a module. Borrows the IR: rebuild after
/// any change to it, since old operation handles prove nothing about the new
/// IR. Finds nothing if `maxSteps` runs out.
class OwnedMethodTableSlots {
public:
    explicit OwnedMethodTableSlots(mlir::ModuleOp module, unsigned maxSteps = 100000);

    [[nodiscard]] llvm::ArrayRef<OwnedMethodTableSlot> slots() const { return checked; }
    /// The slot a store initialises or a read reads, or null.
    [[nodiscard]] const OwnedMethodTableSlot * lookup(mlir::Operation * operation) const;
    [[nodiscard]] bool exhausted() const { return budget.exhausted; }

private:
    llvm::SmallVector<OwnedMethodTableSlot> checked;
    llvm::DenseMap<mlir::Operation *, unsigned> indices;
    WorkBudget budget;
};

} // namespace ctcompile::ctnative
