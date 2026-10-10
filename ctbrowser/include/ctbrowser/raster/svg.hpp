//===- ctbrowser/include/ctbrowser/raster/svg.hpp - SVG raster --*- C++ -*-===//
//
// Rasterises SVG markup through plutosvg.
//
// plutosvg is included only by lib/Raster/svg.cpp, so this header pulls in no
// third-party headers. raster/text/ttf.hpp follows the same pattern.
//
// render_svg() draws at the requested size because display_list::draw_image
// (paint/command.hpp) scales nearest-neighbour: decoding at the natural size
// and scaling up would leave jagged diagonals. shell/page/svg_cache.hpp caches
// the bitmaps so the cost is not paid every frame.
//
// plutosvg is optional. Without it svg_available() is false and render_svg()
// returns an empty bitmap, which draw_image ignores. Layout is unchanged,
// because svg_cache.hpp reads the natural size without plutosvg; the graphic
// simply draws nothing.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <string_view>

#include <ctbrowser/paint/paint.hpp>

namespace ctbrowser::raster {

/// Whether this build has plutosvg. A build-time fact, so a caller can branch
/// on it in a constant expression, and tests can skip their pixel assertions on
/// a machine without plutosvg.
[[nodiscard]] constexpr bool svg_available() noexcept {
#if CTBROWSER_WITH_SVG
    return true;
#else
    return false;
#endif
}

/// Rasterises `source` at `width` x `height`. `source` is the author's
/// original SVG bytes, not a re-serialised DOM, so names such as `viewBox` keep
/// the spelling the author wrote.
///
/// Returns an empty bitmap on any failure (no plutosvg, malformed markup, a
/// non-positive size). Callers need not tell these apart: the display list
/// drops an empty bitmap, and the rest of the page still renders.
[[nodiscard]] paint::bitmap render_svg(std::string_view source, int width, int height);

} // namespace ctbrowser::raster
