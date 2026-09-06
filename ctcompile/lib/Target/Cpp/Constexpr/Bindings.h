#pragma once

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/DenseMap.h"

namespace ctcompile::cpp {
class ConstBindings;

// Target binding time: a static value has both an immutable C++ binding and
// an independently checked constant-expression initializer. Source-level BTA
// facts alone cannot establish the latter after lowering.
class ConstexprBindings {
public:
    void prepare(mlir::Operation * function, const ConstBindings & immutable);
    void finish() { values.clear(); }
    bool qualifies(mlir::Value value) const { return values.contains(value); }

private:
    llvm::DenseMap<mlir::Value, mlir::Attribute> values;
};

namespace constexpr_detail {
bool scalarType(mlir::Type type);
mlir::Attribute evaluate(mlir::Operation * op,
                         const llvm::DenseMap<mlir::Value, mlir::Attribute> & values);
mlir::Attribute integers(mlir::Operation * op, mlir::IntegerAttr left, mlir::IntegerAttr right);
mlir::Attribute floats(mlir::Operation * op, mlir::FloatAttr left, mlir::FloatAttr right);
mlir::Attribute convert(mlir::Attribute value, mlir::Type target);
} // namespace constexpr_detail
} // namespace ctcompile::cpp
