#include "Bindings.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinTypes.h"
#include "llvm/ADT/SmallVector.h"

namespace ctcompile::cpp::constexpr_detail {
namespace ec = mlir::emitc;

bool scalarType(mlir::Type type) {
    if (auto integer = llvm::dyn_cast<mlir::IntegerType>(type)) {
        const unsigned width = integer.getWidth();
        return width == 1 || width == 8 || width == 16 || width == 32 || width == 64;
    }
    return type.isF32() || type.isF64();
}

mlir::Attribute evaluate(mlir::Operation * op,
                         const llvm::DenseMap<mlir::Value, mlir::Attribute> & values) {
    if (auto constant = llvm::dyn_cast<ec::ConstantOp>(op)) {
        auto value = constant.getValue();
        if (auto integer = llvm::dyn_cast<mlir::IntegerAttr>(value)) {
            return convert(integer, op->getResult(0).getType());
        }
        if (auto number = llvm::dyn_cast<mlir::FloatAttr>(value);
            number && number.getValue().isFinite()) {
            return convert(number, op->getResult(0).getType());
        }
        return {}; // Opaque text can hide runtime calls, macros and references.
    }
    llvm::SmallVector<mlir::Attribute> inputs;
    for (mlir::Value operand : op->getOperands()) {
        auto known = values.lookup(operand);
        if (!known) { return {}; }
        inputs.push_back(known);
    }
    if (inputs.empty()) { return {}; }
    if (llvm::isa<ec::CastOp>(op)) { return convert(inputs.front(), op->getResult(0).getType()); }
    if (llvm::isa<ec::ConditionalOp>(op) && inputs.size() == 3) {
        auto condition = llvm::dyn_cast<mlir::IntegerAttr>(inputs.front());
        if (!condition || !condition.getType().isInteger(1)) { return {}; }
        return convert(inputs[condition.getValue().isZero() ? 2u : 1u], op->getResult(0).getType());
    }
    if (inputs.size() > 2) { return {}; }
    if (auto left = llvm::dyn_cast<mlir::IntegerAttr>(inputs.front())) {
        auto right = inputs.size() == 2 ? llvm::dyn_cast<mlir::IntegerAttr>(inputs.back())
                                        : mlir::IntegerAttr{};
        if (inputs.size() == 2 && !right) { return {}; }
        return integers(op, left, right);
    }
    if (auto left = llvm::dyn_cast<mlir::FloatAttr>(inputs.front())) {
        auto right =
            inputs.size() == 2 ? llvm::dyn_cast<mlir::FloatAttr>(inputs.back()) : mlir::FloatAttr{};
        if (inputs.size() == 2 && !right) { return {}; }
        return floats(op, left, right);
    }
    return {};
}

} // namespace ctcompile::cpp::constexpr_detail
