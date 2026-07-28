#pragma once
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

} // namespace ctbrowser
