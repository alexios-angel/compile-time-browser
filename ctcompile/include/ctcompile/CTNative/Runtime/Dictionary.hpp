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

#include "Error.hpp"
#include "Object.hpp"
#include "String.hpp"
#include "ctnative.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
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

// --- Generated code's table operations ---------------------------------------
//
// Generated code holds a dictionary through a std::shared_ptr and calls these
// with the carriers of its keys and values; each is one JavaScript operation.
// A null pointer is a table read from another table that lacked the key:
// undefined, on which each operation throws the interpreter's TypeError.

/// ToPropertyKey (ECMA-262 7.1.19) of a String or Number key, as the text a
/// dictionary keys by. Symbol keys are refused before code is generated.
inline std::string property_key(const js_string & key) {
    return key.value();
}
inline std::string property_key(js_num key) {
    return ctbrowser::number_to_string(key.value());
}
inline std::string property_key(const number_string & key) {
    return key.holds<js_string>() ? property_key(key.text) : property_key(key.scalar);
}

/// Whether Object.prototype has an own property named `key` (ECMA-262 20.1.3
/// and B.2.2), which a key absent from an ordinary object inherits. The
/// compiler's copy of this list is admission::namesObjectPrototypeMember.
inline bool object_prototype_key(std::string_view key) {
    static constexpr std::string_view inherited[] = {
        "constructor",      "hasOwnProperty",   "isPrototypeOf",    "propertyIsEnumerable",
        "toLocaleString",   "toString",         "valueOf",          "__proto__",
        "__defineGetter__", "__defineSetter__", "__lookupGetter__", "__lookupSetter__",
    };
    return std::ranges::find(inherited, key) != std::end(inherited);
}

/// The carrier a read of a dictionary with values V answers: V or undefined.
template <class V> struct dictionary_read {};
template <> struct dictionary_read<js_num> {
    using type = nullable_scalar;
};
template <> struct dictionary_read<js_boolean_t> {
    using type = nullable_scalar;
};
template <> struct dictionary_read<js_string> {
    using type = nullable_string;
};
template <> struct dictionary_read<number_string> {
    using type = nullable_number_string;
};

/// A table of tables reads a table, or undefined as a null pointer.
template <class W> struct dictionary_read<std::shared_ptr<dictionary<W>>> {
    using type = std::shared_ptr<dictionary<W>>;
};

namespace detail {
template <class Read, class W>
Read dictionary_read_of(const std::shared_ptr<dictionary<W>> & value) {
    return value;
}
/// The TypeError a table operation on undefined throws, worded as the
/// interpreter words it.
template <class V> void require_table(const std::shared_ptr<dictionary<V>> & o, const char * what) {
    if (o == nullptr) { throw type_error(what); }
}
template <class Read> Read dictionary_read_of(const js_num & value) {
    return Read{value};
}
template <class Read> Read dictionary_read_of(const js_boolean_t & value) {
    return Read{value};
}
template <class Read> Read dictionary_read_of(const js_string & value) {
    return Read{value.value()};
}
template <class Read> Read dictionary_read_of(const number_string & value) {
    return to_nullable_number_string(value);
}
} // namespace detail

/// `o[key]` (OrdinaryGet, ECMA-262 10.1.8): the own value, or undefined. A
/// key the table lacks but Object.prototype has reads a function or an
/// object there, which no V holds; the compiler proves the key is never one,
/// and this stops the program if that proof is ever wrong.
template <class V, class K>
typename dictionary_read<V>::type dictionary_get(const std::shared_ptr<dictionary<V>> & o,
                                                 const K & key) {
    const std::string name = property_key(key);
    if (o == nullptr) {
        throw type_error("Cannot read properties of undefined (reading '" + name + "')");
    }
    if (const V * found = o->get(name)) {
        return detail::dictionary_read_of<typename dictionary_read<V>::type>(*found);
    }
    if (object_prototype_key(name)) {
        // The compiler admitted this read only for keys it proved are never
        // such a name (Admission/Dictionaries.cpp). Stop rather than answer:
        // no catch may carry on with a wrong undefined.
        print_error_line("ctnative: a table read under `" + name +
                         "`, which Object.prototype answers");
        std::abort();
    }
    return {};
}

/// `o[key] = value` (OrdinarySet, ECMA-262 10.1.9). Setting `__proto__` to a
/// primitive goes to Object.prototype's setter, which ignores it (B.2.2.1.2),
/// so no own property is made.
template <class V, class K, class T>
void dictionary_set(const std::shared_ptr<dictionary<V>> & o, const K & key, T && value) {
    std::string name = property_key(key);
    if (o == nullptr) {
        throw type_error("Cannot set properties of undefined (setting '" + name + "')");
    }
    if (name == "__proto__") { return; }
    o->set(name, V(std::forward<T>(value)));
}

/// `key in o` (HasProperty, ECMA-262 7.3.12): own, or inherited from
/// Object.prototype.
template <class V, class K>
js_boolean_t dictionary_has(const std::shared_ptr<dictionary<V>> & o, const K & key) {
    detail::require_table(o, "Right-hand side of 'in' must be an object");
    const std::string name = property_key(key);
    return js_boolean_t{o->has(name) || object_prototype_key(name)};
}

/// `t === undefined` (and `t == undefined`, since a table is never null) for
/// a table read from a table: a null pointer is undefined.
template <class V> js_boolean_t dictionary_absent(const std::shared_ptr<dictionary<V>> & t) {
    return js_boolean_t{t == nullptr};
}

/// `delete o[key]` (OrdinaryDelete): true, since every own property of a
/// table is configurable and an absent one needs no removal.
template <class V, class K>
js_boolean_t dictionary_delete(const std::shared_ptr<dictionary<V>> & o, const K & key) {
    detail::require_table(o, "Cannot convert undefined to object");
    return js_boolean_t{o->remove(property_key(key))};
}

} // namespace ctnative
