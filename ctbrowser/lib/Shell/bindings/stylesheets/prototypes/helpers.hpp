#pragma once

#include "../internal.hpp"

namespace ctbrowser::shell::detail {

script::object_object * cssom_interface(context & cx, script::object_object * internals,
                                        const char * name, const char * inherits,
                                        script::native_fn construct);
void cssom_iterable(context & cx, script::object_object * on);

} // namespace ctbrowser::shell::detail
