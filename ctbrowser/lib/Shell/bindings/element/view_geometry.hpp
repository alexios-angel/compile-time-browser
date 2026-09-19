#pragma once
#include "internal.hpp"

namespace ctbrowser::shell::detail {
// `backgroundColor` -> `background-color`; the conversion lives with the property
// table, see declarations.cpp.
using style::css::css_name_of;

// An INLINE box in §7's sense - `display: inline` - and not an inline-level
// block or replaced box, which have client edges of their own.
[[nodiscard]] inline bool is_inline_box(const layout::fragment & f) noexcept {
    return f.box != nullptr && f.box->kind == layout::box_kind::inline_;
}

} // namespace ctbrowser::shell::detail
