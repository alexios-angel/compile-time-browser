//===- Types.cpp - String carrier predicates ------------------------------===//
//
// Carrier predicates for native Strings: which carriers hold a String, and
// when `+` and `==` on two carriers lower to the String helpers.
//
//===----------------------------------------------------------------------===//

#include "../LoweringSupport.h"

namespace ctcompile::ctnative::lowering_detail {

bool isStringCarrier(carrier value) {
    return value == carrier::string || value == carrier::nullableString;
}

bool isVectorCarrier(carrier value) {
    return value == carrier::vector || value == carrier::stringVector;
}

/// Whether `left + right` is a String concatenation of two String carriers.
bool stringConcatenation(mlir::Type left, mlir::Type right) {
    const auto a = carrierOf(left), b = carrierOf(right);
    // Generic + only concatenates when at least one operand is definitely a
    // string: two optional strings could both contain undefined.
    return (a == carrier::string && isStringCarrier(b)) ||
           (b == carrier::string && isStringCarrier(a));
}

/// Whether `left == right` compares two primitives at least one of which may
/// be a String.
bool stringEquality(mlir::Type left, mlir::Type right) {
    const auto a = carrierOf(left), b = carrierOf(right);
    // Equality may convert primitives, but must never invoke an unproved
    // object hook (valueOf/toString), so both sides must be primitive.
    return isPrimitiveCarrier(a) && isPrimitiveCarrier(b) &&
           (isStringCarrier(a) || isStringUnionCarrier(a) || isStringCarrier(b) ||
            isStringUnionCarrier(b));
}

} // namespace ctcompile::ctnative::lowering_detail
