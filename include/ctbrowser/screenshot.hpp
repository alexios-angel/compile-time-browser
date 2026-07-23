#ifndef CTBROWSER__SCREENSHOT__HPP
#define CTBROWSER__SCREENSHOT__HPP

#include <cstdint>

#include <cstddef>

#include <SDL3/SDL.h>
// vendored third-party code compiles outside our -Werror regime
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"
#pragma GCC diagnostic pop
#ifndef CTBROWSER_IN_A_MODULE
#include <fstream>
#include <string>
#include <string_view>
#endif

// Screenshots: read the renderer's pixels back and write a PNG (via
// the vendored public-domain stb_image_write). Works under
// SDL_VIDEODRIVER=dummy too - the software renderer supports readback
// - which is how the render tests and CI capture what a page actually
// drew.

namespace ctbrowser {

namespace detail {

// binary PPM (P6): trivially parseable raw pixels - what the render
// tests byte-compare as goldens
inline bool write_ppm(const char * path, std::int32_t w, std::int32_t h, const unsigned char * rgba,
                      std::int32_t pitch) {
	std::ofstream out{path, std::ios::binary};
	out << "P6\n" << w << ' ' << h << "\n255\n";
	for (std::int32_t y = 0; y < h; ++y) {
		const unsigned char * row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch);
		for (std::int32_t x = 0; x < w; ++x) {
			out.write(reinterpret_cast<const char *>(row + x * 4), 3); // RGB of RGBA
		}
	}
	return static_cast<bool>(out);
}

} // namespace detail

// capture the current render target; ".ppm" paths write binary PPM
// (raw pixels, golden-comparable), everything else writes PNG
inline bool save_screenshot(SDL_Renderer * renderer, const char * path) {
	SDL_Surface * shot = SDL_RenderReadPixels(renderer, nullptr);
	if (shot == nullptr) { return false; }
	SDL_Surface * rgba = shot;
	if (shot->format != SDL_PIXELFORMAT_RGBA32) {
		rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(shot);
		if (rgba == nullptr) { return false; }
	}
	const std::string_view p{path};
	bool ok;
	if (p.size() > 4 && p.substr(p.size() - 4) == ".ppm") {
		ok = detail::write_ppm(path, rgba->w, rgba->h,
		                       static_cast<const unsigned char *>(rgba->pixels), rgba->pitch);
	} else {
		ok = stbi_write_png(path, rgba->w, rgba->h, 4, rgba->pixels, rgba->pitch) != 0;
	}
	SDL_DestroySurface(rgba);
	return ok;
}

} // namespace ctbrowser

#endif
