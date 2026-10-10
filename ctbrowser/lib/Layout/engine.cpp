//===- ctbrowser/lib/Layout/engine.cpp - Layout entry point -----*- C++ -*-===//
//
// engine::run: lays out the box tree from the root, then places positioned
// boxes in a second pass over the finished fragment tree.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/layout/engine.hpp>

#include <ctbrowser/layout/position.hpp>

namespace ctbrowser::layout {

fragment engine::run(const box_node & root, float viewport_width, float viewport_height) const {
    fragment out = layout_box(root, constraints{viewport_width, 0, root.font_size}, measure_);
    // Positioning is a pass over the finished tree, because the box that decides
    // where an absolutely positioned child goes is not its parent - see
    // layout/position.hpp. The viewport is the initial containing block; its
    // height is the document's own, so a `bottom: 0` on a page with no positioned
    // ancestor lands at the end of the document rather than at the fold.
    apply_positioning(out, rect{0, 0, viewport_width, out.bounds.height},
                      viewport_height > 0 ? viewport_height : out.bounds.height, measure_);
    return out;
}

} // namespace ctbrowser::layout
