//===- Dictionary.hpp - Objects used as string-keyed tables -----*- C++ -*-===//
//
// ctnative::dictionary<V>, the native form of an object literal that a program
// uses only as a table: every key it reads or writes is computed, every value
// has one proved type V, and it is enumerated only by Object.keys,
// Object.values and Object.entries. Bootstrap's EventHandler keeps its
// listener registry this way (`E[uid][type][handlerUid]`).
//
// A dictionary is an object, so a variable holds a reference to it:
// generated code holds std::shared_ptr<dictionary<V>> (make_dictionary), and
// `E[e] = E[e] || {}` stores the same table it later reads back and mutates.
// Object.keys, Object.values and Object.entries of a dictionary are in
// Object.hpp. Only programs with such an object include this header.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "Object.hpp"
#include "String.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctnative {

/// Whether `key` is an array index (ECMA-262 6.1.7): the canonical decimal
/// spelling of an integer in [0, 2^32 - 2]. "01", "-0", "1e3" and
/// "4294967295" are ordinary string keys.
inline std::optional<std::uint32_t> array_index_of(std::string_view key) {
    if (key.empty() || key.size() > 10 || (key.size() > 1 && key.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), value);
    if (error != std::errc{} || end != key.data() + key.size() || value > 4294967294u) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

/// An ordinary object whose own properties are all writable, enumerable data
/// properties with values of type V, keyed by string.
///
/// ponytail: a vector searched linearly, like ordered_map_storage; the tables
/// this models hold a handful of keys. A hash index can be added beside the
/// vector when one measurably needs it.
template <class V> class dictionary {
public:
    using value_type = V;

    /// `o[key]`, or null when `o` has no own property `key` (the read gives
    /// undefined).
    V * get(std::string_view key) {
        const auto found = find(key);
        return found == entries_.end() ? nullptr : &found->second;
    }
    const V * get(std::string_view key) const { return const_cast<dictionary *>(this)->get(key); }

    /// `o[key] = value` (OrdinarySet, ECMA-262 10.1.9): replaces the value of
    /// an existing key in place, so its position in the key order stays.
    V & set(std::string_view key, V value) {
        const auto found = find(key);
        if (found != entries_.end()) {
            found->second = std::move(value);
            return found->second;
        }
        return entries_.emplace_back(std::string{key}, std::move(value)).second;
    }

    /// `key in o` for an own property.
    [[nodiscard]] bool has(std::string_view key) const { return get(key) != nullptr; }

    /// `delete o[key]` (OrdinaryDelete, ECMA-262 10.1.10). Always true: every
    /// property of a dictionary is configurable.
    bool remove(std::string_view key) {
        const auto found = find(key);
        if (found != entries_.end()) { entries_.erase(found); }
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /// The own keys in OrdinaryOwnPropertyKeys order (ECMA-262 10.1.11.1):
    /// array indices in ascending numeric order, then the other strings in
    /// the order they were created. Object.keys, Object.values and
    /// Object.entries all enumerate in this order (EnumerableOwnProperties,
    /// 7.3.23).
    [[nodiscard]] std::vector<std::size_t> order() const {
        std::vector<std::size_t> at(entries_.size());
        for (std::size_t i = 0; i < at.size(); ++i) { at[i] = i; }
        std::vector<std::optional<std::uint32_t>> index(entries_.size());
        for (std::size_t i = 0; i < at.size(); ++i) {
            index[i] = array_index_of(entries_[i].first);
        }
        // Indices first and ascending; the rest keep their creation order,
        // which stable_sort preserves among equal elements.
        std::ranges::stable_sort(at, [&](std::size_t a, std::size_t b) {
            if (!index[a] || !index[b]) { return index[a].has_value() && !index[b]; }
            return *index[a] < *index[b];
        });
        return at;
    }

    /// The key and value at a position of order().
    [[nodiscard]] const std::pair<std::string, V> & at(std::size_t i) const { return entries_[i]; }

private:
    auto find(std::string_view key) {
        return std::ranges::find_if(entries_,
                                    [&](const auto & entry) { return entry.first == key; });
    }

    std::vector<std::pair<std::string, V>> entries_;
};

/// `{}` for a dictionary: a new, empty table, held by reference.
template <class V> std::shared_ptr<dictionary<V>> make_dictionary() {
    return std::make_shared<dictionary<V>>();
}

} // namespace ctnative
