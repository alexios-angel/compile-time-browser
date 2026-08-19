#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/paint/command.hpp>

// Layers: the unit the compositor moves.
//
// This is where scrolling stops being a layout problem. In the previous engine a scroll shifted
// every paint command and re-ran layout to reserve the scrollbar; here the page
// is one layer, scrolling changes its `offset`, and the compositor re-composites
// already-rasterized tiles. Nothing is re-recorded and nothing is re-rastered.
//
// position:fixed falls out of the same mechanism rather than needing the
// per-command `fixed` flag the previous engine carried: fixed content is its own layer whose
// offset the scroll does not touch.

namespace ctbrowser::paint {

using ctbrowser::node_id;
using ctbrowser::point;
using ctbrowser::rect;

struct layer {
    // Shared and const: the raster threads, the compositor and hit testing all
    // read the same recorded list, concurrently, while the next frame records.
    std::shared_ptr<const display_list> contents;

    // Where this layer's content origin sits in the viewport. Scrolling writes
    // here. Rastered tiles are in CONTENT space and do not know about it, which
    // is exactly why they survive a scroll.
    point offset;

    // Viewport-space clip. Empty means the whole viewport.
    rect clip;

    // Scroll does not move this layer. The scroller sets it; the compositor
    // only reads it.
    bool scrolls = true;

    [[nodiscard]] rect content_bounds() const noexcept {
        return contents ? contents->bounds() : rect{};
    }
};

// The layers of a frame, back to front.
struct layer_tree {
    std::vector<layer> layers;

    [[nodiscard]] bool empty() const noexcept { return layers.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return layers.size(); }

    // Hit-test front to back, in VIEWPORT space. A layer's display list and hit
    // regions are in content space, so its compositor offset is undone before
    // asking the list. This is the same transform raster/composite uses and is
    // what keeps a scroll from requiring a second geometry traversal.
    [[nodiscard]] node_id hit_test(point viewport_point) const noexcept {
        for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
            if (!it->contents || (!it->clip.empty() && !it->clip.contains(viewport_point))) {
                continue;
            }
            const point content_point{viewport_point.x - it->offset.x,
                                      viewport_point.y - it->offset.y};
            if (const node_id hit = it->contents->hit_test(content_point)) { return hit; }
        }
        return node_id{};
    }

    // Move every scrolling layer. The point of the whole design: a scroll is
    // this function, and then a composite.
    void scroll_to(float x, float y) {
        for (layer & l : layers) {
            if (l.scrolls) { l.offset = point{-x, -y}; }
        }
    }
};

} // namespace ctbrowser::paint
