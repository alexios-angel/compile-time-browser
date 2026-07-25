module;
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <cstddef>

export module ctbrowser.core:containers;

// The third-party containers, included in EXACTLY ONE PLACE.
//
// This is not tidiness. A header that defines `static inline` functions -
// boost::unordered's size_index_for is one - gets internal linkage in every
// module that includes it in its global module fragment. A translation unit
// that then imports two such modules sees two definitions with the same mangled
// name, and clang rejects it:
//
//   error: definition with same mangled name '..size_index_for..' as another
//          definition
//
// It only appears once enough modules are combined, which is the worst possible
// time to find out. Owning the include here and re-exporting the types means
// every module gets them through `import ctbrowser.core` and the header is
// textually present once.
//
// The aliases are also the seam for replacing these with std:: versions when
// libstdc++ catches up - flat_map is std::flat_map's shape, and small_vector is
// std::inplace_vector's neighbour.

export namespace ctbrowser {

// Open addressing, and the reason it is here rather than std::unordered_map:
// the standard one is a linked list of nodes and every lookup is a pointer
// chase. These are the atom table, the style intern table and the VM's globals.
template <typename Key, typename Value>
using flat_map = boost::unordered_flat_map<Key, Value>;

template <typename Key> using flat_set = boost::unordered_flat_set<Key>;

// NOT centralised here: boost::container's small_vector and boost::intrusive_ptr.
// They cause no collision, and moving them WOULD break - small_vector's
// allocator constructs through a placement-new overload declared in the header,
// and a template instantiated in another module cannot see a global-module
// declaration that module never included. Centralise only what has to be.

} // namespace ctbrowser
