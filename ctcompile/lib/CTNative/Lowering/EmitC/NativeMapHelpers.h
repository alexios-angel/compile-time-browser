//===- NativeMapHelpers.h - standalone C++ emitted for proved Maps --------===//
#ifndef CTCOMPILE_CTNATIVE_LOWERING_NATIVEMAPHELPERS_H
#define CTCOMPILE_CTNATIVE_LOWERING_NATIVEMAPHELPERS_H

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

// Only nullable String key schemas need these comparisons. Both storage
// layouts preserve the tag before comparing owned text; absent keys neither
// coerce to their spelling nor alias the empty String.
inline constexpr llvm::StringLiteral kNativeNullableStringMapKeys = R"cpp(
namespace ctnative {
inline bool operator==(const nullable_string & a, const nullable_string & b) {
    return a.tag == b.tag && (a.tag != nullable_string::kind::string || a.value == b.value);
}
inline bool operator<(const nullable_string & a, const nullable_string & b) {
    if (a.tag != b.tag) { return a.tag < b.tag; }
    return a.tag == nullable_string::kind::string && a.value < b.value;
}
} // namespace ctnative
)cpp";

// A module with any snapshot keeps insertion order for all Map schemas.
// The facade gives the shared helpers the same operations as std::map.
inline constexpr llvm::StringLiteral kNativeOrderedMapStorage = R"cpp(
// ctcompile: insertion order is observable through Map snapshots
namespace ctnative {
template <class K> bool map_key_equal(const K & a, const K & b) {
    return a == b;
}
inline bool map_key_equal(js_num a, js_num b) {
    return a == b || (std::isnan(a) && std::isnan(b));
}
template <class... T> bool map_key_equal(const std::variant<T...> & a, const std::variant<T...> & b) {
    return std::visit([](const auto & left, const auto & right) {
        if constexpr (std::is_same_v<std::decay_t<decltype(left)>, std::decay_t<decltype(right)>>) {
            return map_key_equal(left, right);
        } else { return false; }
    }, a, b);
}
template <class K, class V> struct map_storage {
    std::vector<std::pair<K, V>> entries;
    auto find(const K & key) {
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (map_key_equal(it->first, key)) { return it; }
        }
        return entries.end();
    }
    auto end() { return entries.end(); }
    void insert_or_assign(const K & key, const V & value) {
        const auto found = find(key);
        if (found != end()) { found->second = value; }
        else { entries.emplace_back(key, value); }
    }
    std::size_t erase(const K & key) {
        const auto found = find(key);
        if (found == end()) { return 0; }
        entries.erase(found);
        return 1;
    }
    void clear() { entries.clear(); }
    std::size_t size() const { return entries.size(); }
};
} // namespace ctnative
)cpp";

// Without iteration, key order is unobservable. Numeric equivalence still
// needs SameValueZero: std::less<double> alone is not valid for NaN keys.
inline constexpr llvm::StringLiteral kNativeAssociativeMapStorage = R"cpp(
// ctcompile: no Map iteration; ordered lookup with SameValueZero keys
namespace ctnative {
template <class K> struct map_key_less {
    bool operator()(const K & a, const K & b) const { return std::less<K>{}(a, b); }
};
template <> struct map_key_less<js_num> {
    bool operator()(js_num a, js_num b) const {
        if (std::isnan(a)) { return !std::isnan(b); }
        return !std::isnan(b) && a < b;
    }
};
template <class... T> struct map_key_less<std::variant<T...>> {
    bool operator()(const std::variant<T...> & a, const std::variant<T...> & b) const {
        if (a.index() != b.index()) { return a.index() < b.index(); }
        return std::visit([](const auto & left, const auto & right) {
            using L = std::decay_t<decltype(left)>;
            if constexpr (std::is_same_v<L, std::decay_t<decltype(right)>>) {
                return map_key_less<L>{}(left, right);
            } else { return false; }
        }, a, b);
    }
};
template <class K, class V> using map_storage = std::map<K, V, map_key_less<K>>;
} // namespace ctnative
)cpp";

