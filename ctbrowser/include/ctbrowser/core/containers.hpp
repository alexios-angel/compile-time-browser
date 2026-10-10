//===- containers.hpp - Hash map aliases ---------------------- -*- C++ -*-===//
//
// The third-party containers, aliased in one place so they can be replaced with
// std:: versions later, plus the string hashing that heterogeneous lookup
// needs.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <functional>
#include <string>
#include <string_view>

#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

namespace ctbrowser {

/// An open-addressing hash map. Used instead of std::unordered_map, whose nodes
/// make every lookup a pointer chase, for the atom table, the style intern
/// table and the VM's globals.
template <typename Key, typename Value> using flat_map = boost::unordered_flat_map<Key, Value>;

/// A name whose hash is already known. Property lookup walks a prototype chain
/// asking every level for the same name, so the hash is computed once (with
/// hash_name) and carried.
///
/// This uses Boost.Unordered's heterogeneous lookup, the mechanism that lets a
/// `string_view` find a `std::string` key: a transparent hasher may accept
/// another key type, here one that returns the hash it carries.
struct prehashed_name {
    std::string_view text;
    std::size_t hash;
};

/// The hasher for a string-keyed map that can be queried with a string_view.
/// Both string overloads hash through `string_view` on purpose: heterogeneous
/// lookup is only correct when the key types hash identically, and
/// `std::hash<std::string>` need not agree with `std::hash<std::string_view>`.
struct string_hash {
    using is_transparent = void;
    // Promises boost::unordered that the hash is already well mixed, so it
    // skips its extra mixing step. boost::hash over strings carries that
    // guarantee; libstdc++'s std::hash for strings does not.
    using is_avalanching = void;
    [[nodiscard]] std::size_t operator()(std::string_view text) const noexcept {
        return boost::hash<std::string_view>{}(text);
    }
    [[nodiscard]] std::size_t operator()(const std::string & text) const noexcept {
        return boost::hash<std::string_view>{}(text);
    }
    // A prehashed name costs nothing to hash.
    [[nodiscard]] std::size_t operator()(prehashed_name name) const noexcept { return name.hash; }
};

/// Transparent equality, which heterogeneous lookup needs beside the hasher.
/// `std::equal_to<>` cannot compare a `prehashed_name` to a `std::string`, so
/// it is spelled out. It always compares text, never hashes.
struct string_equal {
    using is_transparent = void;
    [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a == b;
    }
    [[nodiscard]] bool operator()(std::string_view a, prehashed_name b) const noexcept {
        return a == b.text;
    }
    [[nodiscard]] bool operator()(prehashed_name a, std::string_view b) const noexcept {
        return a.text == b;
    }
    [[nodiscard]] bool operator()(prehashed_name a, prehashed_name b) const noexcept {
        return a.text == b.text;
    }
};

/// The VM's per-object property index, the hottest map in the engine. Four
/// other open-addressing maps measured within ~4% of this one on a Phaser frame
/// (table in docs/performance.md), so there is no reason to switch.
template <typename Value>
using string_flat_map = boost::unordered_flat_map<std::string, Value, string_hash, string_equal>;

/// The hash a `prehashed_name` must carry: the one string_flat_map uses. Any
/// other hash makes the lookup silently never match.
[[nodiscard]] inline std::size_t hash_name(std::string_view text) noexcept {
    return string_hash{}(text);
}

} // namespace ctbrowser
