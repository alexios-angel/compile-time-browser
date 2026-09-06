#include "Bindings.h"

#include "mlir/IR/BuiltinTypes.h"
#include "llvm/ADT/APSInt.h"

namespace ctcompile::cpp::constexpr_detail {

mlir::Attribute convert(mlir::Attribute value, mlir::Type target) {
    if (!scalarType(target)) { return {}; }
    const auto toInteger = llvm::dyn_cast<mlir::IntegerType>(target);
    if (auto integer = llvm::dyn_cast<mlir::IntegerAttr>(value)) {
        const auto from = llvm::cast<mlir::IntegerType>(integer.getType());
        const bool isUnsigned = from.isUnsigned() || from.getWidth() == 1;
        const auto & number = integer.getValue();
        if (toInteger) {
            if (toInteger.getWidth() == 1) {
                return mlir::IntegerAttr::get(target, !number.isZero());
            }
            // Prove representability; out-of-range signed conversions and
            // negative-to-unsigned wrapping remain ordinary runtime casts.
            const bool negative = !isUnsigned && number.isNegative();
            if (toInteger.isUnsigned()) {
                if (negative || number.getActiveBits() > toInteger.getWidth()) { return {}; }
            } else if ((negative && number.getSignificantBits() > toInteger.getWidth()) ||
                       (!negative && number.getActiveBits() >= toInteger.getWidth())) {
                return {};
            }
            const auto result = isUnsigned ? number.zextOrTrunc(toInteger.getWidth())
                                           : number.sextOrTrunc(toInteger.getWidth());
            return mlir::IntegerAttr::get(target, result);
        }
        const auto toFloat = llvm::cast<mlir::FloatType>(target);
        llvm::APFloat result(toFloat.getFloatSemantics());
        const auto status =
            result.convertFromAPInt(number, !isUnsigned, llvm::APFloat::rmNearestTiesToEven);
        return status == llvm::APFloat::opOK && result.isFinite()
                   ? mlir::FloatAttr::get(target, result)
                   : mlir::Attribute{};
    }
    auto number = llvm::dyn_cast<mlir::FloatAttr>(value);
    if (!number || !number.getValue().isFinite()) { return {}; }
    if (toInteger) {
        if (toInteger.getWidth() == 1) {
            return mlir::IntegerAttr::get(target, !number.getValue().isZero());
        }
        llvm::APSInt result(toInteger.getWidth(), toInteger.isUnsigned());
        bool exact = false;
        const auto status =
            number.getValue().convertToInteger(result, llvm::APFloat::rmTowardZero, &exact);
        return status == llvm::APFloat::opOK && exact ? mlir::IntegerAttr::get(target, result)
                                                      : mlir::Attribute{};
    }
    const auto toFloat = llvm::cast<mlir::FloatType>(target);
    auto result = number.getValue();
    bool losesInfo = false;
    const auto status =
        result.convert(toFloat.getFloatSemantics(), llvm::APFloat::rmNearestTiesToEven, &losesInfo);
    return status == llvm::APFloat::opOK && !losesInfo && result.isFinite()
               ? mlir::FloatAttr::get(target, result)
               : mlir::Attribute{};
}

} // namespace ctcompile::cpp::constexpr_detail
