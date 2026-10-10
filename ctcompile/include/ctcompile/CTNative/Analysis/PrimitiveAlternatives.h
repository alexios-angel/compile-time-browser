//===- PrimitiveAlternatives.h - Primitive kind sets ------------*- C++ -*-===//
//
// PrimitiveAlternatives: the closed set of primitive kinds (Boolean, Number,
// String, Null, Undefined) a value can hold, split by truthiness so a branch
// on the value can narrow it.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"

#include <cmath>
#include <optional>

namespace ctcompile::ctnative {

/// A closed set of primitive kinds, split into the truthy and falsy values
/// each kind can take. It describes JavaScript values, independent of native
/// types and storage schemas. "Unknown" (`known == false`) is not an empty
/// set: filtering an unproved value never makes it a proved primitive. An empty
/// arm describes the tested SSA value, but never lets a caller skip proving
/// that arm's effects.
struct PrimitiveAlternatives {
    enum : unsigned {
        Boolean = 1,
        Number = 2,
        String = 4,
        Null = 8,
        Undefined = 16
    };
    unsigned truthy = 0;
    unsigned falsy = 0;
    bool known = false;

    bool operator==(const PrimitiveAlternatives &) const = default;

    /// The kinds alone, with both truthiness arms of each Boolean, Number and
    /// String kind. A parameter records the kinds every current call passes,
    /// not the truthiness of any particular argument, because future calls may
    /// pass either.
    PrimitiveAlternatives categories() const {
        const auto mask = truthy | falsy;
        return {mask & (Boolean | Number | String), mask, known};
    }

    /// Every value of the kind whose attribute type is `tag`; unknown for any
    /// other tag.
    static PrimitiveAlternatives forTag(mlir::TypeID tag) {
        unsigned mask = tag == mlir::TypeID::get<ctjs::BooleanAttr>()     ? Boolean
                        : tag == mlir::TypeID::get<ctjs::NumberAttr>()    ? Number
                        : tag == mlir::TypeID::get<ctjs::StringAttr>()    ? String
                        : tag == mlir::TypeID::get<ctjs::NullAttr>()      ? Null
                        : tag == mlir::TypeID::get<ctjs::UndefinedAttr>() ? Undefined
                                                                          : 0;
        return {mask & (Boolean | Number | String), mask, mask != 0};
    }

    /// The single kind and truthiness of the constant `value`.
    static PrimitiveAlternatives literal(mlir::Attribute value) {
        auto result = forTag(value.getTypeID());
        bool truth = false;
        if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(value)) {
            truth = boolean.getValue();
        } else if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(value)) {
            truth = number.getDouble() != 0 && !std::isnan(number.getDouble());
        } else if (auto string = llvm::dyn_cast<ctjs::StringAttr>(value)) {
            truth = !string.getValue().empty();
        }
        return result.filtered(truth);
    }

    /// The values left on the `branch` side of a truthiness test.
    PrimitiveAlternatives filtered(bool branch) const {
        return {branch ? truthy : 0, branch ? 0 : falsy, known};
    }

    /// The union of two sets; unknown when either is unknown.
    PrimitiveAlternatives joined(PrimitiveAlternatives other) const {
        if (!known || !other.known) { return {}; }
        return {truthy | other.truthy, falsy | other.falsy, true};
    }

    /// The attribute type of the one kind the set holds, or nullopt.
    std::optional<mlir::TypeID> tag() const {
        if (!known) { return {}; }
        switch (truthy | falsy) {
        case Boolean: return mlir::TypeID::get<ctjs::BooleanAttr>();
        case Number: return mlir::TypeID::get<ctjs::NumberAttr>();
        case String: return mlir::TypeID::get<ctjs::StringAttr>();
        case Null: return mlir::TypeID::get<ctjs::NullAttr>();
        case Undefined: return mlir::TypeID::get<ctjs::UndefinedAttr>();
        default: return {};
        }
    }
};

} // namespace ctcompile::ctnative
