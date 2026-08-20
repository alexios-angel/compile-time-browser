#include <ctbrowser/raster/backend/software.hpp>

// software: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::raster {

std::expected<frame_token, gpu_error> software_backend::begin_frame() {
    if (in_frame_) { return std::unexpected(gpu_error::no_frame); }
    in_frame_ = true;
    return frame_token{++frame_};
}

std::expected<void, gpu_error> software_backend::reserve_tiles(std::span<const tile> tiles) {
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

bool software_backend::needs_raster(tile_id id) const {
    if (id.layer >= layers_.size()) { return true; }
    const auto it = layers_[id.layer].find(key_of(id));
    return it == layers_[id.layer].end() || !it->second.valid;
}

void software_backend::discard_layer(std::uint32_t layer) {
    if (layer < layers_.size()) { layers_[layer].clear(); }
}

void software_backend::resize(int width, int height) {
    if (width == target_.width() && height == target_.height()) { return; }
    target_ = surface{width, height};
    discard(); // the tiles were rastered for a different content width
}

std::expected<void, gpu_error> software_backend::raster(tile_id id, const display_list & list) {
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

std::expected<void, gpu_error> software_backend::composite(std::span<const layer> layers) {
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

std::expected<void, gpu_error> software_backend::end_frame() {
    if (!in_frame_) { return std::unexpected(gpu_error::no_frame); }
    in_frame_ = false;
    return {};
}

std::size_t software_backend::raster_calls() const noexcept {
    return raster_calls_.load(std::memory_order_relaxed);
}

constexpr std::uint64_t software_backend::key_of(tile_id id) noexcept {
    return (static_cast<std::uint64_t>(id.column) << 32) | id.row;
}

void software_backend::blit(const surface & from, float at_x, float at_y, const rect & clip) {
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

} // namespace ctbrowser::raster
