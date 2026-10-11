//===- Object.hpp - The native JavaScript Object ----------------*- C++ -*-===//
//
// The Object constructor's static methods that generated native code calls,
// spelled as JavaScript spells them: `ctnative::Object.keys(o)`. Each method
// is an overload set over the native forms an object can take; today that is
// a dictionary (Dictionary.hpp, an object used as a string-keyed table).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "StaticTable.hpp"
#include "String.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace ctnative {

template <class V> class dictionary;

/// A constant table (StaticTable.hpp) whose fields all have one carrier. The
/// emitted struct names its keys in definition order, `static constexpr
/// std::array<std::string_view, N> keys`, and its values in the same order,
/// `std::array<value_type, N> values() const`.
template <class Table>
concept uniform_table = requires(const Table & table) {
    typename Table::value_type;
    { Table::keys.size() } -> std::convertible_to<std::size_t>;
    { table.values() };
};

/// Object.keys, Object.values and Object.entries (ECMA-262 20.1.2, through
/// EnumerableOwnProperties, 7.3.23). On a dictionary they enumerate its keys in
/// dictionary::order(), the OrdinaryOwnPropertyKeys order.
struct object_constructor {
    template <class V> std::vector<js_string> keys(const std::shared_ptr<dictionary<V>> & o) const {
        std::vector<js_string> out;
        for (const std::size_t i : o->order()) { out.emplace_back(o->at(i).first); }
        return out;
    }
    template <class V> std::vector<V> values(const std::shared_ptr<dictionary<V>> & o) const {
        std::vector<V> out;
        for (const std::size_t i : o->order()) { out.push_back(o->at(i).second); }
        return out;
    }
    template <class V>
    std::vector<std::pair<js_string, V>> entries(const std::shared_ptr<dictionary<V>> & o) const {
        std::vector<std::pair<js_string, V>> out;
        for (const std::size_t i : o->order()) {
            out.emplace_back(js_string{o->at(i).first}, o->at(i).second);
        }
        return out;
    }
};
inline constexpr object_constructor Object{};

namespace detail {
/// The table `table` points to, or the TypeError ToObject throws for
/// undefined (ECMA-262 7.1.18): the static was not stored yet.
template <class Table> const Table & table_object(static_table_ptr table) {
    if (table == nullptr) {
        throw std::invalid_argument("TypeError: Cannot convert undefined or null to object");
    }
    return *static_cast<const Table *>(table);
}
} // namespace detail

/// Object.keys, Object.values and Object.entries of a constant table, in its
/// definition order, which is OrdinaryOwnPropertyKeys order: the proof admits
/// no array-index key. Spelled with the table's struct, as
/// `ctnative::object_table<T>::entries(p)`; each answer is a fixed-size array
/// the C++ compiler can unroll a loop over.
template <uniform_table Table> struct object_table {
    static constexpr std::size_t size = Table::keys.size();
    static std::array<js_string, size> keys(static_table_ptr table) {
        (void)detail::table_object<Table>(table);
        std::array<js_string, size> out;
        for (std::size_t i = 0; i < size; ++i) {
            out[i] = js_string(Table::keys[i].data(), Table::keys[i].size());
        }
        return out;
    }
    static auto values(static_table_ptr table) {
        return detail::table_object<Table>(table).values();
    }
    static std::array<std::pair<js_string, typename Table::value_type>, size> entries(
        static_table_ptr table) {
        const auto all = detail::table_object<Table>(table).values();
        std::array<std::pair<js_string, typename Table::value_type>, size> out;
        for (std::size_t i = 0; i < size; ++i) {
            out[i] = {js_string(Table::keys[i].data(), Table::keys[i].size()), all[i]};
        }
        return out;
    }
};

} // namespace ctnative
