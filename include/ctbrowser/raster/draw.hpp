#pragma once
#include "font8x8.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>

#include <ctbrowser/raster/surface.hpp>

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

namespace ctbrowser::raster {

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

// The glyph bitmap itself, exported so the canvas can draw the SAME glyphs the
// page does. A canvas whose text does not match the text around it looks like
// two documents.
[[nodiscard]] inline bool glyph_pixel(char32_t code, int row, int column) noexcept {
    return font8x8_data::glyph_pixel(code, row, column);
}

[[nodiscard]] inline float font8x8_advance(std::string_view text, float font_size) noexcept {
    return static_cast<float>(utf8_length(text) * 8u *
                              static_cast<std::size_t>(font8x8_scale(font_size)));
}

// THE SEAM a real font plugs into.
//
// Everything above this line is font8x8: an 8x8 bitmap per code point scaled by
// an INTEGER factor, which is why font8x8 quantises font sizes to multiples of 8 -
// 16px and 20px text render identically, and that is a property of the font,
// not a rounding bug. A backend with outlines removes it.
//
// Two operations, because they are the two that have to agree: layout measures
// with `advance` and the rasterizer draws with `draw_run`, and text lands where
// layout thought it would only if the same object answers both.
class font_backend {
public:
    virtual ~font_backend() = default;
    font_backend() = default;
    font_backend(const font_backend &) = delete;
    font_backend & operator=(const font_backend &) = delete;

    // The face is passed APART rather than as a paint::font_face, because
    // measuring is the hot path - a text wrap measures the same run repeatedly -
    // and building a face would allocate its family string on every call.
    [[nodiscard]] virtual float advance(std::string_view text, float font_size,
                                        std::string_view family, bool bold, bool italic) const = 0;
    // `where` is TILE-LOCAL; `clip` is in the same space.
    virtual void draw_run(const rect & where, const paint_command & c, const pixel_rect & clip,
                          surface & into) const = 0;
    // The distance from the top of the line box to the baseline, which a
    // decoration band and a canvas fillText both need - and which LAYOUT needs
    // to put two different sizes on one baseline.
    [[nodiscard]] virtual float ascent(float font_size, std::string_view family, bool bold,
                                       bool italic) const = 0;
    // How far the face descends BELOW the baseline. Positive.
    [[nodiscard]] virtual float descent(float font_size, std::string_view family, bool bold,
                                        bool italic) const = 0;
};

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

// A filled ellipse inscribed in `where`, ANTIALIASED at the edge. A hard-edged
// circle at 13 pixels across - the size of a radio button - looks like a
// polygon, and the coverage here is cheap: the distance from the centre in
// normalised ellipse space, one pixel wide at the boundary.
inline void fill_ellipse(const rect & where, color c, const pixel_rect & clip, surface & into) {
    pixel_rect p = to_pixels(where, into.width(), into.height());
    p.left = std::max(p.left, clip.left);
    p.top = std::max(p.top, clip.top);
    p.right = std::min(p.right, clip.right);
    p.bottom = std::min(p.bottom, clip.bottom);
    if (p.empty() || where.width <= 0 || where.height <= 0) { return; }

    const float cx = where.x + where.width / 2;
    const float cy = where.y + where.height / 2;
    const float rx = where.width / 2;
    const float ry = where.height / 2;
    // One pixel of falloff, expressed in the same normalised units the test is
    // in - so a small circle and a large one both get a one-pixel edge.
    const float feather = std::max(1.0f / std::max(rx, ry), 0.001f);
    for (int y = p.top; y < p.bottom; ++y) {
        const std::span<std::uint32_t> row = into.row(y);
        const float dy = (static_cast<float>(y) + 0.5f - cy) / ry;
        for (int x = p.left; x < p.right; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f - cx) / rx;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float coverage = std::clamp((1.0f - distance) / feather, 0.0f, 1.0f);
            if (coverage <= 0) { continue; }
            const color shade = color::rgba(
                c.red(), c.green(), c.blue(),
                static_cast<std::uint8_t>(static_cast<float>(c.alpha()) * coverage + 0.5f));
            row[static_cast<std::size_t>(x)] = blend_over(row[static_cast<std::size_t>(x)], shade);
        }
    }
}

