#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "llvm/ADT/bit.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace ctcompile::ctnative {

enum class PrimitiveMapKeyRelation {
    Unknown,
    Same,
    Distinct
};

struct PrimitiveMapKeyEvidence {
    std::optional<mlir::TypeID> tag;
    // Derived at the actual size read from definite presence in that runtime
    // instance. It describes the immutable numeric snapshot, not later contents.
    unsigned sizeLowerBound = 0;
    // The complete possible-key upper bound also agrees at this read. A
    // positive lower bound alone never establishes an exact cardinality.
    std::optional<unsigned> exactSize;
};

// A deterministic subset is sufficient for a lower bound. Limit candidates,
// not just accepted witnesses, so possibly aliasing keys also bound the work.
inline constexpr unsigned kMaxPrimitiveMapSizeCandidates = 64;

inline std::optional<unsigned> primitiveMapSize(mlir::Value value,
                                                PrimitiveMapKeyEvidence evidence = {}) {
    if (auto constant = value.getDefiningOp<ctjs::ConstantOp>()) {
        auto number = llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue());
        if (!number) { return std::nullopt; }
        const double size = number.getDouble();
        if (size >= 0 && size <= kMaxPrimitiveMapSizeCandidates && std::trunc(size) == size) {
            return static_cast<unsigned>(size);
        }
        return std::nullopt;
    }
    return evidence.exactSize;
}

// Only live literal/SSA identity and independently proved facts are evidence.
// Different SSA names alone never prove different runtime keys.
// Both values belong to the same IR context, so string attributes are interned
// and comparing them requires no unbounded scan of their bytes.
inline PrimitiveMapKeyRelation comparePrimitiveMapKeys(mlir::Value left, mlir::Value right,
                                                       PrimitiveMapKeyEvidence leftEvidence = {},
                                                       PrimitiveMapKeyEvidence rightEvidence = {}) {
    if (left == right) { return PrimitiveMapKeyRelation::Same; }
    // A key's primitive literal, else its exact size as a Number, and the tag
    // that attribute implies (the evidence's tag without one).
    const auto resolve = [](mlir::Value value, const PrimitiveMapKeyEvidence & evidence)
        -> std::pair<mlir::Attribute, std::optional<mlir::TypeID>> {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        mlir::Attribute literal = constant && ctjs::isPrimitiveAttr(constant.getValue())
                                      ? constant.getValue()
                                      : mlir::Attribute{};
        if (!literal && evidence.exactSize) {
            literal = ctjs::NumberAttr::get(
                value.getContext(),
                llvm::bit_cast<uint64_t>(static_cast<double>(*evidence.exactSize)));
        }
        return {literal, literal ? std::optional(literal.getTypeID()) : evidence.tag};
    };
    const auto [lhs, leftTag] = resolve(left, leftEvidence);
    const auto [rhs, rightTag] = resolve(right, rightEvidence);
    if (leftTag && rightTag && leftTag != rightTag) { return PrimitiveMapKeyRelation::Distinct; }
    const auto outsideSizeBound = [](mlir::Attribute value, unsigned lowerBound) {
        auto number = llvm::dyn_cast_if_present<ctjs::NumberAttr>(value);
        if (!number || lowerBound == 0) { return false; }
        const double key = number.getDouble();
        return key < static_cast<double>(lowerBound) || std::isnan(key);
    };
    if (outsideSizeBound(rhs, leftEvidence.sizeLowerBound) ||
        outsideSizeBound(lhs, rightEvidence.sizeLowerBound)) {
        return PrimitiveMapKeyRelation::Distinct;
    }
    if (!lhs || !rhs) { return PrimitiveMapKeyRelation::Unknown; }
    bool same = lhs == rhs;
    if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(lhs)) {
        auto other = llvm::cast<ctjs::NumberAttr>(rhs);
        // Bitwise inequality must never preserve a fact across an aliasing
        // mutation.
        same = ctjs::sameValueZero(number.getDouble(), other.getDouble());
    }
    return same ? PrimitiveMapKeyRelation::Same : PrimitiveMapKeyRelation::Distinct;
}

} // namespace ctcompile::ctnative
