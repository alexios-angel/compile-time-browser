//===- draw.hpp - Display list to pixels ---------------------- -*- C++ -*-===//
//
// Turning display-list commands into pixels: draw_into rasterizes one tile,
// and the individual fills live in draw.cpp. The default text is font8x8
// rather than a real font stack, because a golden that depends on which fonts
// a machine happens to have is not a golden.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <string_view>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>

#include <ctbrowser/raster/surface.hpp>

namespace ctbrowser::raster {

using ctbrowser::color;
using ctbrowser::paint::display_list;
using ctbrowser::paint::paint_command;
using ctbrowser::paint::paint_op;

/// The width of `text` at `font_size` in the built-in font8x8 face.
[[nodiscard]] float font8x8_advance(std::string_view text, float font_size) noexcept;

/// The interface a real font plugs into.
///
/// The default behind it is font8x8: an 8x8 bitmap per code point scaled by an
/// integer factor, so font8x8 quantises font sizes to multiples of 8. 16px and
/// 20px text render identically; that is a property of the font, not a
/// rounding bug, and a backend with outlines removes it. font8x8_advance is
/// that fallback's measure, exposed so the widget tests can size against it.
///
/// Measuring and drawing are on one object because they have to agree: layout
/// measures with `advance` and the rasterizer draws with `draw_run`, and text
/// lands where layout thought it would only if the same object answers both.
class font_backend {
public:
    virtual ~font_backend() = default;
    font_backend() = default;
    font_backend(const font_backend &) = delete;
    font_backend & operator=(const font_backend &) = delete;

    /// The advance width of `text`. The face is passed as separate arguments
    /// rather than as a paint::font_face because measuring is the hot path (a
    /// text wrap measures the same run repeatedly), and building a face would
    /// allocate its family string on every call.
    [[nodiscard]] virtual float advance(std::string_view text, float font_size,
                                        std::string_view family, bool bold, bool italic) const = 0;
    /// Draws the text run `c` into `into`. `where` is tile-local; `clip` is in
    /// the same space.
    virtual void draw_run(const rect & where, const paint_command & c, const pixel_rect & clip,
                          surface & into) const = 0;
    /// The distance from the top of the line box to the baseline. A decoration
    /// band and a canvas fillText both need it, and layout needs it to put two
    /// different sizes on one baseline.
    [[nodiscard]] virtual float ascent(float font_size, std::string_view family, bool bold,
                                       bool italic) const = 0;
    /// How far the face descends below the baseline. Positive.
    [[nodiscard]] virtual float descent(float font_size, std::string_view family, bool bold,
                                        bool italic) const = 0;
};

/// font8x8 as a backend. Always present, needs no files, and produces the same
/// pixels on every machine, which is what keeps goldens reproducible.
[[nodiscard]] const font_backend & font8x8_fonts();

// The adapter that turns a backend into layout's text_metrics lives in the
// shell (shell::metrics_for): it needs to name a layout type, and raster must
// not depend on layout, since the pipeline runs the other way.

/// Rasterizes whatever `list` draws inside `area` into `into`, which is that
/// area's tile. The tile starts transparent: the page background is composited
/// under it, not baked into it, so a tile stays valid when the background
/// changes and when it is reused under a different layer.
void draw_into(surface & into, const display_list & list, const rect & area,
               const font_backend * fonts = nullptr);

} // namespace ctbrowser::raster