// One horizontal band of `thickness` at `y`, for underline and line-through.
inline void fill_band(float x, float y, float width, float thickness, color c,
                      const pixel_rect & clip, surface & into) {
    fill_rect(rect{x, y, width, thickness < 1 ? 1.0f : thickness}, c, clip, into);
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

// font8x8 AS a backend. Always present, needs no files, and produces the same
// pixels on every machine - which is what makes the goldens goldens.
class font8x8_backend final : public font_backend {
public:
    [[nodiscard]] float advance(std::string_view text, float font_size, std::string_view, bool,
                                bool) const override {
        // font8x8 has ONE face. Bold and italic are not synthesised: a fake
        // that is wrong by a pixel is worse here than an honest sameness,
        // because layout measured with this exact function.
        return font8x8_advance(text, font_size);
    }
    void draw_run(const rect & where, const paint_command & c, const pixel_rect & clip,
                  surface & into) const override {
        draw_text(where, c, clip, into);
    }
    [[nodiscard]] float ascent(float font_size, std::string_view, bool, bool) const override {
        // The cell is 8 tall and the glyphs sit on its last row.
        return static_cast<float>(8 * font8x8_scale(font_size));
    }
    [[nodiscard]] float descent(float, std::string_view, bool, bool) const override {
        return 0; // nothing in font8x8 goes below the cell
    }
};

[[nodiscard]] inline const font_backend & font8x8_fonts() {
    static const font8x8_backend backend;
    return backend;
}

// The adapter that turns a backend into layout's text_metrics lives in the
// SHELL (shell::metrics_for): it needs to name a layout type, and raster must
// not import layout - the pipeline runs the other way.

// A run plus whatever line CSS asked to be drawn through or under it. The bands
// are the rasterizer's job rather than layout's because their thickness and
// position follow the FONT - a 1px rule under 40px text looks like a mistake.
inline void draw_text_run(const rect & where, const paint_command & c, const pixel_rect & clip,
                          surface & into, const font_backend & fonts) {
    fonts.draw_run(where, c, clip, into);
    if (c.decoration == ctbrowser::paint::text_decoration::none) { return; }
    const float thickness = std::max(1.0f, c.font_size / 14.0f);
    const float baseline =
        where.y + fonts.ascent(c.font_size, c.face.family, c.face.bold, c.face.italic);
    const float y = c.decoration == ctbrowser::paint::text_decoration::underline
                        ? baseline + thickness
                        : where.y + (baseline - where.y) * 0.62f;
    fill_band(where.x, y, where.width, thickness, c.fill, clip, into);
}

// A bitmap into a tile. Nearest-neighbour: a canvas is laid out at its own
// pixel size, so the common case is 1:1 and any filtering would only blur it.
inline void draw_image(const rect & where, const paint_command & c, const pixel_rect & clip,
                       surface & into) {
    if (!c.pixels || c.pixels->empty() || where.width <= 0 || where.height <= 0) { return; }
    pixel_rect p = to_pixels(where, into.width(), into.height());
    p.left = std::max(p.left, clip.left);
    p.top = std::max(p.top, clip.top);
    p.right = std::min(p.right, clip.right);
    p.bottom = std::min(p.bottom, clip.bottom);
    if (p.empty()) { return; }

    const float scale_x = static_cast<float>(c.pixels->width) / where.width;
    const float scale_y = static_cast<float>(c.pixels->height) / where.height;
    for (int y = p.top; y < p.bottom; ++y) {
        const std::span<std::uint32_t> row = into.row(y);
        const int source_y = static_cast<int>((static_cast<float>(y) + 0.5f - where.y) * scale_y);
        for (int x = p.left; x < p.right; ++x) {
            const int source_x =
                static_cast<int>((static_cast<float>(x) + 0.5f - where.x) * scale_x);
            const std::uint32_t texel = c.pixels->at(source_x, source_y);
            if ((texel >> 24) == 0) { continue; }
            row[static_cast<std::size_t>(x)] =
                blend_over(row[static_cast<std::size_t>(x)], color{texel});
        }
    }
}

inline void draw_commands(const std::vector<paint_command> & commands, const rect & area,
                          surface & into, const font_backend * fonts = nullptr) {
    const font_backend & faces = fonts != nullptr ? *fonts : font8x8_fonts();
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
        case paint_op::fill_ellipse: fill_ellipse(local, c.fill, clip, into); break;
        case paint_op::text_run: draw_text_run(local, c, clip, into, faces); break;
        case paint_op::image: draw_image(local, c, clip, into); break;
        }
    }
}

// Rasterize whatever `list` draws inside `area` into `into`, which is that
// area's tile. The tile starts transparent: the page background is composited
// under it, not baked into it, so a tile stays valid when the background
// changes and when it is reused under a different layer.
inline void draw_into(surface & into, const display_list & list, const rect & area,
                      const font_backend * fonts = nullptr) {
    into.fill(color{0});
    draw_commands(list.intersecting(area), area, into, fonts);
}

} // namespace ctbrowser::raster
