//===- layer.hpp - Compositor layers -------------------------- -*- C++ -*-===//
//
// Layers: the unit the compositor moves.
//
// Scrolling is not a layout problem: the page is one layer, a scroll changes
// its `offset`, and the compositor re-composites already-rasterized tiles.
// Nothing is re-recorded or re-rastered. position:fixed content is its own
// layer whose offset the scroll does not touch.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstdint>
#include <memory>
#include <ranges>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/paint/command.hpp>

namespace ctbrowser::paint {

using ctbrowser::node_id;
using ctbrowser::point;
using ctbrowser::rect;

/// One independently moved piece of a frame: a recorded display list plus
/// where it sits in the viewport.
struct layer {
    /// Shared and const: the raster threads, the compositor and hit testing all
    /// read the same recorded list, concurrently, while the next frame records.
    std::shared_ptr<const display_list> contents;

    /// Where this layer's content origin sits in the viewport. Scrolling writes
    /// here. Rastered tiles are in content space and do not know about it,
    /// which is why they survive a scroll.
    point offset;

    /// Viewport-space clip. Empty means the whole viewport.
    rect clip;

    /// Whether scrolling moves this layer. False for position:fixed content.
    /// The scroller sets it; the compositor only reads it.
    bool scrolls = true;

    [[nodiscard]] rect content_bounds() const noexcept {
        return contents ? contents->bounds() : rect{};
    }
};

/// The layers of a frame, back to front.
struct layer_tree {
    std::vector<layer> layers;

    [[nodiscard]] bool empty() const noexcept { return layers.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return layers.size(); }

    /// Hit-tests front to back, in viewport space. A layer's display list and
    /// hit regions are in content space, so its compositor offset is undone
    /// before asking the list. This is the same transform raster/composite uses,
    /// so a scroll needs no second geometry traversal.
    [[nodiscard]] node_id hit_test(point viewport_point) const noexcept {
        for (const layer & l : std::views::reverse(layers)) {
            if (!l.contents || (!l.clip.empty() && !l.clip.contains(viewport_point))) { continue; }
            const point content_point{viewport_point.x - l.offset.x, viewport_point.y - l.offset.y};
            if (const node_id hit = l.contents->hit_test(content_point)) { return hit; }
        }
        return node_id{};
    }

    /// Moves every scrolling layer. A scroll is this function followed by a
    /// composite.
    void scroll_to(float x, float y) {
        for (layer & l : layers) {
            if (l.scrolls) { l.offset = point{-x, -y}; }
        }
    }
};

} // namespace ctbrowser::paint
