//===- NativeMapHelpers.h - standalone C++ emitted for proved Maps --------===//
#ifndef CTCOMPILE_CTNATIVE_LOWERING_NATIVEMAPHELPERS_H
#define CTCOMPILE_CTNATIVE_LOWERING_NATIVEMAPHELPERS_H

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

// Linear lookup follows the reference's insertion-ordered entry list.
// No callbacks or iterators are admitted, so mutation cannot invalidate a
// live traversal. keys()/values() are snapshots, as in ctbrowser today.
inline constexpr llvm::StringLiteral kNativeMapHelpers = R"cpp(
// ctcompile: primitive keys, acyclic numeric/Map payloads, owning identity
namespace ctnative {
template <class K, class V> struct map_storage {
    std::vector<std::pair<K, V>> entries;
};
template <class K> using number_map = map_storage<K, double>;
template <class K, class V> std::shared_ptr<map_storage<K, V>> make_map() {
    return std::make_shared<map_storage<K, V>>();
}
template <class K> bool map_key_equal(const K & a, const K & b) {
    return a == b;
}
inline bool map_key_equal(double a, double b) {
    return a == b || (std::isnan(a) && std::isnan(b));
}
template <class K> std::shared_ptr<number_map<K>> make_number_map() {
    return std::make_shared<number_map<K>>();
}
template <class K, class V> bool map_has(const std::shared_ptr<map_storage<K, V>> & map, const K & key) {
    for (const auto & entry : map->entries) {
        if (map_key_equal(entry.first, key)) { return true; }
    }
    return false;
}
template <class K> nullable_scalar map_get(const std::shared_ptr<number_map<K>> & map, const K & key) {
    for (const auto & entry : map->entries) {
        if (map_key_equal(entry.first, key)) { return entry.second; }
    }
    return {};
}
template <class K, class V> V map_get_present(
    const std::shared_ptr<map_storage<K, V>> & map, const K & key) {
    for (const auto & entry : map->entries) {
        if (map_key_equal(entry.first, key)) { return entry.second; }
    }
    // Reaching this point contradicts the compiler's dominance/identity proof.
    std::terminate();
}
template <class K, class V> std::shared_ptr<map_storage<K, V>> map_set(
    const std::shared_ptr<map_storage<K, V>> & map, const K & key, const V & value) {
    for (auto & entry : map->entries) {
        if (map_key_equal(entry.first, key)) {
            entry.second = value;
            return map;
        }
    }
    map->entries.emplace_back(key, value);
    return map;
}
template <class K, class V> bool map_delete(const std::shared_ptr<map_storage<K, V>> & map, const K & key) {
    for (auto it = map->entries.begin(); it != map->entries.end(); ++it) {
        if (map_key_equal(it->first, key)) {
            map->entries.erase(it);
            return true;
        }
    }
    return false;
}
template <class K, class V> void map_clear(const std::shared_ptr<map_storage<K, V>> & map) {
    map->entries.clear();
}
template <class K, class V> double map_size(const std::shared_ptr<map_storage<K, V>> & map) {
    return static_cast<double>(map->entries.size());
}
template <class K> std::vector<double> map_values(const std::shared_ptr<number_map<K>> & map) {
    std::vector<double> out;
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
} // namespace ctnative
)cpp";

} // namespace ctcompile::ctnative
#endif
