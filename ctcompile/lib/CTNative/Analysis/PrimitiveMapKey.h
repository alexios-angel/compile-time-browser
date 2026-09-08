#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"

#include <cmath>
#include <optional>

namespace ctcompile::ctnative {

enum class PrimitiveMapKeyRelation {
    Unknown,
    Same,
    Distinct
};

// Only live literal/SSA identity and independently proved primitive tags are
// evidence. Different SSA names alone never prove different runtime keys.
// Both values belong to the same IR context, so string attributes are interned
// and comparing them requires no unbounded scan of their bytes.
inline PrimitiveMapKeyRelation comparePrimitiveMapKeys(mlir::Value left, mlir::Value right,
                                                       std::optional<mlir::TypeID> leftTag = {},
                                                       std::optional<mlir::TypeID> rightTag = {}) {
    if (left == right) { return PrimitiveMapKeyRelation::Same; }
    const auto literal = [](mlir::Value value) -> mlir::Attribute {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        if (!constant || !llvm::isa<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::StringAttr,
                                    ctjs::NullAttr, ctjs::UndefinedAttr>(constant.getValue())) {
            return {};
        }
        return constant.getValue();
    };
    auto lhs = literal(left), rhs = literal(right);
    if (lhs) { leftTag = lhs.getTypeID(); }
    if (rhs) { rightTag = rhs.getTypeID(); }
    if (leftTag && rightTag && leftTag != rightTag) { return PrimitiveMapKeyRelation::Distinct; }
    if (!lhs || !rhs) { return PrimitiveMapKeyRelation::Unknown; }
    bool same = lhs == rhs;
    if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(lhs)) {
        auto other = llvm::cast<ctjs::NumberAttr>(rhs);
        // SameValueZero equates both zeros and all NaN encodings. Bitwise
        // inequality must never preserve a fact across an aliasing mutation.
        const double a = number.getDouble(), b = other.getDouble();
        same = a == b || (std::isnan(a) && std::isnan(b));
    }
    return same ? PrimitiveMapKeyRelation::Same : PrimitiveMapKeyRelation::Distinct;
}

} // namespace ctcompile::ctnative
