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

/// True for an inline box as CSSOM View §7 uses the term (`display: inline`).
/// Inline-level blocks and replaced boxes are excluded because they have their
/// own client edges.
[[nodiscard]] inline bool is_inline_box(const layout::fragment & f) noexcept {
    return f.box != nullptr && f.box->kind == layout::box_kind::inline_;
}

} // namespace ctbrowser::shell::detail
