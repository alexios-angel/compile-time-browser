#pragma once
// Private to lib/Script/builtins/collections/. NOT installed and in no file
// set: ../internal.hpp is the builtins' shared private header and this only
// adds what the four files carved out of collections.cpp on 2026-09-08 share
// with each other and with nothing else.

#include "../internal.hpp"

namespace ctbrowser::script::detail {

// A REAL ITERATOR over a list that already exists - what `keys()`, `values()`
// and `entries()` answer on an Array, a Map and a Set. Was in collections.cpp's
// anonymous namespace; defined in array_iteration.cpp, which says why it
// answers both protocols.
[[nodiscard]] value list_iterator(context & cx, value items, const char * tag);

} // namespace ctbrowser::script::detail

namespace ctbrowser::script::builtins_detail {

// The second half of install_array. One function of 1,212 lines was split at
// the seam before the callback-taking methods; the halves share only the two
// objects passed here, and install_array calls this at exactly the point the
// code used to continue, so every property lands in the order it always did.
void install_array_iteration(context & cx, native_object * array_ctor, object_object * array_proto);

} // namespace ctbrowser::script::builtins_detail
