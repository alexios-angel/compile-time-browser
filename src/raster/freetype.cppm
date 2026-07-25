module;

#if CTBROWSER_WITH_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module ctbrowser.raster:freetype;

import ctbrowser.core;
import ctbrowser.paint;
import :surface;
import :draw;

// A real font backend, over FreeType.
//
// NOT over SDL3_ttf, which is what the plan proposed. SDL3_ttf would have put
// SDL inside the engine, and the engine being SDL-free is a rule this tree
// enforces with a test (tests-v2/api_surface) - so the plan's own placement
// contradicted the plan's own architecture. FreeType has no such problem: it is
// an ordinary C library, the engine stays headless, and the consequence is that
// REAL FONTS ARE TESTABLE without a display and comparable in a golden.
//
// OPTIONAL. Without FreeType at configure time this module still compiles and
// `freetype_available()` is false; the browser keeps font8x8 and every golden
// still matches. That is the same shape as the GPU backend: present when the
// machine has it, and honest when it does not.
//
// THREAD SAFETY. Tiles raster in parallel, and an FT_Face is not reentrant, so
// glyphs are rasterized ONCE under a mutex into an immutable cache and drawn
// from it afterwards. std::map's nodes are stable, so a pointer taken under the
// lock stays valid while other threads insert.

export namespace ctbrowser::raster {

[[nodiscard]] constexpr bool freetype_available() noexcept {
#if CTBROWSER_WITH_FREETYPE
	return true;
#else
	return false;
#endif
}

#if CTBROWSER_WITH_FREETYPE

class freetype_backend final : public font_backend {
public:
	freetype_backend() {
		if (FT_Init_FreeType(&library_) != 0) { library_ = nullptr; }
	}
	~freetype_backend() override {
		for (auto & [key, face] : faces_) {
			if (face.handle != nullptr) { FT_Done_Face(face.handle); }
		}
		if (library_ != nullptr) { FT_Done_FreeType(library_); }
	}

	[[nodiscard]] bool ok() const noexcept { return library_ != nullptr; }

	// Register one face. The BYTES ARE COPIED and kept: FreeType reads from the
	// buffer for the lifetime of the face, so handing it a span of somebody
	// else's vector is a use-after-free waiting for the first garbage-collected
	// page.
	bool add_face(std::string family, bool bold, bool italic, std::span<const std::byte> bytes) {
		if (library_ == nullptr || bytes.empty()) { return false; }
		const std::lock_guard guard{mutex_};
		loaded_face entry;
		entry.bytes.assign(bytes.begin(), bytes.end());
		if (FT_New_Memory_Face(library_, reinterpret_cast<const FT_Byte *>(entry.bytes.data()),
		                       static_cast<FT_Long>(entry.bytes.size()), 0, &entry.handle) != 0) {
			return false;
		}
		faces_[face_key{lowered(family), bold, italic}] = std::move(entry);
		return true;
	}

	[[nodiscard]] std::size_t face_count() const {
		const std::lock_guard guard{mutex_};
		return faces_.size();
	}
	// The family a run would actually be drawn in, which is what makes the
	// fallback chain observable instead of a guess.
	[[nodiscard]] bool has_face(std::string_view family, bool bold, bool italic) const {
		const std::lock_guard guard{mutex_};
		return faces_.find(face_key{lowered(family), bold, italic}) != faces_.end();
	}

	[[nodiscard]] float advance(std::string_view text, float font_size, std::string_view family,
	                            bool bold, bool italic) const override {
		const int size = pixel_size(font_size);
		float total = 0;
		for_each_code_point(text, [&](char32_t cp) {
			if (const glyph * g = glyph_for(family, bold, italic, size, cp)) {
				total += g->advance;
			} else {
				total += font8x8_fonts().advance(" ", font_size, family, bold, italic);
			}
		});
		return total;
	}

