//===- PrimitiveMapKey.h - Compile-time Map key comparison ------*- C++ -*-===//
//
// Compares two primitive native Map keys at compile time (same, distinct or
// unknown), using literal values and facts about `size` reads.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "llvm/ADT/bit.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace ctcompile::ctnative {

/// How two Map keys relate at runtime, as far as the compiler can prove.
enum class PrimitiveMapKeyRelation {
    Unknown,
    Same,
    Distinct
};

/// Facts about a key value that may help compare it.
struct PrimitiveMapKeyEvidence {
    /// The key's primitive type (a ctjs attribute TypeID), if known.
    std::optional<mlir::TypeID> tag;
    /// For a key that is the result of a `size` read: the number of keys known
    /// to be present at that read, so the Number is at least this. It
    /// describes the value read, not the Map's later contents.
    unsigned sizeLowerBound = 0;
    /// For a `size` result: its exact value, known when the Map's possible
    /// keys are exactly its present keys at that read. A positive lower bound
    /// alone never proves an exact size.
    std::optional<unsigned> exactSize;
};

/// The most keys examined when computing a size bound. Any subset gives a
/// valid lower bound, so the cap counts every key looked at, not only the
/// ones found distinct, to bound the work.
inline constexpr unsigned kMaxPrimitiveMapSizeCandidates = 64;

/// The value of `value` as a small non-negative integer size: a Number literal
/// up to kMaxPrimitiveMapSizeCandidates, else `evidence.exactSize`.
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

/// Compares two keys using only SSA identity, literal values and the proved
/// facts in the evidence. Different SSA values alone never prove different
/// runtime keys. Both values belong to the same IR context, so string
/// attributes are interned and comparing them needs no scan of their bytes.
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
        // Compare numbers with SameValueZero (Map key equality), not bit
        // patterns: +0/-0 and distinct NaN payloads are the same key.
        same = ctjs::sameValueZero(number.getDouble(), other.getDouble());
    }
    return same ? PrimitiveMapKeyRelation::Same : PrimitiveMapKeyRelation::Distinct;
}

} // namespace ctcompile::ctnative
