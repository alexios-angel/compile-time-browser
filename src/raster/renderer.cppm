module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

export module ctbrowser.raster:renderer;

import ctbrowser.core;
import ctbrowser.paint;
import :backend;
import :software;
import :surface;
import :tile;

// One renderer, chosen at STARTUP, used through the same interface either way.
//
// The RasterBackend concept is a compile-time seam, and that is right for the
// frame path - but which backend a browser gets is a runtime fact. There may be
// no GPU, the driver may be broken, the user may have asked for software, or
// the device may be lost mid-session. A browser that cannot fall back is a
// browser that shows nothing on those machines.
//
// So this is the runtime seam: a small hand-written vtable over any type that
// satisfies RasterBackend. It satisfies RasterBackend ITSELF, which means the
// compositor thread, draw() and every test work through it unchanged.
//
// The cost is one indirect call per backend operation. Per FRAME that is
// nothing; per TILE (raster, tile_ready) it is a predicted indirect branch in
// front of work measured in microseconds. Paying it to get a browser that
// starts on a machine with no GPU is not a close call.

export namespace ctbrowser::raster {

using ctbrowser::paint::display_list;
using ctbrowser::paint::layer;

class renderer {
public:
	// Take ownership of any backend. `name` is for diagnostics and for the test
	// that checks which one was actually chosen.
	template <RasterBackend B>
	[[nodiscard]] static renderer adopt(std::unique_ptr<B> backend, std::string name) {
		renderer out;
		out.name_ = std::move(name);
		out.hardware_ = B::is_hardware;
		out.self_ = backend.release();
		out.ops_ = &ops_for<B>;
		return out;
	}

	// The always-available one. Every machine can run this.
	[[nodiscard]] static renderer software(int width, int height,
	                                       int extent = default_tile_extent) {
		return adopt(std::make_unique<software_backend>(width, height, extent), "software");
	}

	renderer() = default;
	renderer(renderer && other) noexcept { steal(std::move(other)); }
	renderer & operator=(renderer && other) noexcept {
		if (this != &other) {
			destroy();
			steal(std::move(other));
		}
		return *this;
	}
	renderer(const renderer &) = delete;
	renderer & operator=(const renderer &) = delete;
	~renderer() { destroy(); }

	[[nodiscard]] explicit operator bool() const noexcept { return self_ != nullptr; }
	// The RUNTIME answer. is_hardware below is the concept's compile-time one,
	// and for a type-erased renderer it cannot be anything but false - which
	// backend was chosen is not known until startup. Ask this instead.
	[[nodiscard]] bool hardware() const noexcept { return hardware_; }
	[[nodiscard]] std::string_view name() const noexcept { return name_; }

	// --- RasterBackend ---------------------------------------------------

	static constexpr bool is_hardware = false; // compile-time; see hardware()

	[[nodiscard]] std::expected<frame_token, gpu_error> begin_frame() {
		return ops_->begin_frame(self_);
	}
	[[nodiscard]] std::expected<void, gpu_error> reserve_tiles(std::span<const tile> tiles) {
		return ops_->reserve_tiles(self_, tiles);
	}
	[[nodiscard]] bool needs_raster(tile_id id) const { return ops_->needs_raster(self_, id); }
	[[nodiscard]] std::expected<void, gpu_error> raster(tile_id id, const display_list & list) {
		return ops_->raster(self_, id, list);
	}
	void tile_ready(tile_id id) { ops_->tile_ready(self_, id); }
	[[nodiscard]] std::expected<void, gpu_error> composite(std::span<const layer> layers) {
		return ops_->composite(self_, layers);
	}
	[[nodiscard]] std::expected<void, gpu_error> end_frame() { return ops_->end_frame(self_); }

	// Every tile is stale. What a relayout calls.
	void discard() { ops_->discard(self_); }

	// The composited image, when the backend can produce one. The software
	// backend always can; a GPU backend can only when it is offscreen, which is
	// what makes the two comparable in tests.
	[[nodiscard]] std::expected<surface, gpu_error> read_target() {
		return ops_->read_target(self_);
	}

	// The concrete backend, when a caller needs something outside the concept.
	// Returns null if the renderer is not holding that type.
	template <typename B> [[nodiscard]] B * get_if() noexcept {
		return ops_ == &ops_for<B> ? static_cast<B *>(self_) : nullptr;
	}

private:
	struct vtable {
		std::expected<frame_token, gpu_error> (*begin_frame)(void *);
		std::expected<void, gpu_error> (*reserve_tiles)(void *, std::span<const tile>);
		bool (*needs_raster)(const void *, tile_id);
		std::expected<void, gpu_error> (*raster)(void *, tile_id, const display_list &);
		void (*tile_ready)(void *, tile_id);
		std::expected<void, gpu_error> (*composite)(void *, std::span<const layer>);
		std::expected<void, gpu_error> (*end_frame)(void *);
		void (*discard)(void *);
		std::expected<surface, gpu_error> (*read_target)(void *);
		void (*destroy)(void *);
	};

	// A backend that cannot read its own output says so rather than pretending.
	// The on-screen GPU path is the real case: its target is a swapchain image
	// that belongs to the window system.
	template <typename B>
	[[nodiscard]] static std::expected<surface, gpu_error> read_target_of(B & backend) {
		if constexpr (requires { backend.read_target(); }) {
			return backend.read_target();
		} else if constexpr (requires { backend.target(); }) {
			return backend.target();
		} else {
			return std::unexpected(gpu_error::unsupported);
		}
	}

	template <typename B> static constexpr vtable ops_for{
	    [](void * s) { return static_cast<B *>(s)->begin_frame(); },
	    [](void * s, std::span<const tile> t) { return static_cast<B *>(s)->reserve_tiles(t); },
	    [](const void * s, tile_id id) { return static_cast<const B *>(s)->needs_raster(id); },
	    [](void * s, tile_id id, const display_list & l) { return static_cast<B *>(s)->raster(id, l); },
	    [](void * s, tile_id id) { static_cast<B *>(s)->tile_ready(id); },
	    [](void * s, std::span<const layer> l) { return static_cast<B *>(s)->composite(l); },
	    [](void * s) { return static_cast<B *>(s)->end_frame(); },
	    [](void * s) { static_cast<B *>(s)->discard(); },
	    [](void * s) { return read_target_of(*static_cast<B *>(s)); },
	    [](void * s) { delete static_cast<B *>(s); },
	};

	void steal(renderer && other) noexcept {
		self_ = std::exchange(other.self_, nullptr);
		ops_ = std::exchange(other.ops_, nullptr);
		name_ = std::move(other.name_);
		hardware_ = other.hardware_;
	}
	void destroy() {
		if (self_ != nullptr && ops_ != nullptr) { ops_->destroy(self_); }
		self_ = nullptr;
		ops_ = nullptr;
	}

	void * self_ = nullptr;
	const vtable * ops_ = nullptr;
	std::string name_;
	bool hardware_ = false;
};

static_assert(RasterBackend<renderer>);

} // namespace ctbrowser::raster
