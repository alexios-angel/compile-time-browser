#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include <ctbrowser/core/core.hpp>

// Tiles: the unit of raster work.
//
// Tiling buys three things at once, and it is worth naming them because only
// the first is obvious:
//
//   1. raster parallelises - each tile is written by one thread and read by
//      nobody until it is finished.
//   2. a scroll only rasters what came INTO view; the tiles that were already
//      covered are still valid, because they are in content space.
//   3. an invalidation costs the tiles it touches, not the page.
//
// Only the first is exercised at this stage; the other two need the invalidation
// tracking that comes with the compositor.

namespace ctbrowser::raster {

using ctbrowser::rect;

// 256 is the size Blink and Skia settled on: big enough that the per-tile
// overhead disappears, small enough that a partial invalidation is cheap and a
// tile still fits comfortably in L2.
inline constexpr int default_tile_extent = 256;

struct tile_id {
    std::uint32_t layer = 0;
    std::uint32_t column = 0;
    std::uint32_t row = 0;
    [[nodiscard]] friend constexpr bool operator==(tile_id, tile_id) = default;
};

struct tile {
    tile_id id;
    rect area; // in the layer's CONTENT space, which is why a scroll does not invalidate it
};

// The tiles covering a layer's content bounds.
//
// Anchored at the content origin rather than at the bounds' top-left, so a
// layer whose content starts at a non-zero offset still lands on the same grid
// as one that starts at zero - otherwise two layers of the same page would
// disagree about where tile boundaries are.
[[nodiscard]] inline std::vector<tile> tiles_for(const rect & content, std::uint32_t layer,
                                                 int extent = default_tile_extent) {
    std::vector<tile> out;
    if (content.empty() || extent <= 0) { return out; }
    const auto floor_div = [extent](float v) {
        const int i = static_cast<int>(v < 0 ? v - static_cast<float>(extent) + 1 : v);
        return i / extent - (i % extent != 0 && v < 0 ? 1 : 0);
    };
    const int first_col = floor_div(content.x);
    const int first_row = floor_div(content.y);
    const int last_col = floor_div(content.right() - 0.001f);
    const int last_row = floor_div(content.bottom() - 0.001f);
    for (int r = first_row; r <= last_row; ++r) {
        for (int c = first_col; c <= last_col; ++c) {
            tile t;
            t.id = tile_id{layer, static_cast<std::uint32_t>(c - first_col),
                           static_cast<std::uint32_t>(r - first_row)};
            t.area = rect{static_cast<float>(c * extent), static_cast<float>(r * extent),
                          static_cast<float>(extent), static_cast<float>(extent)};
            out.push_back(t);
        }
    }
    return out;
}

} // namespace ctbrowser::raster
