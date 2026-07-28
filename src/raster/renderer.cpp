#include <ctbrowser/raster/renderer.hpp>

// renderer: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::raster {

std::expected<frame_token, gpu_error> renderer::begin_frame() {
    return ops_->begin_frame(self_);
}

std::expected<void, gpu_error> renderer::reserve_tiles(std::span<const tile> tiles) {
    return ops_->reserve_tiles(self_, tiles);
}

std::expected<void, gpu_error> renderer::raster(tile_id id, const display_list & list) {
    return ops_->raster(self_, id, list);
}

std::expected<void, gpu_error> renderer::composite(std::span<const layer> layers) {
    return ops_->composite(self_, layers);
}

std::expected<surface, gpu_error> renderer::read_target() {
    return ops_->read_target(self_);
}

void renderer::steal(renderer && other) noexcept {
    self_ = std::exchange(other.self_, nullptr);
    ops_ = std::exchange(other.ops_, nullptr);
    name_ = std::move(other.name_);
    hardware_ = other.hardware_;
}

void renderer::destroy() {
    if (self_ != nullptr && ops_ != nullptr) { ops_->destroy(self_); }
    self_ = nullptr;
    ops_ = nullptr;
}

} // namespace ctbrowser::raster
