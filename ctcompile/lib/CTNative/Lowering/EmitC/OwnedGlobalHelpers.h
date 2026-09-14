#pragma once

namespace ctcompile::ctnative::lowering_detail {
inline constexpr char kOwnedGlobalHelpers[] = R"cpp(
namespace ctnative {
// ctcompile: read a field of a source-proved owned global
template <auto Field, class T>
auto owned_global_get(std::shared_ptr<T> const & object) {
    return object.get()->*Field;
}
// ctcompile: write a field of a source-proved owned global
template <auto Field, class T, class V>
void owned_global_set(std::shared_ptr<T> const & object, V value) {
    object.get()->*Field = value;
}
template <auto Field, class T>
auto owned_global_get(T * object) { return object->*Field; }
template <auto Field, class T, class V>
void owned_global_set(T * object, V value) { object->*Field = value; }
template <class Table>
auto data_map(Table * table) { return &table->captured_map; }
} // namespace ctnative
)cpp";
} // namespace ctcompile::ctnative::lowering_detail
