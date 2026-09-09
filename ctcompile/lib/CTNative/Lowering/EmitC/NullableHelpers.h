#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative::lowering_detail {

// Opt and scalar-only Variant types share a carrier; the runtime tag preserves
// which JavaScript value arrived. A present NaN is never an absence sentinel.
inline constexpr llvm::StringLiteral kNullableHelpers = R"cpp(
namespace ctnative {
// ctcompile: optional scalar values preserve null, undefined and present NaN
struct nullable_scalar {
    enum class kind { undefined, null, number, boolean } tag = kind::undefined;
    double value = 0;
    nullable_scalar() = default;
    nullable_scalar(double number) : tag(kind::number), value(number) {}
    nullable_scalar(bool boolean) : tag(kind::boolean), value(boolean ? 1.0 : 0.0) {}
    static nullable_scalar null() {
        nullable_scalar result;
        result.tag = kind::null;
        return result;
    }
};
inline nullable_scalar to_nullable(nullable_scalar value) { return value; }
inline double to_number(nullable_scalar value) {
    if (value.tag == nullable_scalar::kind::undefined) { return NAN; }
    if (value.tag == nullable_scalar::kind::null) { return 0.0; }
    return value.value;
}
// Numeric global admission is a proof about the stored tag. Check it at the
// observation boundary so a missing generated store cannot imitate a NaN.
inline double global_number(nullable_scalar value) {
    if (value.tag != nullable_scalar::kind::number) { std::terminate(); }
    return value.value;
}
inline bool global_boolean(nullable_scalar value) {
    if (value.tag != nullable_scalar::kind::boolean) { std::terminate(); }
    return value.value != 0.0;
}
inline bool scalar_truthy(nullable_scalar value) {
    return (value.tag == nullable_scalar::kind::number ||
            value.tag == nullable_scalar::kind::boolean) &&
           value.value != 0.0 && !std::isnan(value.value);
}
inline bool scalar_strict_equal(nullable_scalar left, nullable_scalar right) {
    if (left.tag != right.tag) { return false; }
    if (left.tag == nullable_scalar::kind::undefined ||
        left.tag == nullable_scalar::kind::null) { return true; }
    return left.value == right.value;
}
inline bool scalar_equal(nullable_scalar left, nullable_scalar right) {
    const bool lnull = left.tag == nullable_scalar::kind::undefined ||
                       left.tag == nullable_scalar::kind::null;
    const bool rnull = right.tag == nullable_scalar::kind::undefined ||
                       right.tag == nullable_scalar::kind::null;
    if (lnull || rnull) { return lnull && rnull; }
    return left.value == right.value;
}
inline std::string scalar_typeof(nullable_scalar value) {
    switch (value.tag) {
    case nullable_scalar::kind::undefined: return "undefined";
    case nullable_scalar::kind::null: return "object";
    case nullable_scalar::kind::boolean: return "boolean";
    case nullable_scalar::kind::number: return "number";
    }
    std::terminate();
}
} // namespace ctnative
)cpp";

} // namespace ctcompile::ctnative::lowering_detail
