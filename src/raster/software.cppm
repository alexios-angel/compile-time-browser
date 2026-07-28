module;

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

export module ctbrowser.raster:software;

import ctbrowser.core;
import ctbrowser.paint;
import :backend;
import :draw;
import :surface;
import :tile;

// The software backend: the FIRST implementation, on purpose.
//
// Writing it before any GPU code means raster and composition are testable
// headlessly and byte-for-byte from the start, CI needs no GPU, and the golden
// image is reproducible on a machine with no fonts installed. When the SDL3 GPU
// backend arrives it has something to be checked against rather than being the
// only implementation and therefore correct by definition.
//
// Text is font8x8 rather than a real font stack for the same reason: a golden
// that depends on which fonts a machine happens to have is not a golden. Real
// text shaping belongs with the font work, and it will consume the same
// display list.

export namespace ctbrowser::raster {

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

    [[nodiscard]] std::expected<frame_token, gpu_error> begin_frame() {
        if (in_frame_) { return std::unexpected(gpu_error::no_frame); }
        in_frame_ = true;
        return frame_token{++frame_};
    }

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
    [[nodiscard]] std::expected<void, gpu_error> reserve_tiles(std::span<const tile> tiles) {
        if (!in_frame_) { return std::unexpected(gpu_error::no_frame); }
        for (const tile & t : tiles) {
            if (t.id.layer >= layers_.size()) { layers_.resize(t.id.layer + 1); }
            slot & s = layers_[t.id.layer][key_of(t.id)];
            // A tile whose content-space area moved is a different tile wearing
            // the same id, and what it holds is no longer what it should show.
            if (s.pixels.width() != extent_ || !(s.area == t.area)) {
                s.pixels = surface{extent_, extent_};
                s.area = t.area;
                s.valid = false;
            }
        }
        return {};
    }

    // Does this tile still have to be drawn? A valid tile from a previous frame
    // does not, which is the whole basis of incremental raster.
    [[nodiscard]] bool needs_raster(tile_id id) const {
        if (id.layer >= layers_.size()) { return true; }
        const auto it = layers_[id.layer].find(key_of(id));
        return it == layers_[id.layer].end() || !it->second.valid;
    }

    // Throw every tile away. What a relayout has to call, since the content the
    // tiles were drawn from no longer exists.
    void discard() { layers_.clear(); }

    // ONE layer's tiles. The scrollbar is chrome that changes on every scroll
    // while the page underneath it does not - discarding everything for it
    // would re-raster the whole document sixty times a second and undo the
    // entire point of tiling in content space.
    void discard_layer(std::uint32_t layer) {
        if (layer < layers_.size()) { layers_[layer].clear(); }
    }

    // A new viewport size, KEEPING the backend. The alternative - building a
    // fresh renderer - silently throws away a GPU device and drops the app to
    // software on its first window resize, permanently.
    void resize(int width, int height) {
        if (width == target_.width() && height == target_.height()) { return; }
        target_ = surface{width, height};
        discard(); // the tiles were rastered for a different content width
    }

    // Rasterize one tile. SAFE TO CALL CONCURRENTLY FOR DISTINCT TILE IDS:
    // every write lands in that tile's own storage, which reserve_tiles already
    // allocated, and the display list is const.
    [[nodiscard]] std::expected<void, gpu_error> raster(tile_id id, const display_list & list) {
        if (!in_frame_) { return std::unexpected(gpu_error::no_frame); }
        if (id.layer >= layers_.size()) { return std::unexpected(gpu_error::bad_tile); }
        const auto it = layers_[id.layer].find(key_of(id));
        if (it == layers_[id.layer].end()) { return std::unexpected(gpu_error::bad_tile); }

        slot & s = it->second;
        raster_calls_.fetch_add(1, std::memory_order_relaxed);
        draw_into(s.pixels, list, s.area, fonts); // shared with the GPU backend, see :draw
        s.valid = true;
        return {};
    }

    // Nothing to do: the pixels a tile was rastered into ARE the pixels
    // composite reads. A GPU backend uploads here instead.
    void tile_ready(tile_id) {}

