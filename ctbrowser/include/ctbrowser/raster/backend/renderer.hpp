#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>

#include <ctbrowser/raster/backend/backend.hpp>
#include <ctbrowser/raster/backend/software.hpp>
#include <ctbrowser/raster/surface.hpp>
#include <ctbrowser/raster/tile.hpp>

// The renderer the browser owns: a movable handle to the software backend.
//
// software_backend itself is not movable (it counts raster calls in an atomic),
// and the browser has to be able to swap its renderer at runtime, so this is
// the handle that can be. It satisfies RasterBackend, so draw() and every test
// run through it unchanged.

namespace ctbrowser::raster {

using ctbrowser::paint::display_list;
using ctbrowser::paint::layer;

class renderer {
public:
    [[nodiscard]] static renderer software(int width, int height,
                                           int extent = default_tile_extent) {
        renderer out;
        out.self_ = std::make_unique<software_backend>(width, height, extent);
        return out;
    }

    renderer() = default;

    [[nodiscard]] explicit operator bool() const noexcept { return self_ != nullptr; }
    [[nodiscard]] bool hardware() const noexcept { return false; }
    [[nodiscard]] std::string_view name() const noexcept { return "software"; }

    // --- RasterBackend ---------------------------------------------------

    static constexpr bool is_hardware = false;

    [[nodiscard]] std::expected<frame_token, gpu_error> begin_frame() {
        return self_->begin_frame();
    }
    [[nodiscard]] std::expected<void, gpu_error> reserve_tiles(std::span<const tile> tiles) {
        return self_->reserve_tiles(tiles);
    }
    [[nodiscard]] bool needs_raster(tile_id id) const { return self_->needs_raster(id); }
    [[nodiscard]] std::expected<void, gpu_error> raster(tile_id id, const display_list & list) {
        return self_->raster(id, list);
    }
    void tile_ready(tile_id id) { self_->tile_ready(id); }
    [[nodiscard]] std::expected<void, gpu_error> composite(std::span<const layer> layers) {
        return self_->composite(layers);
    }
    [[nodiscard]] std::expected<void, gpu_error> end_frame() { return self_->end_frame(); }

    // Every tile is stale. What a relayout calls.
    void discard() { self_->discard(); }

    // ONE layer's tiles are stale. What a chrome overlay calls when it redraws
    // itself and the page beneath it has not changed.
    void discard_layer(std::uint32_t layer) { self_->discard_layer(layer); }

    void resize(int width, int height) { self_->resize(width, height); }

    // The fonts every tile is drawn with. A pointer rather than a value: it
    // owns a glyph cache, it is shared by every tile of every frame, and the
    // browser outlives the renderer. Null means font8x8, which is always there.
    void set_fonts(const font_backend * fonts) { self_->fonts = fonts; }

    // The page canvas colour, behind every layer.
    void set_clear_color(color c) { self_->clear_color = c; }

    // The composited image, for goldens and headless runs.
    [[nodiscard]] std::expected<surface, gpu_error> read_target() { return self_->target(); }

    // The concrete backend, when a caller needs something outside the concept.
    template <typename B> [[nodiscard]] B * get_if() noexcept {
        if constexpr (std::same_as<B, software_backend>) {
            return self_.get();
        } else {
            return nullptr;
        }
    }

private:
    std::unique_ptr<software_backend> self_;
};

static_assert(RasterBackend<renderer>);

} // namespace ctbrowser::raster
