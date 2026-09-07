#pragma once

namespace ctcompile::ctnative::lowering_detail {
inline constexpr char kOwnedGlobalHelpers[] = R"cpp(
namespace ctnative {
template <auto Field, class T>
auto owned_global_get(std::shared_ptr<T> const & object) {
    return object.get()->*Field;
}
template <auto Field, class T, class V>
void owned_global_set(std::shared_ptr<T> const & object, V value) {
    object.get()->*Field = value;
}
} // namespace ctnative
)cpp";
} // namespace ctcompile::ctnative::lowering_detail