    // Blit every layer's tiles into the target at the layer's current offset.
    //
    // This is the function a scroll runs. It reads tiles that were rasterized in
    // CONTENT space and places them in viewport space, so moving the page costs
    // a composite and no raster at all.
    [[nodiscard]] std::expected<void, gpu_error> composite(std::span<const layer> layers) {
        if (!in_frame_) { return std::unexpected(gpu_error::no_frame); }
        target_.fill(clear_color);
        // Layers composite back to front, so the outer loop order is load
        // bearing. Order WITHIN a layer is not: a layer's tiles never overlap,
        // so each target pixel is touched at most once per layer - which is what
        // makes it safe to iterate an unordered store and still get a
        // deterministic image.
        for (std::size_t li = 0; li < layers.size() && li < layers_.size(); ++li) {
            const layer & l = layers[li];
            for (const auto & [key, s] : layers_[li]) {
                if (!s.valid) { continue; }
                const float at_x = s.area.x + l.offset.x;
                const float at_y = s.area.y + l.offset.y;
                // Tiles are KEPT after they scroll out of view, so that scrolling
                // back is free - which means the store holds every tile the page
                // has ever shown. Compositing all of them would make a scroll get
                // slower the longer it went on. Skip the ones that land off the
                // target entirely.
                if (at_x + static_cast<float>(extent_) <= 0 ||
                    at_y + static_cast<float>(extent_) <= 0 ||
                    at_x >= static_cast<float>(target_.width()) ||
                    at_y >= static_cast<float>(target_.height())) {
                    continue;
                }
                blit(s.pixels, at_x, at_y, l.clip);
            }
        }
        return {};
    }

    [[nodiscard]] std::expected<void, gpu_error> end_frame() {
        if (!in_frame_) { return std::unexpected(gpu_error::no_frame); }
        in_frame_ = false;
        return {};
    }

    [[nodiscard]] const surface & target() const noexcept { return target_; }

    // How many tiles have been rastered since construction. The evidence for
    // "a scroll re-composites without re-rastering" is this number not moving.
    [[nodiscard]] std::size_t raster_calls() const noexcept {
        return raster_calls_.load(std::memory_order_relaxed);
    }

private:
    struct slot {
        rect area;
        surface pixels;
        bool valid = false;
    };

    [[nodiscard]] static constexpr std::uint64_t key_of(tile_id id) noexcept {
        return (static_cast<std::uint64_t>(id.column) << 32) | id.row;
    }

    // --- compositing ----------------------------------------------------

    void blit(const surface & from, float at_x, float at_y, const rect & clip) {
        const int dx = round_to_pixel(at_x);
        const int dy = round_to_pixel(at_y);
        pixel_rect window{0, 0, target_.width(), target_.height()};
        if (!clip.empty()) {
            const pixel_rect c = to_pixels(clip, target_.width(), target_.height());
            window = pixel_rect{std::max(window.left, c.left), std::max(window.top, c.top),
                                std::min(window.right, c.right), std::min(window.bottom, c.bottom)};
        }
        for (int y = 0; y < from.height(); ++y) {
            const int ty = dy + y;
            if (ty < window.top || ty >= window.bottom) { continue; }
            const std::span<const std::uint32_t> src = from.row(y);
            const std::span<std::uint32_t> dst = target_.row(ty);
            for (int x = 0; x < from.width(); ++x) {
                const int tx = dx + x;
                if (tx < window.left || tx >= window.right) { continue; }
                const std::uint32_t s = src[static_cast<std::size_t>(x)];
                dst[static_cast<std::size_t>(tx)] =
                    blend_over(dst[static_cast<std::size_t>(tx)], color{s});
            }
        }
    }

    surface target_;
    int extent_ = default_tile_extent;
    std::vector<flat_map<std::uint64_t, slot>> layers_;
    std::atomic<std::size_t> raster_calls_{0};
    std::uint64_t frame_ = 0;
    bool in_frame_ = false;
};

static_assert(RasterBackend<software_backend>);

} // namespace ctbrowser::raster
