#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>

#include <ctbrowser/raster/backend/software.hpp>
#include <ctbrowser/raster/tile.hpp>

// Driving a frame: draw() rasters the tiles a frame needs and composites them.
// A scroll costs a composite only, because tiles are in content space and a
// scroll only moves the layer.

namespace ctbrowser::raster {

using ctbrowser::paint::layer_tree;

// Raster the tiles a frame needs - in parallel when a pool is given - then
// composite.
//
// Two things keep this from doing the whole page every time:
//
//   * VIEWPORT CULLING. Only tiles near the visible region are rastered. A page
//     is routinely a hundred times taller than the window, and rastering all of
//     it costs a hundred times what it should. `viewport` is in viewport space;
//     an empty one means "everything", which is what the identity tests want.
//   * INCREMENTAL RASTER. needs_raster() skips tiles a previous frame already
//     drew, so scrolling pays only for what came into view.
//
// The prefetch margin is one tile on each side. It is what stops a slow scroll
// from stuttering at every tile boundary, and one tile is the smallest margin
// that can hide a scroll step shorter than a tile.
inline void draw(software_backend & backend, const layer_tree & tree, scheduler * pool = nullptr,
                 int extent = default_tile_extent, const rect & viewport = rect{}) {
    // The tiles this frame has to have, and which display list each comes from.
    std::vector<tile> tiles;
    std::vector<const paint::display_list *> lists;
    for (std::uint32_t i = 0; i < tree.layers.size(); ++i) {
        const paint::layer & l = tree.layers[i];
        if (!l.contents) { continue; }
        // The visible region in this layer's CONTENT space - which is where its
        // tiles live, and why a layer's offset is what turns a scroll into a
        // different set of tiles rather than a different set of commands.
        const float margin = static_cast<float>(extent);
        const rect visible =
            viewport.empty()
                ? rect{}
                : rect{viewport.x - l.offset.x - margin, viewport.y - l.offset.y - margin,
                       viewport.width + 2 * margin, viewport.height + 2 * margin};
        for (const tile & t : tiles_for(l.contents->bounds(), i, extent)) {
            if (!visible.empty() && !t.area.intersects(visible)) { continue; }
            tiles.push_back(t);
            lists.push_back(l.contents.get());
        }
    }

    backend.reserve_tiles(tiles);

    // Each tile is written by one thread into storage reserve_tiles already
    // allocated, and reads a const display list. Nothing is shared.
    // Ask which tiles are actually stale BEFORE fanning out: needs_raster reads
    // the same store raster() writes, and mixing the two across threads would
    // be a race for no benefit - the query is trivial.
    std::vector<std::size_t> stale;
    stale.reserve(tiles.size());
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (backend.needs_raster(tiles[i].id)) { stale.push_back(i); }
    }

    const auto do_tile = [&](std::size_t k) {
        const std::size_t i = stale[k];
        backend.raster(tiles[i].id, *lists[i]);
    };
    if (pool != nullptr && stale.size() > 1) {
        pool->parallel_for(stale.size(), do_tile);
    } else {
        for (std::size_t k = 0; k < stale.size(); ++k) { do_tile(k); }
    }
    backend.composite(tree.layers);
}

} // namespace ctbrowser::raster
