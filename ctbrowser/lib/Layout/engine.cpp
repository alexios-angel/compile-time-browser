#include <ctbrowser/layout/engine.hpp>

#include <ctbrowser/layout/position.hpp>

// engine: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::layout {

fragment engine::run(const box_node & root, float viewport_width, float viewport_height) const {
    fragment out = layout_box(root, constraints{viewport_width, 0, root.font_size}, measure_);
    // POSITIONING IS A PASS OVER THE FINISHED TREE, because the box that decides
    // where an absolutely positioned child goes is not its parent - see
    // layout/position.hpp. The viewport is the initial containing block; its
    // height is the document's own, so a `bottom: 0` on a page with no positioned
    // ancestor lands at the end of the document rather than at the fold.
    apply_positioning(out, rect{0, 0, viewport_width, out.bounds.height},
                      viewport_height > 0 ? viewport_height : out.bounds.height, measure_);
    return out;
}

} // namespace ctbrowser::layout
