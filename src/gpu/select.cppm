module;
#include <SDL3/SDL.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

export module ctbrowser.gpu:select;

import ctbrowser.raster;
import :device;

// Choosing a renderer at startup, and falling back when the GPU is not there.
//
// This is the whole reason raster::renderer is type-erased. Every one of these
// is a real machine somebody runs a browser on:
//
//   * no GPU at all - a container, a CI runner, a headless server
//   * a GPU whose driver does not expose Vulkan, or exposes a broken one
//   * a user who has asked for software rendering because the GPU path
//     misbehaves on their hardware
//
// A browser that only works on the first kind of machine is not finished. So
// the GPU is TRIED, and software is what happens when trying does not work -
// which is a normal outcome to be reported, not an error to be propagated.

export namespace ctbrowser::gpu {

enum class renderer_preference : std::uint8_t {
	automatic, // try the GPU, fall back to software
	force_gpu, // fail rather than fall back - for tests and for bug reports
	force_software,
};

// What the environment asks for. CTBROWSER_RENDERER=software is the escape
// hatch every browser needs and the switch that makes the fallback path
// exercisable on a machine where the GPU works.
[[nodiscard]] inline renderer_preference preference_from_environment() {
	const char * value = std::getenv("CTBROWSER_RENDERER");
	if (value == nullptr) { return renderer_preference::automatic; }
	const std::string_view text{value};
	if (text == "software" || text == "cpu") { return renderer_preference::force_software; }
	if (text == "gpu" || text == "hardware") { return renderer_preference::force_gpu; }
	return renderer_preference::automatic;
}

struct renderer_choice {
	raster::renderer renderer;
	// Why software was chosen, when it was not what was asked for. Empty when
	// the GPU came up or when software was requested outright.
	std::string fell_back_because;

	// True when a GPU device WAS created but its adapter is a software
	// implementation of the graphics API - lavapipe, SwiftShader, WARP. The
	// path is hardware-accelerated in name only, and anything reporting
	// performance has to say so.
	bool software_adapter = false;

	[[nodiscard]] bool hardware() const noexcept { return renderer.hardware(); }
};

// Build the renderer this machine can actually run.
//
// `window` null means offscreen, which is how tests and headless runs work.
// Returns an empty renderer only if force_gpu was asked for and the GPU failed:
// a caller that demanded hardware gets told, rather than silently getting
// something else.
[[nodiscard]] inline renderer_choice create_renderer(
    int width, int height, SDL_Window * window = nullptr, int extent = raster::default_tile_extent,
    renderer_preference want = renderer_preference::automatic) {
	renderer_choice out;
	if (want == renderer_preference::force_software) {
		out.renderer = raster::renderer::software(width, height, extent);
		return out;
	}

	auto device = sdl_gpu_backend::create(width, height, window, extent);
	if (device) {
		// The adapter goes in the name because "gpu" alone is misleading when
		// the adapter is llvmpipe. A user reading a bug report, or a benchmark
		// reading its own output, needs to know which it got.
		const std::string name = "gpu (" + device->driver() + ": " + device->adapter() + ")";
		out.software_adapter = device->adapter_is_software();
		out.renderer =
		    raster::renderer::adopt(std::make_unique<sdl_gpu_backend>(std::move(*device)), name);
		return out;
	}

	out.fell_back_because = std::string{describe(device.error())};
	if (want == renderer_preference::force_gpu) { return out; } // empty renderer, on purpose
	out.renderer = raster::renderer::software(width, height, extent);
	return out;
}

} // namespace ctbrowser::gpu
