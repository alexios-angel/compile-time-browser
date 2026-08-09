#pragma once
#if CTBROWSER_WITH_TTF
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>

#include <ctbrowser/raster/draw.hpp>
#include <ctbrowser/raster/surface.hpp>

// Real fonts, through SDL3_ttf.
//
// THIS IS THE ONE PLACE THE ENGINE KNOWS ABOUT SDL, and it is a deliberate
// exception rather than an oversight. Everything else in ctbrowser.raster,
// .shell and below is SDL-free so the browser can be driven headlessly, and
// tests/api_surface enforces that - with this module named as the exception.
//
// The trade: SDL3_ttf is one dependency where FreeType plus a glyph pipeline
// was two, and it is the same library the window and the audio already come
// from. TTF_Init needs no video subsystem, so real text is still testable with
// no display - which is what made this swap safe to make.
//
// OPTIONAL, like everything else here: without SDL3_ttf the module still
// compiles, `ttf_available()` is false, and text renders with font8x8, which is
// what keeps the goldens reproducible on a machine with no font libraries.
//
// THREAD SAFETY. Tiles raster in parallel and a TTF_Font is not reentrant, so
// glyphs are rasterized ONCE under a mutex into an immutable cache and drawn
// from it afterwards. std::map's nodes are stable, so a pointer taken under the
// lock stays valid while other threads insert.

namespace ctbrowser::raster {

[[nodiscard]] constexpr bool ttf_available() noexcept {
#if CTBROWSER_WITH_TTF
    return true;
#else
    return false;
#endif
}

#if CTBROWSER_WITH_TTF

class ttf_backend final : public font_backend {
public:
    ttf_backend() { started_ = TTF_Init(); }
    ~ttf_backend() override {
        for (auto & [key, font] : sized_) {
            if (font != nullptr) { TTF_CloseFont(font); }
        }
        if (started_) { TTF_Quit(); }
    }

    [[nodiscard]] bool ok() const noexcept { return started_; }

    // Register one face. The BYTES ARE COPIED and kept: SDL3_ttf reads from the
    // stream for the lifetime of the font, so handing it a span of somebody
    // else's vector is a use-after-free waiting for the first page that gets
    // collected.
    bool add_face(std::string family, bool bold, bool italic, std::span<const std::byte> bytes);

    [[nodiscard]] std::size_t face_count() const;
    [[nodiscard]] bool has_face(std::string_view family, bool bold, bool italic) const;
    void set_default_family(std::string family);

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
        // The run's box top is the top of the LINE and glyphs sit on a baseline
        // inside it - the same convention font8x8 uses, so a page that mixes
        // backends does not step up and down.
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
        const std::lock_guard guard{mutex_};
        TTF_Font * font = sized_font(family, bold, italic, pixel_size(font_size));
        if (font == nullptr) { return font8x8_fonts().ascent(font_size, family, bold, italic); }
        return static_cast<float>(TTF_GetFontAscent(font));
    }

    [[nodiscard]] float descent(float font_size, std::string_view family, bool bold,
                                bool italic) const override {
        const std::lock_guard guard{mutex_};
        TTF_Font * font = sized_font(family, bold, italic, pixel_size(font_size));
        if (font == nullptr) { return font8x8_fonts().descent(font_size, family, bold, italic); }
        // SDL3_ttf's descent is NEGATIVE, and a descent is a distance.
        return -static_cast<float>(TTF_GetFontDescent(font));
    }

private:
    struct face_key {
        std::string family;
        bool bold = false;
        bool italic = false;
        [[nodiscard]] friend auto operator<=>(const face_key &, const face_key &) = default;
    };
    struct loaded_face {
        std::vector<std::byte> bytes; // SDL3_ttf reads these for the font's life
    };
    struct sized_key {
        face_key face;
        int size = 0;
        [[nodiscard]] friend auto operator<=>(const sized_key &, const sized_key &) = default;
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
        int left = 0; // from the pen, positive right
        int top = 0;  // from the baseline, positive up
        float advance = 0;
        std::vector<std::uint8_t> coverage; // width * height, 0..255
    };

    [[nodiscard]] static std::string lowered(std::string_view text);
    [[nodiscard]] static int pixel_size(float font_size) noexcept;

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

