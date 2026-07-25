module;
#include "font8x8.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

export module ctbrowser.raster:draw;

import ctbrowser.core;
import ctbrowser.paint;
import :surface;

// Turning display-list commands into pixels, in ONE place.
//
// Both backends rasterize with this. That is the point: the GPU backend's
// hardware acceleration is in COMPOSITION, not in raster, so if it had its own
// rasterizer the two would drift and "the GPU image matches the software image"
// would stop being a test and start being a coincidence. Sharing the code makes
// the differential test meaningful.
//
// Text is font8x8 rather than a real font stack because a golden that depends
// on which fonts a machine happens to have is not a golden.

export namespace ctbrowser::raster {

using ctbrowser::color;
using ctbrowser::paint::display_list;
using ctbrowser::paint::paint_command;
using ctbrowser::paint::paint_op;

// font8x8 draws each glyph in an 8x8 cell scaled by an integer factor, so its
// advance is exact and has no font metrics behind it. Layout must measure with
// THIS if the raster is to place text where layout thought it would.
[[nodiscard]] inline int font8x8_scale(float font_size) noexcept {
	const int s = static_cast<int>(font_size / 8.0f + 0.5f);
	return s < 1 ? 1 : s;
}

[[nodiscard]] inline std::size_t utf8_length(std::string_view text) noexcept {
	std::size_t n = 0;
	for (const char c : text) {
		if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) { ++n; }
	}
	return n;
}

[[nodiscard]] inline float font8x8_advance(std::string_view text, float font_size) noexcept {
	return static_cast<float>(utf8_length(text) * 8u *
	                          static_cast<std::size_t>(font8x8_scale(font_size)));
}


inline void fill_rect(const rect & where, color c, const pixel_rect & clip, surface & into) {
	pixel_rect p = to_pixels(where, into.width(), into.height());
	p.left = std::max(p.left, clip.left);
	p.top = std::max(p.top, clip.top);
	p.right = std::min(p.right, clip.right);
	p.bottom = std::min(p.bottom, clip.bottom);
	if (p.empty()) { return; }
	for (int y = p.top; y < p.bottom; ++y) {
		const std::span<std::uint32_t> row = into.row(y);
		for (int x = p.left; x < p.right; ++x) {
			row[static_cast<std::size_t>(x)] = blend_over(row[static_cast<std::size_t>(x)], c);
		}
	}
}

// font8x8: an 8x8 bitmap per code point, scaled by an integer factor. The
// run's box top is the TOP of the cell, matching how layout positions a line.
inline void draw_text(const rect & where, const paint_command & c, const pixel_rect & clip,
                      surface & into) {
	const int scale = font8x8_scale(c.font_size);
	const int origin_x = round_to_pixel(where.x);
	const int origin_y = round_to_pixel(where.y);
	int cell = 0;
	for (std::size_t i = 0; i < c.text.size();) {
		const auto byte = static_cast<unsigned char>(c.text[i]);
		char32_t cp = byte;
		std::size_t advance = 1;
		if (byte >= 0xF0u) {
			advance = 4;
			cp = 0xFFFDu;
		} else if (byte >= 0xE0u) {
			advance = 3;
			cp = 0xFFFDu;
		} else if (byte >= 0xC0u) {
			advance = 2;
			cp = 0xFFFDu;
		}
		i += advance;
		const int left = origin_x + cell * 8 * scale;
		++cell;
		if (cp > 0x7F) { continue; } // outside font8x8; the cell is still advanced
		for (int gy = 0; gy < 8; ++gy) {
			for (int gx = 0; gx < 8; ++gx) {
				if (!font8x8_data::glyph_pixel(cp, gy, gx)) { continue; }
				const int px = left + gx * scale;
				const int py = origin_y + gy * scale;
				for (int sy = 0; sy < scale; ++sy) {
					const int y = py + sy;
					if (y < clip.top || y >= clip.bottom) { continue; }
					const std::span<std::uint32_t> row = into.row(y);
					for (int sx = 0; sx < scale; ++sx) {
						const int x = px + sx;
						if (x < clip.left || x >= clip.right) { continue; }
						row[static_cast<std::size_t>(x)] =
						    blend_over(row[static_cast<std::size_t>(x)], c.fill);
					}
				}
			}
		}
	}
}

inline void draw_commands(const std::vector<paint_command> & commands, const rect & area,
                          surface & into) {
	// Clip stack in tile-local pixels. push_clip intersects, pop restores.
	std::vector<pixel_rect> clips;
	pixel_rect clip{0, 0, into.width(), into.height()};
	for (const paint_command & c : commands) {
		const rect local = c.bounds.translated(-area.x, -area.y);
		switch (c.op) {
		case paint_op::push_clip: {
			clips.push_back(clip);
			const pixel_rect next = to_pixels(local, into.width(), into.height());
			clip = pixel_rect{std::max(clip.left, next.left), std::max(clip.top, next.top),
			                  std::min(clip.right, next.right), std::min(clip.bottom, next.bottom)};
			break;
		}
		case paint_op::pop_clip:
			if (!clips.empty()) {
				clip = clips.back();
				clips.pop_back();
			}
			break;
		case paint_op::fill_rect: fill_rect(local, c.fill, clip, into); break;
		case paint_op::text_run: draw_text(local, c, clip, into); break;
		}
	}
}

// Rasterize whatever `list` draws inside `area` into `into`, which is that
// area's tile. The tile starts transparent: the page background is composited
// under it, not baked into it, so a tile stays valid when the background
// changes and when it is reused under a different layer.
inline void draw_into(surface & into, const display_list & list, const rect & area) {
	into.fill(color{0});
	draw_commands(list.intersecting(area), area, into);
}

} // namespace ctbrowser::raster
