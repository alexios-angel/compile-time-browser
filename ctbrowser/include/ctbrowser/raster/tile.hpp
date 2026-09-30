#pragma once
#include <algorithm>
#include <cmath>
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

// The tiles covering a layer's content bounds - only those whose area meets
// `visible` when it is not empty.
//
// Anchored at the content origin rather than at the bounds' top-left, so a
// layer whose content starts at a non-zero offset still lands on the same grid
// as one that starts at zero - otherwise two layers of the same page would
// disagree about where tile boundaries are. A tile's id counts from the
// CONTENT's first tile whatever `visible` is, so a scroll keeps the ids stable.
//
// THE CULL HAPPENS BEFORE THE LOOP: a `width: 1e7px; height: 1e7px` box is
// 1.5 billion tiles, and enumerating them to throw all but a screenful away was
// the whole frame. Tile indices are clamped to +-2^29 so the float-to-int cast
// is defined for any coordinate, NaN included.
[[nodiscard]] inline std::vector<tile> tiles_for(const rect & content, std::uint32_t layer,
                                                 int extent = default_tile_extent,
                                                 const rect & visible = rect{}) {
    std::vector<tile> out;
    if (content.empty() || extent <= 0) { return out; }
    const auto floor_div = [extent](float v) {
        constexpr double limit = 1 << 29;
        const double q = std::floor(static_cast<double>(v) / static_cast<double>(extent));
        return static_cast<int>(q >= -limit ? (q <= limit ? q : limit) : -limit);
    };
    const int first_col = floor_div(content.x);
    const int first_row = floor_div(content.y);
    const int last_col = floor_div(content.right() - 0.001f);
    const int last_row = floor_div(content.bottom() - 0.001f);
    int lo_col = first_col, hi_col = last_col, lo_row = first_row, hi_row = last_row;
    if (!visible.empty()) {
        // One tile of slack either side; the intersects() test below is exact.
        lo_col = std::max(lo_col, floor_div(visible.x) - 1);
        hi_col = std::min(hi_col, floor_div(visible.right()) + 1);
        lo_row = std::max(lo_row, floor_div(visible.y) - 1);
        hi_row = std::min(hi_row, floor_div(visible.bottom()) + 1);
    }
    for (int r = lo_row; r <= hi_row; ++r) {
        for (int c = lo_col; c <= hi_col; ++c) {
            tile t;
            t.id = tile_id{layer, static_cast<std::uint32_t>(c - first_col),
                           static_cast<std::uint32_t>(r - first_row)};
            t.area = rect{static_cast<float>(static_cast<double>(c) * extent),
                          static_cast<float>(static_cast<double>(r) * extent),
                          static_cast<float>(extent), static_cast<float>(extent)};
            if (!visible.empty() && !t.area.intersects(visible)) { continue; }
            out.push_back(t);
        }
    }
    return out;
}

} // namespace ctbrowser::raster
