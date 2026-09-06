#include "Bindings.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinTypes.h"

namespace ctcompile::cpp::constexpr_detail {
namespace ec = mlir::emitc;

mlir::Attribute integers(mlir::Operation * op, mlir::IntegerAttr left, mlir::IntegerAttr right) {
    const auto type = llvm::cast<mlir::IntegerType>(left.getType());
    const auto resultType = op->getResult(0).getType();
    const auto & a = left.getValue();
    const bool unsignedValue = type.isUnsigned() || type.getWidth() == 1;
    const auto boolean = [&](bool value) -> mlir::Attribute {
        return resultType.isInteger(1) ? mlir::IntegerAttr::get(resultType, value)
                                       : mlir::Attribute{};
    };
    if (llvm::isa<ec::LogicalNotOp>(op)) { return boolean(a.isZero()); }
    if (right && right.getType() != type) { return {}; }
    if (auto compare = llvm::dyn_cast<ec::CmpOp>(op); compare && right) {
        const auto & b = right.getValue();
        const bool less = unsignedValue ? a.ult(b) : a.slt(b);
        const bool greater = unsignedValue ? a.ugt(b) : a.sgt(b);
        switch (compare.getPredicate()) {
        case ec::CmpPredicate::eq: return boolean(a == b);
        case ec::CmpPredicate::ne: return boolean(a != b);
        case ec::CmpPredicate::lt: return boolean(less);
        case ec::CmpPredicate::le: return boolean(!greater);
        case ec::CmpPredicate::gt: return boolean(greater);
        case ec::CmpPredicate::ge: return boolean(!less);
        default: return {};
        }
    }
    if (right && llvm::isa<ec::LogicalAndOp, ec::LogicalOrOp>(op)) {
        const bool aTrue = !a.isZero(), bTrue = !right.getValue().isZero();
        return boolean(llvm::isa<ec::LogicalAndOp>(op) ? aTrue && bTrue : aTrue || bTrue);
    }
    // Narrow integer operands undergo C++ integral promotions. Keep their
    // arithmetic dynamic until the promoted intermediate type is modeled.
    if (type.getWidth() < 32 || resultType != type) { return {}; }
    llvm::APInt result = a;
    bool overflow = false;
    if (!right) {
        if (llvm::isa<ec::UnaryMinusOp>(op)) {
            if (!unsignedValue && a.isMinSignedValue()) { return {}; }
            result = -a;
        } else if (llvm::isa<ec::BitwiseNotOp>(op)) {
            result = ~a;
        } else if (!llvm::isa<ec::UnaryPlusOp>(op)) {
            return {};
        }
    } else {
        const auto & b = right.getValue();
        if (llvm::isa<ec::AddOp>(op)) {
            result = unsignedValue ? a + b : a.sadd_ov(b, overflow);
        } else if (llvm::isa<ec::SubOp>(op)) {
            result = unsignedValue ? a - b : a.ssub_ov(b, overflow);
        } else if (llvm::isa<ec::MulOp>(op)) {
            result = unsignedValue ? a * b : a.smul_ov(b, overflow);
        } else if (llvm::isa<ec::DivOp, ec::RemOp>(op)) {
            if (b.isZero() || (!unsignedValue && a.isMinSignedValue() && b.isAllOnes())) {
                return {};
            }
            if (llvm::isa<ec::DivOp>(op)) {
                result = unsignedValue ? a.udiv(b) : a.sdiv(b);
            } else {
                result = unsignedValue ? a.urem(b) : a.srem(b);
            }
        } else if (llvm::isa<ec::BitwiseAndOp>(op)) {
            result = a & b;
        } else if (llvm::isa<ec::BitwiseOrOp>(op)) {
            result = a | b;
        } else if (llvm::isa<ec::BitwiseXorOp>(op)) {
            result = a ^ b;
        } else if (llvm::isa<ec::BitwiseLeftShiftOp, ec::BitwiseRightShiftOp>(op)) {
            if (b.uge(type.getWidth()) || (!unsignedValue && a.isNegative())) { return {}; }
            const auto shift = static_cast<unsigned>(b.getZExtValue());
            if (llvm::isa<ec::BitwiseLeftShiftOp>(op)) {
                if (!unsignedValue && a.getActiveBits() + shift >= type.getWidth()) { return {}; }
                result = a.shl(shift);
            } else {
                result = a.lshr(shift);
            }
        } else {
            return {};
        }
    }
    return overflow ? mlir::Attribute{} : mlir::IntegerAttr::get(type, result);
}

} // namespace ctcompile::cpp::constexpr_detail
