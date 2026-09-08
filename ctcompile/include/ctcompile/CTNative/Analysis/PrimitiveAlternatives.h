#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"

#include <cmath>
#include <optional>

namespace ctcompile::ctnative {

// A closed primitive set, partitioned by truthiness. This is semantic evidence,
// independent of native types and storage schemas. Unknown is not an empty set:
// filtering an unproved value never makes it a proved primitive. An empty arm
// can describe the tested SSA value, but never skips that arm's effect proof.
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

    bool operator==(const PrimitiveAlternatives & other) const {
        return truthy == other.truthy && falsy == other.falsy && known == other.known;
    }

    // A formal parameter records the categories supplied by every current
    // call, not the truth value of any startup argument. Future calls retain
    // both truthiness arms of each permitted category.
    PrimitiveAlternatives categories() const {
        const auto mask = truthy | falsy;
        return {mask & (Boolean | Number | String), mask, known};
    }

    static PrimitiveAlternatives forTag(mlir::TypeID tag) {
        unsigned mask = tag == mlir::TypeID::get<ctjs::BooleanAttr>()     ? Boolean
                        : tag == mlir::TypeID::get<ctjs::NumberAttr>()    ? Number
                        : tag == mlir::TypeID::get<ctjs::StringAttr>()    ? String
                        : tag == mlir::TypeID::get<ctjs::NullAttr>()      ? Null
                        : tag == mlir::TypeID::get<ctjs::UndefinedAttr>() ? Undefined
                                                                          : 0;
        return {mask & (Boolean | Number | String), mask, mask != 0};
    }

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

    PrimitiveAlternatives filtered(bool branch) const {
        return {branch ? truthy : 0, branch ? 0 : falsy, known};
    }

    PrimitiveAlternatives joined(PrimitiveAlternatives other) const {
        if (!known || !other.known) { return {}; }
        return {truthy | other.truthy, falsy | other.falsy, true};
    }

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
