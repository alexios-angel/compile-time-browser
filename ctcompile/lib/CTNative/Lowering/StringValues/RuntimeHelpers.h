#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative::lowering_detail {

inline constexpr llvm::StringLiteral kNullableStringHelpers = R"cpp(
namespace ctnative {
// ctcompile: owning strings retain separate null and undefined tags
struct nullable_string {
    enum class kind { undefined, null_value, string };
    kind tag = kind::undefined;
    std::string value;
    nullable_string() = default;
    nullable_string(const std::string & text) : tag(kind::string), value(text) {}
};
inline nullable_string to_nullable_string(const std::string & value) { return value; }
inline nullable_string to_nullable_string(const nullable_string & value) { return value; }
inline nullable_string to_nullable_string(nullable_scalar value) {
    nullable_string out;
    if (value.tag == nullable_scalar::kind::null) {
        out.tag = nullable_string::kind::null_value;
    } else if (value.tag != nullable_scalar::kind::undefined) {
        // Admission only permits an absent scalar to widen into this carrier.
        std::terminate();
    }
    return out;
}
inline bool string_truthy(const nullable_string & value) {
    return value.tag == nullable_string::kind::string && !value.value.empty();
}
inline std::string string_typeof(const nullable_string & value) {
    return value.tag == nullable_string::kind::undefined ? "undefined"
           : value.tag == nullable_string::kind::null_value ? "object" : "string";
}
inline std::string string_text(const nullable_string & value) {
    return value.tag == nullable_string::kind::undefined ? "undefined"
           : value.tag == nullable_string::kind::null_value ? "null" : value.value;
}
template <class L, class R> bool string_strict_equal(const L & left, const R & right) {
    const auto a = to_nullable_string(left), b = to_nullable_string(right);
    return a.tag == b.tag && (a.tag != nullable_string::kind::string || a.value == b.value);
}
template <class L, class R> bool string_equal(const L & left, const R & right) {
    const auto a = to_nullable_string(left), b = to_nullable_string(right);
    if (a.tag != nullable_string::kind::string && b.tag != nullable_string::kind::string) {
        return true;
    }
    return string_strict_equal(a, b);
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kStringVectorHelpers = R"cpp(
// ctcompile: confined owning Map string-key snapshots
namespace ctnative {
inline nullable_string vec_at(const std::vector<std::string> & values, nullable_scalar key) {
    if (key.tag != nullable_scalar::kind::number) { return {}; }
    const double index = std::trunc(key.value);
    if (!(index >= 0.0) || index >= static_cast<double>(values.size())) { return {}; }
    return values[static_cast<std::vector<std::string>::size_type>(index)];
}
inline double vec_length(const std::vector<std::string> & values) {
    return static_cast<double>(values.size());
}
} // namespace ctnative
)cpp";

} // namespace ctcompile::ctnative::lowering_detail