    [[nodiscard]] static TTF_Font * open_sized(const std::vector<std::byte> & bytes, int size);

    // An unknown family falls back to the DEFAULT one rather than drawing
    // nothing, and a missing bold/italic variant to the upright one - which is
    // what a browser does with a family it only has one weight of.
    [[nodiscard]] const loaded_face * resolve(std::string_view family, bool bold,
                                              bool italic) const {
        const std::string name = lowered(family);
        for (const face_key & candidate :
             {face_key{name, bold, italic}, face_key{name, false, false},
              face_key{default_family_, bold, italic}, face_key{default_family_, false, false}}) {
            if (candidate.family.empty()) { continue; }
            if (const auto it = faces_.find(candidate); it != faces_.end()) { return &it->second; }
        }
        return faces_.empty() ? nullptr : &faces_.begin()->second;
    }

    // One TTF_Font per (face, pixel size). Opening per size rather than calling
    // TTF_SetFontSize keeps the cached glyphs of one size valid while another
    // is being drawn - resizing a shared font would invalidate them underneath
    // whichever thread was using it.
    //
    // Caller holds the lock.
    [[nodiscard]] TTF_Font * sized_font(std::string_view family, bool bold, bool italic,
                                        int size) const {
        const loaded_face * face = resolve(family, bold, italic);
        if (face == nullptr) { return nullptr; }
        const sized_key key{face_key{lowered(family), bold, italic}, size};
        if (const auto it = sized_.find(key); it != sized_.end()) { return it->second; }
        TTF_Font * font = open_sized(face->bytes, size);
        sized_.emplace(key, font);
        return font;
    }

    // Rasterized once, then read. The lookup is under the lock; the BLIT is
    // not, which is safe because a glyph never changes after it is inserted and
    // std::map does not move its nodes.
    [[nodiscard]] const glyph * glyph_for(std::string_view family, bool bold, bool italic, int size,
                                          char32_t code) const {
        const std::lock_guard guard{mutex_};
        TTF_Font * font = sized_font(family, bold, italic, size);
        if (font == nullptr) { return nullptr; }
        const glyph_key key{face_key{lowered(family), bold, italic}, size, code};
        if (const auto it = glyphs_.find(key); it != glyphs_.end()) { return &it->second; }

        int minx = 0;
        int maxx = 0;
        int miny = 0;
        int maxy = 0;
        int advance = 0;
        if (!TTF_GetGlyphMetrics(font, code, &minx, &maxx, &miny, &maxy, &advance)) {
            return nullptr;
        }
        glyph made;
        made.left = minx;
        made.top = maxy;
        made.advance = static_cast<float>(advance);

        TTF_ImageType type = TTF_IMAGE_INVALID;
        SDL_Surface * image = TTF_GetGlyphImage(font, code, &type);
        if (image != nullptr) {
            // Whatever SDL3_ttf produced, read it as straight 8-bit COVERAGE:
            // the glyph is a mask that scales the text colour's alpha, and the
            // text colour is the page's, not the font's.
            SDL_Surface * alpha = SDL_ConvertSurface(image, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(image);
            if (alpha != nullptr) {
                made.width = alpha->w;
                made.height = alpha->h;
                made.coverage.resize(static_cast<std::size_t>(made.width) *
                                     static_cast<std::size_t>(made.height));
                for (int y = 0; y < made.height; ++y) {
                    const auto * row = reinterpret_cast<const std::uint8_t *>(
                        static_cast<const std::byte *>(alpha->pixels) +
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(alpha->pitch));
                    for (int x = 0; x < made.width; ++x) {
                        made.coverage[static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(made.width) +
                                      static_cast<std::size_t>(x)] =
                            row[static_cast<std::size_t>(x) * 4U + 3U]; // the alpha channel
                    }
                }
                SDL_DestroySurface(alpha);
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

    bool started_ = false;
    mutable std::mutex mutex_;
    std::map<face_key, loaded_face> faces_;
    mutable std::map<sized_key, TTF_Font *> sized_;
    mutable std::map<glyph_key, glyph> glyphs_;
    std::string default_family_;
};

#endif // CTBROWSER_WITH_TTF

} // namespace ctbrowser::raster
