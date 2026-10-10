#pragma once

//===- Bindings.h - Const bindings ------------------------------*- C++ -*-===//
//
// Decides which values the C++ emitter may declare `const`. Only active in a
// module carrying the `ctnative.const_bindings` unit attribute.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"

#include "mlir/IR/Value.h"
#include "llvm/ADT/DenseSet.h"

namespace mlir {
class Operation;
class OpOperand;
} // namespace mlir

namespace ctcompile::cpp {

/// A backward may-mutate analysis over C++ bindings and their lvalue views.
/// This is independent of heap effects: a const pointer can mutate its pointee.
class ConstBindings {
public:
    /// Analyses `function`. Does nothing (and qualifies() stays false) unless
    /// the enclosing module carries `ctnative.const_bindings`.
    void prepare(mlir::Operation * function);
    /// Turns the analysis off once the function has been printed.
    void finish() { active = false; }
    /// True when `value` can be printed as a `const` binding: its type is a
    /// copyable carrier, nothing writes it or a view of it, and it is not an
    /// emitc.variable or an empty opaque constant.
    bool qualifies(mlir::Value value) const;

private:
    bool active = false;
    llvm::DenseSet<mlir::Value> mutableBindings;
};

/// True for types whose `const` copy behaves like the value: scalars,
/// pointers, and a fixed list of copyable native carrier types.
bool supportsConstBinding(mlir::Type type);

/// True when `operand`'s use only reads the bound value, so it does not stop
/// that value being `const`. Anything not known to be a read counts as a write.
bool readsBinding(mlir::OpOperand & operand);

} // namespace ctcompile::cpp
