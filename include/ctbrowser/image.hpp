#ifndef CTBROWSER__IMAGE__HPP
#define CTBROWSER__IMAGE__HPP

#include <cstddef>

#ifndef CTBROWSER_IN_A_MODULE
#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#endif

// Sprite images for the canvas: a minimal BMP reader (uncompressed
// 24/32bpp, the kind every image tool can write) into a plain
// 0xAARRGGBB pixel buffer, plus the registry scripts address images
// through. Deliberately SDL-free so sprite drawing is testable
// headless - the canvas already composes into a plain pixel buffer.

namespace ctbrowser {

struct image {
	std::int32_t w = 0;
	std::int32_t h = 0;
	std::vector<uint32_t> pixels; // 0xAARRGGBB, top-down

	bool ok() const { return w > 0 && h > 0 && !pixels.empty(); }
};

namespace detail {

inline uint32_t read_u32(const unsigned char * p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint16_t read_u16(const unsigned char * p) {
	return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
	                             (static_cast<uint16_t>(p[1]) << 8));
}

} // namespace detail

// an asset baked into the BINARY at compile time (std::embed / #embed
// when the compiler has it - see examples/invaders.cpp); loaders
// consult these before touching the filesystem
struct embedded_asset {
	std::string path; // the exact string scripts pass to loadImage/playSound
	const unsigned char * data = nullptr;
	std::size_t size = 0;
};

inline const embedded_asset * find_asset(const std::vector<embedded_asset> * assets,
                                         std::string_view path) {
	if (assets == nullptr) { return nullptr; }
	for (const embedded_asset & a : *assets) {
		if (a.path == path) { return &a; }
	}
	return nullptr;
}

// parse an uncompressed 24- or 32-bit BMP from memory; image.ok() ==
// false on any problem (truncated, unsupported flavour)
inline image parse_bmp(const unsigned char * data, std::size_t size) {
	if (data == nullptr || size < 54 || data[0] != 'B' || data[1] != 'M') { return {}; }
	const uint32_t pixel_offset = detail::read_u32(&data[10]);
	const uint32_t header_size = detail::read_u32(&data[14]);
	if (header_size < 40) { return {}; }
	const int32_t w = static_cast<int32_t>(detail::read_u32(&data[18]));
	const int32_t raw_h = static_cast<int32_t>(detail::read_u32(&data[22]));
	const uint16_t bpp = detail::read_u16(&data[28]);
	const uint32_t compression = detail::read_u32(&data[30]);
	if (w <= 0 || raw_h == 0 || (bpp != 24 && bpp != 32) ||
	    (compression != 0 && compression != 3)) {
		return {};
	}
	const bool top_down = raw_h < 0;
	const int32_t h = top_down ? -raw_h : raw_h;
	const std::size_t bytes_pp = bpp / 8u;
	const std::size_t row = (static_cast<std::size_t>(w) * bytes_pp + 3u) & ~std::size_t{3};
	if (size < pixel_offset + row * static_cast<std::size_t>(h)) { return {}; }

	image out;
	out.w = w;
	out.h = h;
	out.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
	for (int32_t y = 0; y < h; ++y) {
		const int32_t src_y = top_down ? y : h - 1 - y;
		const unsigned char * line = &data[pixel_offset + row * static_cast<std::size_t>(src_y)];
		for (int32_t x = 0; x < w; ++x) {
			const unsigned char * px = line + static_cast<std::size_t>(x) * bytes_pp;
			const uint32_t b = px[0];
			const uint32_t g = px[1];
			const uint32_t r = px[2];
			const uint32_t a = bytes_pp == 4 ? px[3] : 0xFFu;
			out.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
			           static_cast<std::size_t>(x)] =
			    (a << 24) | (r << 16) | (g << 8) | b;
		}
	}
	return out;
}

// read a BMP from disk (see parse_bmp for the supported flavours)
inline image load_bmp(const std::string & path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) { return {}; }
	const std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
	                                      std::istreambuf_iterator<char>());
	return parse_bmp(data.data(), data.size());
}

// the registry scripts hold image handles into (index = the handle)
struct image_store {
	std::vector<image> images;
	// shell-installed decoder for formats beyond BMP (SDL3_image gives
	// PNG/JPG/WebP); tried when the built-in reader says no
	std::function<image(const std::string &)> decoder;
	// compile-time-embedded assets, consulted before the filesystem
	const std::vector<embedded_asset> * embedded = nullptr;

	// returns the handle, or -1 when loading failed
	std::int32_t load(const std::string & path) {
		image im;
		if (const embedded_asset * a = find_asset(embedded, path)) {
			im = parse_bmp(a->data, a->size);
		}
		if (!im.ok()) { im = load_bmp(path); }
		if (!im.ok() && decoder) { im = decoder(path); }
		if (!im.ok()) { return -1; }
		images.push_back(std::move(im));
		return static_cast<std::int32_t>(images.size()) - 1;
	}
	const image * get(std::int32_t handle) const {
		if (handle < 0 || static_cast<std::size_t>(handle) >= images.size()) { return nullptr; }
		return &images[static_cast<std::size_t>(handle)];
	}
};

} // namespace ctbrowser

#endif
