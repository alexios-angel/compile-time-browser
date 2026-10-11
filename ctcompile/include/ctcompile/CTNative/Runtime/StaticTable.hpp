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

#include <array>
#include <stdexcept>
#include <string>
#include <type_traits>

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
    // Not null here, so the key the error would name is never read.
    const auto & value = table_field<Field>(table, "");
    if constexpr (std::is_constructible_v<Result, decltype(value)>) {
        return Result(value);
    } else {
        // A String field into a nullable_string, found by argument-dependent
        // lookup in ctnative.hpp.
        return to_nullable_string(value);
    }
}

} // namespace ctnative
