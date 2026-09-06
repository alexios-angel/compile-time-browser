#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative::lowering_detail {
inline constexpr llvm::StringLiteral kObjectValueHelpers = R"cpp(
namespace ctnative {
// ctcompile: an owning property-free identity or an exact tagged scalar
struct object_value {
    nullable_scalar scalar;
    std::shared_ptr<identity_object> object;
    object_value() = default;
    object_value(nullable_scalar value) : scalar(value) {}
    object_value(double value) : scalar(value) {}
    object_value(bool value) : scalar(value) {}
    object_value(std::shared_ptr<identity_object> value) : object(std::move(value)) {}
};
inline object_value to_object_value(object_value value) { return value; }
inline bool object_truthy(const object_value & value) {
    return value.object || scalar_truthy(value.scalar);
}
inline bool object_strict_equal(const object_value & left, const object_value & right) {
    if (left.object || right.object) { return left.object == right.object; }
    return scalar_strict_equal(left.scalar, right.scalar);
}
inline bool object_equal(const object_value & left, const object_value & right) {
    // Admission excludes any pair that could invoke object-to-primitive conversion.
    if (left.object || right.object) { return left.object == right.object; }
    return scalar_equal(left.scalar, right.scalar);
}
inline std::string object_typeof(const object_value & value) {
    return value.object ? "object" : scalar_typeof(value.scalar);
}
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kObjectMapHelpers = R"cpp(
namespace ctnative {
// ctcompile: absent lookups retain undefined, saved values retain their owner
template <class K> object_value map_get(
    const std::shared_ptr<map_storage<K, object_value>> & map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    return {};
}
} // namespace ctnative
)cpp";
} // namespace ctcompile::ctnative::lowering_detail
