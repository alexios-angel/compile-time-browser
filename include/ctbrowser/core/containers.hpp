#pragma once
#include <functional>
#include <string>
#include <string_view>

#include <boost/container_hash/hash.hpp>
// SET BEFORE THE INCLUDES BELOW, which is the only place it can go: an
// undefined macro evaluates to 0 in an #if, so defining the default further
// down silently selected NO include and then asked for a type nothing had
// declared.
#ifndef CTBROWSER_STRING_MAP
#define CTBROWSER_STRING_MAP 2
#endif

#if CTBROWSER_STRING_MAP == 1
#include <ankerl/unordered_dense.h>
#elif CTBROWSER_STRING_MAP == 2
#include <tsl/robin_map.h>
#elif CTBROWSER_STRING_MAP == 3
#include <absl/container/flat_hash_map.h>
#elif CTBROWSER_STRING_MAP == 4
#include <gtl/phmap.hpp>
#endif
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

// The third-party containers, aliased in one place.
//
// This used to be a correctness requirement rather than tidiness: under C++
// modules, a header defining `static inline` functions - boost::unordered's
// size_index_for is one - got internal linkage in every module that included it
// in its global module fragment, and a translation unit importing two such
// modules saw two definitions with the same mangled name. clang rejected it,
// and only once enough modules were combined. Plain headers with an include
// guard are textually included once per translation unit, so that failure mode
// is gone with the modules that caused it.
//
// The file stays because the aliases are the seam for replacing these with
// std:: versions when libstdc++ catches up - flat_map is std::flat_map's shape,
// and small_vector is std::inplace_vector's neighbour.

namespace ctbrowser {

// Open addressing, and the reason it is here rather than std::unordered_map:
// the standard one is a linked list of nodes and every lookup is a pointer
// chase. These are the atom table, the style intern table and the VM's globals.
template <typename Key, typename Value> using flat_map = boost::unordered_flat_map<Key, Value>;

template <typename Key> using flat_set = boost::unordered_flat_set<Key>;

// A STRING-KEYED MAP THAT CAN BE ASKED WITH A string_view.
//
// `flat_map<std::string, V>::find` takes the key type, so looking one up with a
// `string_view` means building a `std::string` to throw away - an allocation
// for anything past the small-string buffer, and a copy for everything else, on
// every single lookup. `object_object::find` did exactly that, and property
// lookup is 10.7% of a Phaser frame (docs/performance.md).
//
// Both overloads hash through `string_view` on purpose. Heterogeneous lookup is
// only correct when the two key types hash IDENTICALLY, and
// `std::hash<std::string>` is not required to agree with
// `std::hash<std::string_view>` - converting first makes them agree by
// construction rather than by hoping.
// A NAME WHOSE HASH IS ALREADY KNOWN.
//
// Property lookup walks a prototype chain asking EVERY level for the same name
// - 2.25 levels on average in a Phaser frame - and each `find` hashed the
// string again. The hash cannot change between levels, so compute it once and
// carry it.
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

struct string_hash {
    using is_transparent = void;
    // AVALANCHING, and this is a promise rather than a decoration:
    // boost::unordered applies an EXTRA mixing step to any hash not marked so,
    // because open addressing needs the low bits to be as good as the high
    // ones. libstdc++'s std::hash for strings is MurmurHash2 walked a byte at a
    // time and is NOT marked - so using it here paid for a weak hash and then
    // paid again to fix it up. boost::hash is a stronger mix over word-sized
    // chunks and carries the guarantee, so both costs go away at once.
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

// THE PROPERTY MAP, SWAPPABLE FOR MEASUREMENT.
//
// boost::unordered_flat_map is the default and the one that ships. The
// alternatives are here because "which open-addressing map is fastest" is not a
// question to answer from a README - they are all the same shape on paper. Set
// CTBROWSER_STRING_MAP to compare on a real workload:
//
//   0  boost::unordered_flat_map   the previous default, still the fallback
//   1  ankerl::unordered_dense     single header, MIT, insertion-ordered
//   2  tsl::robin_map              THE DEFAULT. Header-only, MIT, vendored at
//                                  external/robin-map. -3.6% wall and -8.7%
//                                  peak RSS against Boost's on a Phaser frame,
//                                  measured; see the table in that directory's
//                                  README-ctbrowser.md
//   3  absl::flat_hash_map         Google's Swiss table - the design
//                                  boost::unordered_flat_map also implements,
//                                  so this is the closest real rival. NOT
//                                  header-only: it links, and shipping it would
//                                  need the mingw sysroot treatment mimalloc
//                                  got.
//   4  gtl::flat_hash_map          Greg's Template Library - the parallel-hashmap
//                                  successor, header-only, Apache-2.0. Also a
//                                  Swiss table, and the one that ships a
//                                  parallel (sharded) variant.
//
// All three take the same transparent hasher and equality, so the prehashed
// chain walk works with any of them.

#if CTBROWSER_STRING_MAP == 1
template <typename Value>
using string_flat_map = ankerl::unordered_dense::map<std::string, Value, string_hash, string_equal>;
#elif CTBROWSER_STRING_MAP == 2
template <typename Value>
using string_flat_map = tsl::robin_map<std::string, Value, string_hash, string_equal>;
#elif CTBROWSER_STRING_MAP == 3
template <typename Value>
using string_flat_map = absl::flat_hash_map<std::string, Value, string_hash, string_equal>;
#elif CTBROWSER_STRING_MAP == 4
template <typename Value>
using string_flat_map = gtl::flat_hash_map<std::string, Value, string_hash, string_equal>;
#else
template <typename Value>
using string_flat_map = boost::unordered_flat_map<std::string, Value, string_hash, string_equal>;
#endif

// The hash a `prehashed_name` carries, computed the one way the map agrees
// with. Using anything else here is a lookup that silently never matches.
[[nodiscard]] inline std::size_t hash_name(std::string_view text) noexcept {
    return string_hash{}(text);
}

} // namespace ctbrowser
