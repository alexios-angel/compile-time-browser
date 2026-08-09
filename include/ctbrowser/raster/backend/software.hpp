#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>

#include <ctbrowser/raster/backend/backend.hpp>
#include <ctbrowser/raster/draw.hpp>
#include <ctbrowser/raster/surface.hpp>
#include <ctbrowser/raster/tile.hpp>

// The software backend: the FIRST implementation, on purpose.
//
// Writing it before any GPU code means raster and composition are testable
// headlessly and byte-for-byte from the start, the test suite needs no GPU, and the golden
// image is reproducible on a machine with no fonts installed. When the SDL3 GPU
// backend arrives it has something to be checked against rather than being the
// only implementation and therefore correct by definition.
//
// Text is font8x8 rather than a real font stack for the same reason: a golden
// that depends on which fonts a machine happens to have is not a golden. Real
// text shaping belongs with the font work, and it will consume the same
// display list.

namespace ctbrowser::raster {

using ctbrowser::color;
using ctbrowser::paint::display_list;
using ctbrowser::paint::layer;
using ctbrowser::paint::paint_command;
using ctbrowser::paint::paint_op;

class software_backend {
public:
    static constexpr bool is_hardware = false;

    software_backend(int width, int height, int extent = default_tile_extent)
        : target_(width, height), extent_(extent) {}

    // The page canvas colour. White because that is what a browser with no
    // document background shows, not because anything here needs it.
    color clear_color = color::rgba(255, 255, 255);
    // Null means font8x8. Set through renderer::set_fonts by whoever owns the
    // faces - the browser - because a glyph cache outlives any one frame.
    const font_backend * fonts = nullptr;

    [[nodiscard]] std::expected<frame_token, gpu_error> begin_frame();

    // Allocate storage for every tile that will be rastered this frame.
    //
    // This is not in the plan's sketch of the concept, and it has to be: raster()
    // is called from many threads for distinct tiles, so the storage they write
    // into must already exist and must not be reallocated underneath them. A GPU
    // backend has the same requirement for its tile textures, so the addition is
    // not a software-backend accommodation.
    // Allocate storage for the tiles about to be rastered, KEEPING any that are
    // still valid from an earlier frame. Keeping them is what makes scrolling
    // back over ground already covered free, and it is why the store is keyed
    // by tile id rather than being a grid that gets rebuilt: a grid has to be
    // re-indexed whenever the visible window moves, and re-indexing loses
    // exactly the tiles that were worth keeping.
    //
    // Not done here: EVICTION. Tiles accumulate for as long as the content
    // bounds hold, so a long scroll through a long page keeps every tile it has
    // ever drawn. That is a memory budget question and it belongs with the
    // compositor thread in stage 6; discard() is the blunt instrument until then.
    [[nodiscard]] std::expected<void, gpu_error> reserve_tiles(std::span<const tile> tiles);

    // Does this tile still have to be drawn? A valid tile from a previous frame
    // does not, which is the whole basis of incremental raster.
    [[nodiscard]] bool needs_raster(tile_id id) const;

    // Throw every tile away. What a relayout has to call, since the content the
    // tiles were drawn from no longer exists.
    void discard() { layers_.clear(); }

    // ONE layer's tiles. The scrollbar is chrome that changes on every scroll
    // while the page underneath it does not - discarding everything for it
    // would re-raster the whole document sixty times a second and undo the
    // entire point of tiling in content space.
    void discard_layer(std::uint32_t layer);

    // A new viewport size, KEEPING the backend. The alternative - building a
    // fresh renderer - silently throws away a GPU device and drops the app to
    // software on its first window resize, permanently.
    void resize(int width, int height);

    // Rasterize one tile. SAFE TO CALL CONCURRENTLY FOR DISTINCT TILE IDS:
    // every write lands in that tile's own storage, which reserve_tiles already
    // allocated, and the display list is const.
    [[nodiscard]] std::expected<void, gpu_error> raster(tile_id id, const display_list & list);

    // Nothing to do: the pixels a tile was rastered into ARE the pixels
    // composite reads. A GPU backend uploads here instead.
    void tile_ready(tile_id) {}

    // Blit every layer's tiles into the target at the layer's current offset.
    //
    // This is the function a scroll runs. It reads tiles that were rasterized in
    // CONTENT space and places them in viewport space, so moving the page costs
    // a composite and no raster at all.
    [[nodiscard]] std::expected<void, gpu_error> composite(std::span<const layer> layers);

    [[nodiscard]] std::expected<void, gpu_error> end_frame();

    [[nodiscard]] const surface & target() const noexcept { return target_; }

    // How many tiles have been rastered since construction. The evidence for
    // "a scroll re-composites without re-rastering" is this number not moving.
    [[nodiscard]] std::size_t raster_calls() const noexcept;

private:
    struct slot {
        rect area;
        surface pixels;
        bool valid = false;
    };

    [[nodiscard]] static constexpr std::uint64_t key_of(tile_id id) noexcept;

    // --- compositing ----------------------------------------------------

    void blit(const surface & from, float at_x, float at_y, const rect & clip);

    surface target_;
    int extent_ = default_tile_extent;
    std::vector<flat_map<std::uint64_t, slot>> layers_;
    std::atomic<std::size_t> raster_calls_{0};
    std::uint64_t frame_ = 0;
    bool in_frame_ = false;
};

static_assert(RasterBackend<software_backend>);

} // namespace ctbrowser::raster
