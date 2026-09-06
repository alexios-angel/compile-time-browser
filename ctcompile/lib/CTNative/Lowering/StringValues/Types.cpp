#include "../LoweringSupport.h"

namespace ctcompile::ctnative::lowering_detail {

bool isNullableStringCarrier(mlir::Type type) {
    if (!type) { return false; }
    if (auto lvalue = llvm::dyn_cast<ec::LValueType>(type)) { type = lvalue.getValueType(); }
    auto opaque = llvm::dyn_cast<ec::OpaqueType>(type);
    return opaque && opaque.getValue() == kNullableStringType;
}

bool isStringCarrier(carrier value) {
    return value == carrier::string || value == carrier::nullableString;
}

bool isVectorCarrier(carrier value) {
    return value == carrier::vector || value == carrier::stringVector;
}

bool stringConcatenation(mlir::Type left, mlir::Type right) {
    const auto a = carrierOf(left), b = carrierOf(right);
    // Generic + only concatenates when at least one operand is definitely a
    // string: two optional strings could both contain undefined.
    return (a == carrier::string && isStringCarrier(b)) ||
           (b == carrier::string && isStringCarrier(a));
}

bool stringEquality(mlir::Type left, mlir::Type right) {
    const auto a = carrierOf(left), b = carrierOf(right);
    return (isStringCarrier(a) && (isStringCarrier(b) || onlyAbsent(right))) ||
           (isStringCarrier(b) && onlyAbsent(left));
}

} // namespace ctcompile::ctnative::lowering_detail