	void draw_run(const rect & where, const paint_command & c, const pixel_rect & clip,
	              surface & into) const override {
		const int size = pixel_size(c.font_size);
		// The run's box top is the top of the LINE, and glyphs sit on a
		// baseline inside it - the same convention font8x8 uses, so a page that
		// mixes backends does not step up and down.
		const float baseline =
		    where.y + ascent(c.font_size, c.face.family, c.face.bold, c.face.italic);
		float pen = where.x;
		for_each_code_point(c.text, [&](char32_t cp) {
			const glyph * g = glyph_for(c.face.family, c.face.bold, c.face.italic, size, cp);
			if (g == nullptr) {
				pen += font8x8_fonts().advance(" ", c.font_size, c.face.family, c.face.bold,
				                               c.face.italic);
				return;
			}
			blit(*g, pen, baseline, c.fill, clip, into);
			pen += g->advance;
		});
	}

	[[nodiscard]] float ascent(float font_size, std::string_view family, bool bold,
	                           bool italic) const override {
		const int size = pixel_size(font_size);
		const std::lock_guard guard{mutex_};
		const loaded_face * face = resolve(family, bold, italic);
		if (face == nullptr) {
			return font8x8_fonts().ascent(font_size, family, bold, italic);
		}
		if (FT_Set_Pixel_Sizes(face->handle, 0, static_cast<FT_UInt>(size)) != 0) { return font_size; }
		return static_cast<float>(face->handle->size->metrics.ascender) / 64.0f;
	}

private:
	struct face_key {
		std::string family;
		bool bold = false;
		bool italic = false;
		[[nodiscard]] friend auto operator<=>(const face_key &, const face_key &) = default;
	};
	struct loaded_face {
		std::vector<std::byte> bytes; // FreeType reads these for the face's life
		FT_Face handle = nullptr;
	};
	struct glyph_key {
		face_key face;
		int size = 0;
		char32_t code = 0;
		[[nodiscard]] friend auto operator<=>(const glyph_key &, const glyph_key &) = default;
	};
	struct glyph {
		int width = 0;
		int height = 0;
		int left = 0;   // from the pen, positive right
		int top = 0;    // from the baseline, positive up
		float advance = 0;
		std::vector<std::uint8_t> coverage; // width * height, 0..255
	};

