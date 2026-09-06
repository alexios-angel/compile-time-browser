#include "Bindings.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinTypes.h"

namespace ctcompile::cpp::constexpr_detail {
namespace ec = mlir::emitc;

mlir::Attribute floats(mlir::Operation * op, mlir::FloatAttr left, mlir::FloatAttr right) {
    const auto type = left.getType();
    const auto resultType = op->getResult(0).getType();
    const auto & a = left.getValue();
    if (!a.isFinite() || (right && (right.getType() != type || !right.getValue().isFinite()))) {
        return {};
    }
    const auto boolean = [&](bool value) -> mlir::Attribute {
        return resultType.isInteger(1) ? mlir::IntegerAttr::get(resultType, value)
                                       : mlir::Attribute{};
    };
    if (llvm::isa<ec::LogicalNotOp>(op)) { return boolean(a.isZero()); }
    if (auto compare = llvm::dyn_cast<ec::CmpOp>(op); compare && right) {
        const auto order = a.compare(right.getValue());
        switch (compare.getPredicate()) {
        case ec::CmpPredicate::eq: return boolean(order == llvm::APFloat::cmpEqual);
        case ec::CmpPredicate::ne: return boolean(order != llvm::APFloat::cmpEqual);
        case ec::CmpPredicate::lt: return boolean(order == llvm::APFloat::cmpLessThan);
        case ec::CmpPredicate::le: return boolean(order != llvm::APFloat::cmpGreaterThan);
        case ec::CmpPredicate::gt: return boolean(order == llvm::APFloat::cmpGreaterThan);
        case ec::CmpPredicate::ge: return boolean(order != llvm::APFloat::cmpLessThan);
        default: return {};
        }
    }
    if (right && llvm::isa<ec::LogicalAndOp, ec::LogicalOrOp>(op)) {
        const bool aTrue = !a.isZero(), bTrue = !right.getValue().isZero();
        return boolean(llvm::isa<ec::LogicalAndOp>(op) ? aTrue && bTrue : aTrue || bTrue);
    }
    if (resultType != type) { return {}; }
    llvm::APFloat result = a;
    auto status = llvm::APFloat::opOK;
    constexpr auto rounding = llvm::APFloat::rmNearestTiesToEven;
    if (!right) {
        if (llvm::isa<ec::UnaryMinusOp>(op)) {
            result.changeSign();
        } else if (!llvm::isa<ec::UnaryPlusOp>(op)) {
            return {};
        }
    } else if (llvm::isa<ec::AddOp>(op)) {
        status = result.add(right.getValue(), rounding);
    } else if (llvm::isa<ec::SubOp>(op)) {
        status = result.subtract(right.getValue(), rounding);
    } else if (llvm::isa<ec::MulOp>(op)) {
        status = result.multiply(right.getValue(), rounding);
    } else if (llvm::isa<ec::DivOp>(op)) {
        status = result.divide(right.getValue(), rounding);
    } else {
        return {};
    }
    // Constant evaluation must not turn an exceptional runtime expression
    // into a compile error, or round an inexact runtime operation differently.
    if (status != llvm::APFloat::opOK || !result.isFinite()) { return {}; }
    // Even exact cancellation can select the sign of zero from the runtime
    // rounding mode. Multiplication/division/negation preserve its fixed sign.
    if (result.isZero() && llvm::isa<ec::AddOp, ec::SubOp>(op)) { return {}; }
    return mlir::FloatAttr::get(type, result);
}

} // namespace ctcompile::cpp::constexpr_detail
