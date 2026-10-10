//===- tile.hpp - Raster tiles ----------------------------------*- C++ -*-===//
//
// Tiles: the unit of raster work. Tiling buys three things:
//
//   1. Raster parallelises: each tile is written by one thread and read by
//      nobody until it is finished.
//   2. A scroll only rasters what came into view; tiles already covered stay
//      valid, because they are in content space.
//   3. An invalidation costs the tiles it touches, not the page.
//
// raster/backend/compositor.hpp uses all three: it culls tiles to the viewport
// and re-rasters only the tiles the backend reports as stale.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <ctbrowser/core/core.hpp>

namespace ctbrowser::raster {

using ctbrowser::rect;

/// Tile side length in pixels. 256 is the size Blink and Skia settled on: big
/// enough that per-tile overhead disappears, small enough that a partial
/// invalidation is cheap and a tile fits comfortably in L2.
inline constexpr int default_tile_extent = 256;

/// A tile's position: its layer and its column and row counted from the
/// layer content's first tile.
struct tile_id {
    std::uint32_t layer = 0;
    std::uint32_t column = 0;
    std::uint32_t row = 0;
    [[nodiscard]] friend constexpr bool operator==(tile_id, tile_id) = default;
};

/// One tile and the area it covers.
struct tile {
    tile_id id;
    rect area; // In the layer's content space, so a scroll does not invalidate it.
};

/// The tiles covering a layer's content bounds; when `visible` is not empty,
/// only those whose area meets it.
///
/// The grid is anchored at the content origin (0, 0) rather than at the bounds'
/// top-left, so every layer of a page agrees on where tile boundaries are. A
/// tile's id counts from the content's first tile whatever `visible` is, so a
/// scroll keeps the ids stable.
///
/// The cull narrows the index range before the loop: a 1e7 x 1e7 px box is
/// 1.5 billion tiles, too many to enumerate and discard. Tile indices are
/// clamped to +-2^29 so the float-to-int cast is defined for any coordinate,
/// NaN included.
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