	[[nodiscard]] static std::string lowered(std::string_view text) {
		std::string out{text};
		std::ranges::transform(out, out.begin(),
		                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}
	[[nodiscard]] static int pixel_size(float font_size) noexcept {
		const int size = static_cast<int>(font_size + 0.5f);
		return size < 1 ? 1 : size;
	}

	template <typename Fn> static void for_each_code_point(std::string_view text, Fn && fn) {
		for (std::size_t i = 0; i < text.size();) {
			const auto byte = static_cast<unsigned char>(text[i]);
			char32_t cp = byte;
			std::size_t length = 1;
			if (byte >= 0xF0u) {
				length = 4;
				cp = static_cast<char32_t>(byte & 0x07u);
			} else if (byte >= 0xE0u) {
				length = 3;
				cp = static_cast<char32_t>(byte & 0x0Fu);
			} else if (byte >= 0xC0u) {
				length = 2;
				cp = static_cast<char32_t>(byte & 0x1Fu);
			}
			for (std::size_t k = 1; k < length && i + k < text.size(); ++k) {
				cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
			}
			i += length;
			fn(cp);
		}
	}

	// The face a request resolves to. An unknown family falls back to the
	// DEFAULT one rather than drawing nothing, and a missing bold/italic
	// variant falls back to the upright one - which is what a browser does with
	// a family it only has one weight of.
	[[nodiscard]] const loaded_face * resolve(std::string_view family, bool bold,
	                                          bool italic) const {
		const std::string name = lowered(family);
		for (const face_key & candidate : {face_key{name, bold, italic}, face_key{name, false, false},
		                                   face_key{default_family_, bold, italic},
		                                   face_key{default_family_, false, false}}) {
			if (candidate.family.empty()) { continue; }
			if (const auto it = faces_.find(candidate); it != faces_.end()) { return &it->second; }
		}
		return faces_.empty() ? nullptr : &faces_.begin()->second;
	}

	// Rasterized once, then read. The lookup is under the lock; the BLIT is
	// not, which is safe because a glyph never changes after it is inserted and
	// std::map does not move its nodes.
	[[nodiscard]] const glyph * glyph_for(std::string_view family, bool bold, bool italic, int size,
	                                      char32_t code) const {
		const std::lock_guard guard{mutex_};
		const loaded_face * face = resolve(family, bold, italic);
		if (face == nullptr) { return nullptr; }
		const glyph_key key{face_key{lowered(family), bold, italic}, size, code};
		if (const auto it = glyphs_.find(key); it != glyphs_.end()) {
			return it->second.width >= 0 ? &it->second : nullptr;
		}
		if (FT_Set_Pixel_Sizes(face->handle, 0, static_cast<FT_UInt>(size)) != 0) { return nullptr; }
		if (FT_Load_Char(face->handle, code, FT_LOAD_RENDER) != 0) { return nullptr; }
		const FT_GlyphSlot slot = face->handle->glyph;

		glyph made;
		made.width = static_cast<int>(slot->bitmap.width);
		made.height = static_cast<int>(slot->bitmap.rows);
		made.left = slot->bitmap_left;
		made.top = slot->bitmap_top;
		made.advance = static_cast<float>(slot->advance.x) / 64.0f;
		made.coverage.resize(static_cast<std::size_t>(made.width) *
		                     static_cast<std::size_t>(made.height));
		for (int y = 0; y < made.height; ++y) {
			for (int x = 0; x < made.width; ++x) {
				made.coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(made.width) +
				              static_cast<std::size_t>(x)] =
				    slot->bitmap.buffer[static_cast<std::size_t>(y) *
				                            static_cast<std::size_t>(slot->bitmap.pitch) +
				                        static_cast<std::size_t>(x)];
			}
		}
		return &glyphs_.emplace(key, std::move(made)).first->second;
	}

	// Antialiased: the glyph's coverage scales the text colour's alpha, which
	// is what makes an outline font look like one.
	static void blit(const glyph & g, float pen_x, float baseline, color fill,
	                 const pixel_rect & clip, surface & into) {
		const int left = static_cast<int>(pen_x + 0.5f) + g.left;
		const int top = static_cast<int>(baseline + 0.5f) - g.top;
		for (int y = 0; y < g.height; ++y) {
			const int py = top + y;
			if (py < clip.top || py >= clip.bottom) { continue; }
			const std::span<std::uint32_t> row = into.row(py);
			for (int x = 0; x < g.width; ++x) {
				const int px = left + x;
				if (px < clip.left || px >= clip.right) { continue; }
				const std::uint8_t coverage =
				    g.coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(g.width) +
				               static_cast<std::size_t>(x)];
				if (coverage == 0) { continue; }
				const auto alpha = static_cast<std::uint8_t>(
				    (static_cast<std::uint32_t>(fill.alpha()) * coverage) / 255u);
				row[static_cast<std::size_t>(px)] =
				    blend_over(row[static_cast<std::size_t>(px)],
				               color::rgba(fill.red(), fill.green(), fill.blue(), alpha));
			}
		}
	}

	FT_Library library_ = nullptr;
	mutable std::mutex mutex_;
	std::map<face_key, loaded_face> faces_;
	mutable std::map<glyph_key, glyph> glyphs_;
	std::string default_family_;

public:
	// Which family an unknown one falls back to. Set once, when the faces are
	// registered.
	void set_default_family(std::string family) {
		const std::lock_guard guard{mutex_};
		default_family_ = lowered(family);
	}
};

#endif // CTBROWSER_WITH_FREETYPE

} // namespace ctbrowser::raster
