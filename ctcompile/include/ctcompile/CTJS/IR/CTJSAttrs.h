//===- CTJSAttrs.h - CTJS literal attributes --------------------*- C++ -*-===//
//
// The ctjs literal attributes (undefined, null, boolean, number, string,
// bigint), generated from CTJSAttrs.td, plus small helpers over them.
//
//===----------------------------------------------------------------------===//

#pragma once
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"

#include "ctcompile/CTJS/IR/CTJSEnums.h"

#define GET_ATTRDEF_CLASSES
#include "ctcompile/CTJS/IR/CTJSAttrs.h.inc"

#include <cmath>

namespace ctcompile::ctjs {

/// Returns true for a literal of one of the five primitive kinds the native
/// tiers carry: the attribute a `ctjs.constant` holds when its value is not an
/// object.
inline bool isPrimitiveAttr(mlir::Attribute attr) {
    return llvm::isa<NumberAttr, BooleanAttr, StringAttr, NullAttr, UndefinedAttr>(attr);
}

/// ECMAScript SameValueZero on numbers: both zeros are the same key, and so are
/// all NaN encodings. Map and Set compare keys with this predicate.
inline bool sameValueZero(double a, double b) {
    return a == b || (std::isnan(a) && std::isnan(b));
}

} // namespace ctcompile::ctjs
