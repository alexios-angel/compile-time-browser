//===- software.hpp - The software raster backend ------------- -*- C++ -*-===//
//
// The software backend: tiles rastered in content space, composited into a
// target surface. Headless and byte-for-byte reproducible, so the test suite
// needs no GPU and the golden image is the same on a machine with no fonts
// installed.
//
// One part of the contract the types cannot express, so it is stated here and
// checked by the tests instead:
//
//   raster(t, dl) must be safe to call concurrently for distinct tile ids.
//
// That is the point of tiling, and it is why draw() may fan tiles across the
// pool. reserve_tiles and composite are single-threaded.
//
// Text defaults to font8x8 rather than a real font stack for the same reason
// as reproducibility above: a golden that depends on which fonts a machine
// happens to have is not a golden.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>

#include <ctbrowser/raster/draw.hpp>
#include <ctbrowser/raster/surface.hpp>
#include <ctbrowser/raster/tile.hpp>

namespace ctbrowser::raster {

using ctbrowser::color;
using ctbrowser::paint::display_list;
using ctbrowser::paint::layer;
using ctbrowser::paint::paint_command;
using ctbrowser::paint::paint_op;

/// Rasters tiles on the CPU and composites them into one target surface.
class software_backend {
public:
    software_backend(int width, int height, int extent = default_tile_extent)
        : target_(width, height), extent_(extent) {}

    /// The page canvas colour. White because that is what a browser with no
    /// document background shows.
    color clear_color = color::rgba(255, 255, 255);
    /// Null means font8x8. Set by whoever owns the faces (the browser),
    /// because a glyph cache outlives any one frame.
    const font_backend * fonts = nullptr;

    /// Allocates storage for every tile about to be rastered, before raster():
    /// raster() is called from many threads for distinct tiles, so the storage
    /// they write into must already exist and must not be reallocated
    /// underneath them. Keeps any tile still valid from an earlier frame, which
    /// makes scrolling back over ground already covered free. That is why the
    /// store is keyed by tile id rather than being a grid that gets rebuilt: a
    /// grid has to be re-indexed whenever the visible window moves, and
    /// re-indexing loses exactly the tiles that were worth keeping.
    ///
    /// Not done here: eviction. Tiles accumulate for as long as the content
    /// bounds hold, so a long scroll through a long page keeps every tile it
    /// has ever drawn. That is a memory budget question for the compositor;
    /// discard() is the blunt instrument until then.
    void reserve_tiles(std::span<const tile> tiles);

    /// Whether this tile still has to be drawn. A valid tile from a previous
    /// frame does not, which is the basis of incremental raster.
    [[nodiscard]] bool needs_raster(tile_id id) const;

    /// Throws every tile away. A relayout has to call this, since the content
    /// the tiles were drawn from no longer exists.
    void discard() { layers_.clear(); }

    /// Throws away one layer's tiles. The scrollbar is chrome that changes on
    /// every scroll while the page underneath it does not; discarding
    /// everything for it would re-raster the whole document sixty times a
    /// second.
    void discard_layer(std::uint32_t layer);

    /// Sets a new viewport size. The tiles are discarded, since they were
    /// rastered for a different content width; the fonts and the raster
    /// counter stay.
    void resize(int width, int height);

    /// Rasterizes one tile that reserve_tiles allocated. Safe to call
    /// concurrently for distinct tile ids: every write lands in that tile's own
    /// storage and the display list is const.
    void raster(tile_id id, const display_list & list);

    /// Blits every layer's tiles into the target at the layer's current offset.
    ///
    /// This is the function a scroll runs. It reads tiles that were rasterized
    /// in content space and places them in viewport space, so moving the page
    /// costs a composite and no raster at all.
    void composite(std::span<const layer> layers);

    /// The composited image, for goldens and headless runs.
    [[nodiscard]] const surface & target() const noexcept { return target_; }

    /// How many tiles have been rastered since construction. Tests check that a
    /// scroll re-composites without re-rastering by seeing this number not move.
    [[nodiscard]] std::size_t raster_calls() const noexcept;

private:
    struct slot {
        rect area;
        surface pixels;
        bool valid = false;
    };

    [[nodiscard]] static constexpr std::uint64_t key_of(tile_id id) noexcept;

    void blit(const surface & from, float at_x, float at_y, const rect & clip);

    surface target_;
    int extent_ = default_tile_extent;
    std::vector<flat_map<std::uint64_t, slot>> layers_;
    std::atomic<std::size_t> raster_calls_{0};
};

} // namespace ctbrowser::raster
