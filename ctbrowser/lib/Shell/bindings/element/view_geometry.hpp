//===- view_geometry.hpp - Element view geometry helpers --------*- C++ -*-===//
//
// Small helpers shared by the element view accessors (views.cpp,
// style_views.cpp): IDL-to-CSS property name conversion and the "is this an
// inline box" test that CSSOM View's client metrics depend on.
//
//===----------------------------------------------------------------------===//
#pragma once
#include "internal.hpp"

namespace ctbrowser::shell::detail {
/// `backgroundColor` -> `background-color`; defined beside the property table in
/// lib/Style/css/properties/table.cpp.
using style::css::css_name_of;

/// The CSSOM View "inline box" test; see ctbrowser/page/client_rects.hpp.
using page::is_inline_box;

} // namespace ctbrowser::shell::detail