inline constexpr llvm::StringLiteral kNativeMapHelpers = R"cpp(
// ctcompile: primitive keys, acyclic primitive/Map payloads, owning identity
namespace ctnative {
template <class K> using number_map = map_storage<K, js_num>;
using string_to_number_map = number_map<std::string>;
template <class K, class V> std::shared_ptr<map_storage<K, V>> make_map() {
    return std::make_shared<map_storage<K, V>>();
}
template <class K> std::shared_ptr<number_map<K>> make_number_map() {
    return std::make_shared<number_map<K>>();
}
inline std::shared_ptr<string_to_number_map> make_string_to_number_map() {
    return make_number_map<std::string>();
}
template <class K, class V> bool map_has(const std::shared_ptr<map_storage<K, V>> & map, const K & key) {
    return map->find(key) != map->end();
}
template <class K> nullable_scalar map_get(const std::shared_ptr<number_map<K>> & map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    return {};
}
template <class K> nullable_scalar map_get(
    const std::shared_ptr<map_storage<K, bool>> & map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    return {};
}
template <class K, class V> V map_get_present(
    const std::shared_ptr<map_storage<K, V>> & map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    // Reaching this point contradicts the compiler's dominance/identity proof.
    std::terminate();
}
// T is selected only by a rederived present, exact-payload proof. Return
// by value so an owning string survives overwrite, deletion and Map lifetime.
template <class T, class K, class... V> T map_get_present_as(
    const std::shared_ptr<map_storage<K, std::variant<V...>>> & map, const K & key) {
    const auto found = map->find(key);
    if (found == map->end()) { std::terminate(); }
    return std::visit([](const auto & value) -> T {
        if constexpr (std::is_same_v<T, std::decay_t<decltype(value)>>) { return value; }
        else { std::terminate(); }
    }, found->second);
}
template <class K, class V> std::shared_ptr<map_storage<K, V>> map_set(
    const std::shared_ptr<map_storage<K, V>> & map, const K & key, const V & value) {
    map->insert_or_assign(key, value);
    return map;
}
template <class K, class V> bool map_delete(const std::shared_ptr<map_storage<K, V>> & map, const K & key) {
    return map->erase(key) != 0;
}
template <class K, class V> void map_clear(const std::shared_ptr<map_storage<K, V>> & map) {
    map->clear();
}
template <class K, class V> js_num map_size(const std::shared_ptr<map_storage<K, V>> & map) {
    return static_cast<js_num>(map->size());
}
} // namespace ctnative
)cpp";

// Nullable Strings also own payload storage. Every read returns a copy;
// neither a full-schema read nor an exact-tag read borrows Map storage.
inline constexpr llvm::StringLiteral kNativeStringMapHelpers = R"cpp(
namespace ctnative {
template <class K> nullable_string map_get(
    const std::shared_ptr<map_storage<K, std::string>> & map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    return {};
}
template <class K> nullable_string map_get(
    const std::shared_ptr<map_storage<K, nullable_string>> & map, const K & key) {
    const auto found = map->find(key);
    if (found != map->end()) { return found->second; }
    return {};
}
template <class T> T map_nullable_payload_as(const nullable_string & value) {
    if constexpr (std::is_same_v<T, nullable_string>) { return value; }
    else if constexpr (std::is_same_v<T, std::string>) {
        if (value.tag != nullable_string::kind::string) { std::terminate(); }
        return value.value;
    } else { std::terminate(); }
}
// The compiler supplies T only from an independent present payload fact.
// The storage schema itself never selects a narrower alternative.
template <class T, class K, class V> T map_get_present_nullable_as(
    const std::shared_ptr<map_storage<K, V>> & map, const K & key) {
    const auto found = map->find(key);
    if (found == map->end()) { std::terminate(); }
    if constexpr (std::is_same_v<V, nullable_string>) {
        return map_nullable_payload_as<T>(found->second);
    } else {
        return std::visit([](const auto & value) -> T {
            using Actual = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Actual, nullable_string>) {
                return map_nullable_payload_as<T>(value);
            } else if constexpr (std::is_same_v<T, Actual>) { return value; }
            else { std::terminate(); }
        }, found->second);
    }
}
} // namespace ctnative
)cpp";

// Kept separately so a lookup-only module does not accidentally acquire an
// iteration API whose sorted order would differ from JavaScript insertion order.
inline constexpr llvm::StringLiteral kNativeMapSnapshotHelpers = R"cpp(
namespace ctnative {
template <class K> std::vector<double> map_values(const std::shared_ptr<number_map<K>> & map) {
    std::vector<double> out;
    out.reserve(map->entries.size());
    for (const auto & entry : map->entries) { out.push_back(entry.second); }
    return out;
}
template <class K> std::vector<std::string> map_values(
    const std::shared_ptr<map_storage<K, std::string>> & map) {
    std::vector<std::string> out;
    out.reserve(map->entries.size());
    for (const auto & entry : map->entries) { out.push_back(entry.second); }
    return out;
}
template <class V> std::vector<double> map_keys(const std::shared_ptr<map_storage<double, V>> & map) {
    std::vector<double> out;
    out.reserve(map->entries.size());
    for (const auto & entry : map->entries) { out.push_back(entry.first); }
    return out;
}
template <class V> std::vector<std::string> map_keys(
    const std::shared_ptr<map_storage<std::string, V>> & map) {
    std::vector<std::string> out;
    out.reserve(map->entries.size());
    for (const auto & entry : map->entries) { out.push_back(entry.first); }
    return out;
}
} // namespace ctnative
)cpp";

} // namespace ctcompile::ctnative
#endif
