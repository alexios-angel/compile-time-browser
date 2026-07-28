#pragma once
#include <ctbrowser/raster/font8x8.hpp>

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
[[nodiscard]] int font8x8_scale(float font_size) noexcept;

[[nodiscard]] std::size_t utf8_length(std::string_view text) noexcept;

// The glyph bitmap itself, exported so the canvas can draw the SAME glyphs the
// page does. A canvas whose text does not match the text around it looks like
// two documents.
[[nodiscard]] bool glyph_pixel(char32_t code, int row, int column) noexcept;

[[nodiscard]] float font8x8_advance(std::string_view text, float font_size) noexcept;

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

void fill_rect(const rect & where, color c, const pixel_rect & clip, surface & into);

// A filled ellipse inscribed in `where`, ANTIALIASED at the edge. A hard-edged
// circle at 13 pixels across - the size of a radio button - looks like a
// polygon, and the coverage here is cheap: the distance from the centre in
// normalised ellipse space, one pixel wide at the boundary.
void fill_ellipse(const rect & where, color c, const pixel_rect & clip, surface & into);

// One horizontal band of `thickness` at `y`, for underline and line-through.
void fill_band(float x, float y, float width, float thickness, color c, const pixel_rect & clip,
               surface & into);

// font8x8: an 8x8 bitmap per code point, scaled by an integer factor. The
// run's box top is the TOP of the cell, matching how layout positions a line.
void draw_text(const rect & where, const paint_command & c, const pixel_rect & clip,
               surface & into);

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

[[nodiscard]] const font_backend & font8x8_fonts();

// The adapter that turns a backend into layout's text_metrics lives in the
// SHELL (shell::metrics_for): it needs to name a layout type, and raster must
// not import layout - the pipeline runs the other way.

// A run plus whatever line CSS asked to be drawn through or under it. The bands
// are the rasterizer's job rather than layout's because their thickness and
// position follow the FONT - a 1px rule under 40px text looks like a mistake.
void draw_text_run(const rect & where, const paint_command & c, const pixel_rect & clip,
                   surface & into, const font_backend & fonts);

// A bitmap into a tile. Nearest-neighbour: a canvas is laid out at its own
// pixel size, so the common case is 1:1 and any filtering would only blur it.
void draw_image(const rect & where, const paint_command & c, const pixel_rect & clip,
                surface & into);

void draw_commands(const std::vector<paint_command> & commands, const rect & area, surface & into,
                   const font_backend * fonts = nullptr);

// Rasterize whatever `list` draws inside `area` into `into`, which is that
// area's tile. The tile starts transparent: the page background is composited
// under it, not baked into it, so a tile stays valid when the background
// changes and when it is reused under a different layer.
void draw_into(surface & into, const display_list & list, const rect & area,
               const font_backend * fonts = nullptr);

} // namespace ctbrowser::raster
