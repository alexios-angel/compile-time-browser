//===- NativeMapHelpers.h - standalone C++ emitted for proved Maps --------===//
#ifndef CTCOMPILE_CTNATIVE_LOWERING_NATIVEMAPHELPERS_H
#define CTCOMPILE_CTNATIVE_LOWERING_NATIVEMAPHELPERS_H

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

// Linear lookup follows the reference's insertion-ordered entry list.
// No callbacks or iterators are admitted, so mutation cannot invalidate a
// live traversal. keys()/values() are snapshots, as in ctbrowser today.
inline constexpr llvm::StringLiteral kNativeMapHelpers = R"cpp(
// ctcompile: proved primitive keys, numeric values, owning Map identity
namespace ctnative {
template <class K> struct number_map {
    std::vector<std::pair<K, double>> entries;
};
template <class K> bool map_key_equal(const K & a, const K & b) {
    return a == b;
}
inline bool map_key_equal(double a, double b) {
    return a == b || (std::isnan(a) && std::isnan(b));
}
template <class K> std::shared_ptr<number_map<K>> make_number_map() {
    return std::make_shared<number_map<K>>();
}
template <class K> bool map_has(const std::shared_ptr<number_map<K>> & map, const K & key) {
    for (const auto & entry : map->entries) {
        if (map_key_equal(entry.first, key)) { return true; }
    }
    return false;
}
template <class K> double map_get(const std::shared_ptr<number_map<K>> & map, const K & key) {
    for (const auto & entry : map->entries) {
        if (map_key_equal(entry.first, key)) { return entry.second; }
    }
    return NAN;
}
template <class K> std::shared_ptr<number_map<K>> map_set(
    const std::shared_ptr<number_map<K>> & map, const K & key, double value) {
    for (auto & entry : map->entries) {
        if (map_key_equal(entry.first, key)) {
            entry.second = value;
            return map;
        }
    }
    map->entries.emplace_back(key, value);
    return map;
}
template <class K> bool map_delete(const std::shared_ptr<number_map<K>> & map, const K & key) {
    for (auto it = map->entries.begin(); it != map->entries.end(); ++it) {
        if (map_key_equal(it->first, key)) {
            map->entries.erase(it);
            return true;
        }
    }
    return false;
}
template <class K> void map_clear(const std::shared_ptr<number_map<K>> & map) {
    map->entries.clear();
}
template <class K> double map_size(const std::shared_ptr<number_map<K>> & map) {
    return static_cast<double>(map->entries.size());
}
template <class K> std::vector<double> map_values(const std::shared_ptr<number_map<K>> & map) {
    std::vector<double> out;
    out.reserve(map->entries.size());
    for (const auto & entry : map->entries) { out.push_back(entry.second); }
    return out;
}
inline std::vector<double> map_keys(const std::shared_ptr<number_map<double>> & map) {
    std::vector<double> out;
    out.reserve(map->entries.size());
    for (const auto & entry : map->entries) { out.push_back(entry.first); }
    return out;
}
} // namespace ctnative
)cpp";

} // namespace ctcompile::ctnative
#endif
