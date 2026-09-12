#include <ctbrowser/raster/draw.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/raster/text/font8x8.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// draw: the fills, the font8x8 glyph walk and the command loop. Only
// draw_into, font8x8_fonts and font8x8_advance are exported; the rest has no
// caller outside this file.

namespace ctbrowser::raster {

namespace {

// font8x8 draws each glyph in an 8x8 cell scaled by an integer factor, so its
// advance is exact and has no font metrics behind it. Layout must measure with
// THIS if the raster is to place text where layout thought it would.
int font8x8_scale(float font_size) noexcept {
    const int s = static_cast<int>(font_size / 8.0f + 0.5f);
    return s < 1 ? 1 : s;
}

std::size_t utf8_length(std::string_view text) noexcept {
    std::size_t n = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) { ++n; }
    }
    return n;
}

} // namespace

float font8x8_advance(std::string_view text, float font_size) noexcept {
    return static_cast<float>(utf8_length(text) * 8u *
                              static_cast<std::size_t>(font8x8_scale(font_size)));
}

namespace {

void fill_rect(const rect & where, color c, const pixel_rect & clip, surface & into) {
    const pixel_rect p = intersect(to_pixels(where, into.width(), into.height()), clip);
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
void fill_ellipse(const rect & where, color c, const pixel_rect & clip, surface & into) {
    const pixel_rect p = intersect(to_pixels(where, into.width(), into.height()), clip);
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

// The signed distance from a point to a rounded rectangle, in PIXELS: negative
// inside, zero on the edge. The standard rounded-box field, with the corner
// radius chosen by which quadrant the point is in - which is what makes four
// different radii cost nothing over one.
[[nodiscard]] float round_rect_distance(float px, float py, const rect & box,
                                        const paint::corner_radii & radii) noexcept {
    const float half_w = box.width / 2;
    const float half_h = box.height / 2;
    const float cx = box.x + half_w;
    const float cy = box.y + half_h;
    const float r = px < cx ? (py < cy ? radii.top_left : radii.bottom_left)
                            : (py < cy ? radii.top_right : radii.bottom_right);
    // Distance from the box shrunk by r on every side; adding r back turns that
    // rectangle's field into the rounded one's.
    const float qx = std::fabs(px - cx) - (half_w - r);
    const float qy = std::fabs(py - cy) - (half_h - r);
    const float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) +
                                    std::max(qy, 0.0f) * std::max(qy, 0.0f));
    return outside + std::min(std::max(qx, qy), 0.0f) - r;
}

// One pixel of falloff centred on the boundary, which is what `0.5 - d` is.
[[nodiscard]] float coverage_of(float distance) noexcept {
    return std::clamp(0.5f - distance, 0.0f, 1.0f);
}

// A rounded rectangle, or - with `ring` above zero - the band that thick along
// its inside edge, which is what a rounded BORDER is.
//
// ANTIALIASED by the same means as the ellipse and for the same reason: a hard
// edge on a 4px corner reads as a staircase, and Bootstrap puts one on every
// button, badge, card and alert on the page. The coverage comes from a signed
// distance to the shape, in pixels, so one implementation serves every radius
// from a 2px input corner to a 20px pill.
//
// A RING IS A DIFFERENCE OF TWO COVERAGES rather than a stroked path: the outer
// shape less the shape inset by `ring`. That antialiases both edges of the band
// for free and needs no path machinery, and it is exact for the uniform borders
// this engine models. Per-side widths and dashes are not this function's job -
// see the note on emit_border.
void fill_round_rect(const rect & where, const paint::corner_radii & radii, float ring, color c,
                     const pixel_rect & clip, surface & into) {
    // AN ORDINARY RECTANGLE TAKES THE ORDINARY PATH. Every fill on a page with no
    // radius anywhere - which is every golden this repository already has - must
    // stay byte-identical, and a distance field evaluated per pixel would not be.
    if (radii.empty() && ring <= 0) {
        fill_rect(where, c, clip, into);
        return;
    }
    const pixel_rect p = intersect(to_pixels(where, into.width(), into.height()), clip);
    if (p.empty() || where.width <= 0 || where.height <= 0) { return; }

    const bool hollow = ring > 0;
    const rect inner{where.x + ring, where.y + ring, std::max(0.0f, where.width - 2 * ring),
                     std::max(0.0f, where.height - 2 * ring)};
    // A SQUARE RING THAT SWALLOWS ITS OWN HOLE IS A SOLID RECTANGLE, and drawing
    // it as one matters: the distance field feathers its edge by a pixel, so two
    // adjacent flooded cells left a light SEAM between them where they met.
    // Bootstrap floods every table cell with `inset 0 0 0 9999px`, so that seam
    // ran down every column boundary of every table.
    if (radii.empty() && (inner.width <= 0 || inner.height <= 0)) {
        fill_rect(where, c, clip, into);
        return;
    }
    const paint::corner_radii inner_radii = radii.inset(ring);
    for (int y = p.top; y < p.bottom; ++y) {
        const std::span<std::uint32_t> row = into.row(y);
        const float py = static_cast<float>(y) + 0.5f;
        for (int x = p.left; x < p.right; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            float coverage = coverage_of(round_rect_distance(px, py, where, radii));
            if (hollow && inner.width > 0 && inner.height > 0) {
                coverage -= coverage_of(round_rect_distance(px, py, inner, inner_radii));
            }
            if (coverage <= 0) { continue; }
            const color shade =
                color::rgba(c.red(), c.green(), c.blue(),
                            static_cast<std::uint8_t>(
                                static_cast<float>(c.alpha()) * std::min(coverage, 1.0f) + 0.5f));
            row[static_cast<std::size_t>(x)] = blend_over(row[static_cast<std::size_t>(x)], shade);
        }
    }
}

// One horizontal band of `thickness` at `y`, for underline and line-through.
void fill_band(float x, float y, float width, float thickness, color c, const pixel_rect & clip,
               surface & into) {
    fill_rect(rect{x, y, width, thickness < 1 ? 1.0f : thickness}, c, clip, into);
}

// How far row `gy` of an italic glyph leans right. Two pixels over the cell,
// top-heavy: one is not visibly slanted at 8x8 and three shears the glyph into
// its neighbour.
[[nodiscard]] constexpr int italic_shift(int gy) noexcept {
    return (7 - gy) / 3;
}

// Is this pixel of the OUTPUT inked, given the style?
//
// BOLD is the glyph OR'd with itself one pixel right - the way bitmap fonts
// have always been emboldened, and the only way here: there is no second set
// of bitmaps and no outline to thicken. ITALIC shears the rows.
//
// Both read from the same 8x8 source, so neither needs a table of its own.
[[nodiscard]] bool inked(char32_t cp, int gy, int gx, bool bold, bool italic) {
    const int sx = gx - (italic ? italic_shift(gy) : 0);
    const auto at = [&](int x) { return x >= 0 && x < 8 && font8x8_data::glyph_pixel(cp, gy, x); };
    return at(sx) || (bold && at(sx - 1));
}

// font8x8: an 8x8 bitmap per code point, scaled by an integer factor. The
// run's box top is the TOP of the cell, matching how layout positions a line.
void draw_text(const rect & where, const paint_command & c, const pixel_rect & clip,
               surface & into) {
    const int scale = font8x8_scale(c.font_size);
    const int origin_x = round_to_pixel(where.x);
    const int origin_y = round_to_pixel(where.y);
    const bool bold = c.face.bold;
    const bool italic = c.face.italic;
    // A styled glyph reaches past its 8-wide cell - one column for the bold
    // smear, two for the italic lean. It is allowed to: an italic that stops
    // dead at the cell edge is a clipped italic, and the OVERHANG is what makes
    // the slant read. The advance is unchanged, which is what keeps layout and
    // this function agreeing about where text goes.
    const int overhang = (bold ? 1 : 0) + (italic ? 2 : 0);
    int cell = 0;
    for (std::size_t i = 0; i < c.text.size();) {
        const char32_t cp = decode_utf8(c.text, i);
        const int left = origin_x + cell * 8 * scale;
        ++cell;
        if (cp > 0x7F) { continue; } // outside font8x8; the cell is still advanced
        for (int gy = 0; gy < 8; ++gy) {
            for (int gx = 0; gx < 8 + overhang; ++gx) {
                if (!inked(cp, gy, gx, bold, italic)) { continue; }
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

// font8x8 AS a backend: always present, needs no files, the same pixels on
// every machine.
class font8x8_backend final : public font_backend {
public:
    [[nodiscard]] float advance(std::string_view text, float font_size, std::string_view, bool,
                                bool) const override {
        // The SAME WIDTH whatever the style. font8x8 has one set of bitmaps and
        // synthesises bold and italic from it (see draw_text) - a smear and a
        // shear, both of which overhang the cell rather than widening it.
        //
        // Deliberately not wider for bold. Layout measures with this exact
        // function and the rasterizer draws with the other; a style that
        // advanced differently from the way it is drawn would put every caret
        // and every wrap in the wrong place, which is worse than a bold that
        // occupies the same cells as its regular. Monospace bitmap faces have
        // always done it this way.
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

// A run plus whatever line CSS asked to be drawn through or under it. The bands
// are the rasterizer's job rather than layout's because their thickness and
// position follow the FONT - a 1px rule under 40px text looks like a mistake.
void draw_text_run(const rect & where, const paint_command & c, const pixel_rect & clip,
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
void draw_image(const rect & where, const paint_command & c, const pixel_rect & clip,
                surface & into) {
    if (!c.pixels || c.pixels->empty() || where.width <= 0 || where.height <= 0) { return; }
    const pixel_rect p = intersect(to_pixels(where, into.width(), into.height()), clip);
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

void draw_commands(const std::vector<paint_command> & commands, const rect & area, surface & into,
                   const font_backend * fonts) {
    const font_backend & faces = fonts != nullptr ? *fonts : font8x8_fonts();
    // Clip stack in tile-local pixels. push_clip intersects, pop restores.
    std::vector<pixel_rect> clips;
    pixel_rect clip{0, 0, into.width(), into.height()};
    for (const paint_command & c : commands) {
        const rect local = c.bounds.translated(-area.x, -area.y);
        switch (c.op) {
        case paint_op::push_clip: {
            clips.push_back(clip);
            clip = intersect(clip, to_pixels(local, into.width(), into.height()));
            break;
        }
        case paint_op::pop_clip:
            if (!clips.empty()) {
                clip = clips.back();
                clips.pop_back();
            }
            break;
        case paint_op::fill_rect:
            // Through the rounded path always: it falls back to fill_rect for a
            // square, un-ringed fill, which is every fill this engine recorded
            // before border-radius existed - so no existing render moves.
            fill_round_rect(local, c.radii, c.ring, c.fill, clip, into);
            break;
        case paint_op::fill_ellipse: fill_ellipse(local, c.fill, clip, into); break;
        case paint_op::text_run: draw_text_run(local, c, clip, into, faces); break;
        case paint_op::image: draw_image(local, c, clip, into); break;
        }
    }
}

} // namespace

const font_backend & font8x8_fonts() {
    static const font8x8_backend backend;
    return backend;
}

void draw_into(surface & into, const display_list & list, const rect & area,
               const font_backend * fonts) {
    into.fill(color{0});
    draw_commands(list.intersecting(area), area, into, fonts);
}

} // namespace ctbrowser::raster
