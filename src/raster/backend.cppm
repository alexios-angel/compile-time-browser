module;
#include <concepts>
#include <cstdint>
#include <expected>
#include <span>

export module ctbrowser.raster:backend;

import ctbrowser.core;
import ctbrowser.paint;
import :tile;

// What a rasterizer has to be able to do.
//
// The concept exists so that the software backend and the GPU backend are
// interchangeable at compile time rather than through a vtable on the frame
// path - and, more usefully, so that "did I implement all of it" is a compile
// error. The software backend is written FIRST on purpose: it keeps raster and
// composition testable headlessly and byte-for-byte before any GPU code exists,
// and CI has no GPU.
//
// One part of the contract a concept cannot express, so it is stated here and
// checked by the tests instead:
//
//   raster(t, dl) MUST be safe to call concurrently for DISTINCT tile ids.
//
// That is the whole point of tiling, and it is why the compositor may fan tiles
// across the pool. begin_frame, composite and end_frame are single-threaded and
// are called by the compositor thread only.

export namespace ctbrowser::raster {

enum class gpu_error : std::uint8_t {
	device_lost,
	out_of_memory,
	unsupported,
	no_frame, // raster/composite called outside begin_frame..end_frame
	bad_tile, // a tile id the backend has no storage for
};

// Identifies the in-flight frame. Opaque on purpose: on the GPU path it will
// carry a command buffer, and callers must not care.
struct frame_token {
	std::uint64_t id = 0;
	[[nodiscard]] friend constexpr bool operator==(frame_token, frame_token) = default;
};

// Two additions to the plan's sketch of this concept, both forced:
//
//   reserve_tiles  raster() runs on many threads for distinct tiles, so the
//                  storage they write into must already exist and must not be
//                  reallocated underneath them.
//   needs_raster   a tile that is still valid from a previous frame must not be
//                  redrawn. Without this the compositor cannot do incremental
//                  raster at all, and a scroll pays for the whole page again.
//   tile_ready     called ON THE COMPOSITOR THREAD as each tile finishes, so a
//                  backend can start moving its pixels while the rest of the
//                  page is still being drawn. The software backend has nothing
//                  to do here; the GPU backend uploads the tile's texture, and
//                  doing that as tiles land rather than in one burst at
//                  composite time is the entire reason the handoff exists.
//
// A GPU backend needs both for the same reasons - tile textures have to be
// allocated before a command buffer references them, and re-rastering a valid
// tile wastes exactly as much there - so neither is a software accommodation
// leaking into the interface.
template <typename B>
concept RasterBackend = requires(B & b, tile_id t, std::span<const tile> ts,
                                 const paint::display_list & dl, std::span<const paint::layer> ls) {
	{ b.begin_frame() } -> std::same_as<std::expected<frame_token, gpu_error>>;
	{ b.reserve_tiles(ts) } -> std::same_as<std::expected<void, gpu_error>>;
	{ b.needs_raster(t) } -> std::same_as<bool>;
	{ b.raster(t, dl) } -> std::same_as<std::expected<void, gpu_error>>;
	{ b.tile_ready(t) } -> std::same_as<void>;
	{ b.composite(ls) } -> std::same_as<std::expected<void, gpu_error>>;
	{ b.end_frame() } -> std::same_as<std::expected<void, gpu_error>>;
	{ B::is_hardware } -> std::convertible_to<bool>;
};

} // namespace ctbrowser::raster
