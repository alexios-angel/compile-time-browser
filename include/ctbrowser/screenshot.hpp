#ifndef CTBROWSER__SCREENSHOT__HPP
#define CTBROWSER__SCREENSHOT__HPP

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
inline bool write_ppm(const char * path, int w, int h, const unsigned char * rgba,
                      int pitch) {
	SDL_IOStream * io = SDL_IOFromFile(path, "wb");
	if (io == nullptr) { return false; }
	char header[64];
	const int n = SDL_snprintf(header, sizeof(header), "P6\n%d %d\n255\n", w, h);
	SDL_WriteIO(io, header, static_cast<size_t>(n));
	for (int y = 0; y < h; ++y) {
		const unsigned char * row = rgba + static_cast<size_t>(y) * static_cast<size_t>(pitch);
		for (int x = 0; x < w; ++x) {
			SDL_WriteIO(io, row + x * 4, 3); // RGB of RGBA
		}
	}
	SDL_CloseIO(io);
	return true;
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
