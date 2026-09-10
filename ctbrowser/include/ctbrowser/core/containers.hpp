#pragma once
#include <functional>
#include <string>
#include <string_view>

#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

// The third-party containers, aliased in one place: the seam for replacing them
// with std:: versions when libstdc++ catches up.

namespace ctbrowser {

// Open addressing, and the reason it is here rather than std::unordered_map:
// the standard one is a linked list of nodes and every lookup is a pointer
// chase. These are the atom table, the style intern table and the VM's globals.
template <typename Key, typename Value> using flat_map = boost::unordered_flat_map<Key, Value>;

// A NAME WHOSE HASH IS ALREADY KNOWN. Property lookup walks a prototype chain
// asking EVERY level for the same name, and the hash cannot change between
// levels, so compute it once and carry it.
//
// This rides Boost.Unordered's HETEROGENEOUS LOOKUP, the same machinery that
// lets a `string_view` be looked up in a `std::string`-keyed map: a transparent
// hasher may accept more than one key type, so it can accept one that simply
// hands back the hash it was given. There is no lower-level "find with this
// hash" entry point to reach for, and this needs none.
struct prehashed_name {
    std::string_view text;
    std::size_t hash;
};

// The hasher for a string-keyed map that can be asked with a string_view. Both
// overloads hash through `string_view` on purpose: heterogeneous lookup is only
// correct when the two key types hash IDENTICALLY, and `std::hash<std::string>`
// is not required to agree with `std::hash<std::string_view>`.
struct string_hash {
    using is_transparent = void;
    // AVALANCHING is a promise, not a decoration: boost::unordered applies an
    // EXTRA mixing step to any hash not marked so. boost::hash mixes over
    // word-sized chunks and carries the guarantee; libstdc++'s std::hash for
    // strings walks a byte at a time and does not.
    using is_avalanching = void;
    [[nodiscard]] std::size_t operator()(std::string_view text) const noexcept {
        return boost::hash<std::string_view>{}(text);
    }
    [[nodiscard]] std::size_t operator()(const std::string & text) const noexcept {
        return boost::hash<std::string_view>{}(text);
    }
    // The whole point: free.
    [[nodiscard]] std::size_t operator()(prehashed_name name) const noexcept { return name.hash; }
};

// Transparent equality, which heterogeneous lookup needs beside the hasher.
// `std::equal_to<>` cannot compare a `prehashed_name` to a `std::string`, so it
// is spelled out - and only ever against the TEXT, because two names are equal
// when their characters are, never because their hashes agree.
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

// THE PROPERTY MAP - the VM's per-object property index, and the hottest map in
// the engine. ONE MAP, AND THE QUESTION IS CLOSED: four other open-addressing
// maps measured within ~4% of this one on a Phaser frame, and docs/performance.md
// keeps the table.
template <typename Value>
using string_flat_map = boost::unordered_flat_map<std::string, Value, string_hash, string_equal>;

// The hash a `prehashed_name` carries, computed the one way the map agrees
// with. Using anything else here is a lookup that silently never matches.
[[nodiscard]] inline std::size_t hash_name(std::string_view text) noexcept {
    return string_hash{}(text);
}

} // namespace ctbrowser
