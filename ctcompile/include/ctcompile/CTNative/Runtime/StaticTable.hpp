//===- StaticTable.hpp - Constant tables of run-once statics ----*- C++ -*-===//
//
// static_table, the base of the `const` struct ctcompile emits for an object
// literal of constants that a run-once function keeps in a static variable,
// and table_field, which reads one of its fields. The variable is a
// static_table_ptr: null while it is still undefined, then the address of the
// one struct, so every reader sees the same object.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Error.hpp"
#include "String.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ctnative {

/// The base of every emitted constant table. Empty: it only lets one pointer
/// type stand for every table, while each read names its concrete struct.
struct static_table {};

/// A run-once static that holds a constant table, or a copy of one. The alias
/// gives the pointer a name EmitC's opaque types accept.
using static_table_ptr = const static_table *;

namespace detail {
template <class Member> struct table_of_member;
template <class Table, class Field> struct table_of_member<Field Table::*> {
    using type = Table;
};
} // namespace detail

/// Reads `Field` (`&ctn_x::key`), the property `key` names, of the table
/// `table` points to. A property read of undefined throws a TypeError
/// (GetValue calls ToObject, ECMA-262 7.1.18) with the interpreter's message,
/// and a null `table` is a static the run-once function has not stored yet.
template <auto Field> const auto & table_field(static_table_ptr table, const char * key) {
    using concrete = typename detail::table_of_member<decltype(Field)>::type;
    if (table == nullptr) {
        throw type_error(std::string{"Cannot read properties of undefined (reading '"} + key +
                         "')");
    }
    return static_cast<const concrete *>(table)->*Field;
}

/// Reads `Field` of the table `table` points to for an object spread
/// (`{...table}`), as a `Result` (a nullable carrier such as nullable_scalar).
/// CopyDataProperties (ECMA-262 7.3.25) copies nothing from undefined, so a
/// null `table` gives an undefined `Result` instead of throwing.
template <auto Field, class Result> Result table_field_or_undefined(static_table_ptr table) {
    if (table == nullptr) { return Result{}; }
    // The table is stored, so the key a read of undefined names is unused.
    const auto & value = table_field<Field>(table, "");
    if constexpr (std::is_constructible_v<Result, decltype(value)>) {
        return Result(value);
    } else {
        // A String field into a nullable_string, found by argument-dependent
        // lookup in ctnative.hpp.
        return to_nullable_string(value);
    }
}

/// A constant table whose fields all have one carrier. The emitted struct
/// names its keys in definition order, `static constexpr
/// std::array<std::string_view, N> keys`, and its values in the same order,
/// `std::array<value_type, N> values() const`.
template <class Table>
concept uniform_table = requires(const Table & table) {
    typename Table::value_type;
    { Table::keys.size() } -> std::convertible_to<std::size_t>;
    { table.values() };
};

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

/// Object.keys, Object.values and Object.entries (ECMA-262 20.1.2.18, .22,
/// .5) of a constant table, in its definition order, which is
/// OrdinaryOwnPropertyKeys order: the proof admits no array-index key.
/// Spelled with the table's struct, `ctnative::object_table<T>::entries(p)`.
/// Each answer is an element vector whose size is known at compile time, so
/// the C++ compiler can unroll a loop over it.
template <uniform_table Table> struct object_table {
    static constexpr std::size_t size = Table::keys.size();
    static std::vector<js_string> keys(static_table_ptr table) {
        (void)detail::table_object<Table>(table);
        std::vector<js_string> out;
        out.reserve(size);
        for (const std::string_view key : Table::keys) { out.emplace_back(key.data(), key.size()); }
        return out;
    }
    static std::vector<typename Table::value_type> values(static_table_ptr table) {
        const auto all = detail::table_object<Table>(table).values();
        return {all.begin(), all.end()};
    }
    static std::vector<std::pair<js_string, typename Table::value_type>> entries(
        static_table_ptr table) {
        const auto all = detail::table_object<Table>(table).values();
        std::vector<std::pair<js_string, typename Table::value_type>> out;
        out.reserve(size);
        for (std::size_t i = 0; i < size; ++i) {
            out.emplace_back(js_string(Table::keys[i].data(), Table::keys[i].size()), all[i]);
        }
        return out;
    }
};

} // namespace ctnative
