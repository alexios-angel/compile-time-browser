//===- metrics.hpp - Font metrics for layout --------------------*- C++ -*-===//
//
// The one place layout's idea of a font and raster's meet.
//
// It lives in the shell because it has to name both, and neither may name the
// other: paint and raster sit downstream of layout, so layout including raster
// would invert the direction the pipeline runs in. The shell assembles the
// engine, so the shell converts; the browser owns the fonts for the same
// reason.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <string_view>

namespace ctbrowser::shell {

/// Layout metrics backed by \p fonts, the same object the rasterizer draws
/// with. Text lands where layout placed it only if one object answers both
/// "how wide is this run" and "where is its baseline".
///
/// The returned callbacks capture \p fonts by reference, so it must outlive
/// them.
[[nodiscard]] inline ctbrowser::layout::measure_text_fn metrics_for(
    const ctbrowser::raster::font_backend & fonts) {
    ctbrowser::layout::measure_text_fn out;
    out.measure = [&fonts](std::string_view text, float size,
                           const ctbrowser::layout::text_face & face) {
        return fonts.advance(text, size, face.family, face.bold, face.italic);
    };
    out.ascent_of = [&fonts](float size, const ctbrowser::layout::text_face & face) {
        return fonts.ascent(size, face.family, face.bold, face.italic);
    };
    out.descent_of = [&fonts](float size, const ctbrowser::layout::text_face & face) {
        return fonts.descent(size, face.family, face.bold, face.italic);
    };
    return out;
}

/// Metrics for the built-in 8x8 bitmap font, which is always available: for
/// tests and for a build with no font files.
[[nodiscard]] inline ctbrowser::layout::measure_text_fn font8x8_metrics() {
    return metrics_for(ctbrowser::raster::font8x8_fonts());
}

} // namespace ctbrowser::shell
