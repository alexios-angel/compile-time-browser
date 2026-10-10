//===- ctbrowser/page/client_rects.hpp - Element client rectangles -*- C++ -*-===//
//
// Where an element's boxes are in the viewport, as CSSOM View reports them:
// one box per fragment (getClientRects, getBoxQuads) and their bounding
// rectangle (getBoundingClientRect). Bare section numbers (§7) refer to the
// CSSOM View specification.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <ctbrowser/page/view.hpp>

namespace ctbrowser::page {

/// A box's bounds and the transform that maps them into the viewport.
struct box_geometry {
    rect bounds;
    transform to_viewport;
    /// The axis-aligned rectangle round the transformed bounds.
    [[nodiscard]] rect bounding_rect() const;
};

/// True for an inline box as CSSOM View §7 uses the term (`display: inline`).
/// Inline-level blocks and replaced boxes are excluded because they have their
/// own client edges.
[[nodiscard]] inline bool is_inline_box(const layout::fragment & f) noexcept {
    return f.box != nullptr && f.box->kind == layout::box_kind::inline_;
}

/// The absolute bounds of the first fragment for \p id in tree order, or
/// nothing when the node has no fragment. A fragment's bounds are relative to
/// its containing block, so this is fragment::find with the offsets
/// accumulated. \p dx and \p dy are the offset of \p at's containing block.
[[nodiscard]] inline std::optional<rect> absolute_rect_of(const layout::fragment & at, node_id id,
                                                          float dx = 0, float dy = 0) noexcept {
    const rect box = at.absolute_bounds(dx, dy);
    if (at.source == id) { return box; }
    for (const layout::fragment & child : at.children) {
        if (const std::optional<rect> hit = absolute_rect_of(child, id, box.x, box.y)) {
            return hit;
        }
    }
    return std::nullopt;
}

/// Returns one box_geometry per fragment of \p self: the untransformed
/// \p kind box ("border", "padding", "content" or "margin") and the mapping
/// from box-local coordinates to the viewport. Empty when it has no box.
[[nodiscard]] std::vector<box_geometry> client_boxes(const page_view & view, node_id self,
                                                     std::string_view kind);

/// getBoundingClientRect (§7) of an element whose client rects are \p rects:
/// the smallest rectangle round every rect that has a size, the first rect
/// when all are empty, and a zero rectangle when there are none.
[[nodiscard]] rect bounding_client_rect(std::span<const rect> rects);

} // namespace ctbrowser::page
