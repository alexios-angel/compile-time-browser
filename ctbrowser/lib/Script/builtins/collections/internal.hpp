#pragma once
// Private to lib/Script/builtins/collections/. NOT installed and in no file
// set: ../internal.hpp is the builtins' shared private header and this only
// adds what the four files carved out of collections.cpp on 2026-09-08 share
// with each other and with nothing else.

#include "../internal.hpp"

namespace ctbrowser::script::detail {

// list_iterator, which the four files here share, moved to ../internal.hpp on
// 2026-09-12 when String.prototype[@@iterator] needed it too.

} // namespace ctbrowser::script::detail

namespace ctbrowser::script::builtins_detail {

// The second half of install_array. One function of 1,212 lines was split at
// the seam before the callback-taking methods; the halves share only the two
// objects passed here, and install_array calls this at exactly the point the
// code used to continue, so every property lands in the order it always did.
void install_array_iteration(context & cx, native_object * array_ctor, object_object * array_proto);

} // namespace ctbrowser::script::builtins_detail
