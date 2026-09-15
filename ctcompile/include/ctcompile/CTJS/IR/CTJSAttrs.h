#pragma once
// Attribute definitions. Empty until something needs one; see CTJSAttrs.td.
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"

#include "ctcompile/CTJS/IR/CTJSEnums.h"

#define GET_ATTRDEF_CLASSES
#include "ctcompile/CTJS/IR/CTJSAttrs.h.inc"

#include <cmath>

namespace ctcompile::ctjs {

// A literal of one of the five primitive kinds the native tiers carry: the
// attribute a `ctjs.constant` holds when its value is not an object.
inline bool isPrimitiveAttr(mlir::Attribute attr) {
    return llvm::isa<NumberAttr, BooleanAttr, StringAttr, NullAttr, UndefinedAttr>(attr);
}

// ECMAScript SameValueZero on numbers: both zeros are the same key, and so are
// all NaN encodings. The predicate Map and Set key on.
inline bool sameValueZero(double a, double b) {
    return a == b || (std::isnan(a) && std::isnan(b));
}

} // namespace ctcompile::ctjs
