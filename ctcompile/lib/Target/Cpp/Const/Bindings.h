#pragma once

#include "mlir/IR/Value.h"
#include "llvm/ADT/DenseSet.h"

namespace mlir {
class Operation;
class OpOperand;
} // namespace mlir

namespace ctcompile::cpp {

// A backward may-mutate analysis over C++ bindings and their lvalue views.
// This is independent of heap effects: a const pointer can mutate its pointee.
class ConstBindings {
public:
    void prepare(mlir::Operation * function);
    void finish() { active = false; }
    bool qualifies(mlir::Value value) const;

private:
    bool active = false;
    llvm::DenseSet<mlir::Value> mutableBindings;
};

bool supportsConstBinding(mlir::Type type);
bool readsBinding(mlir::OpOperand & operand);

} // namespace ctcompile::cpp
