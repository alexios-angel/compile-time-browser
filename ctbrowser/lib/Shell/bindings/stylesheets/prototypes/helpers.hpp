//===- helpers.hpp - CSSOM interface helpers --------------------*- C++ -*-===//
//
// Helpers shared by the CSSOM prototype installers in this directory.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../internal.hpp"

namespace ctbrowser::shell::detail {

/// Creates the CSSOM interface `name` (constructor plus prototype), inheriting
/// from the interface `inherits` when it is not null, and returns the
/// prototype. A null `construct` makes the constructor throw "Illegal
/// constructor".
script::object_object * cssom_interface(context & cx, script::object_object * internals,
                                        const char * name, const char * inherits,
                                        script::native_fn construct);
/// Installs `@@iterator` on `on`: the Array iterator over its indexed items.
void cssom_iterable(context & cx, script::object_object * on);

} // namespace ctbrowser::shell::detail
