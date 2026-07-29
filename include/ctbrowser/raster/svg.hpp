#pragma once
#include <string_view>

#include <ctbrowser/paint/paint.hpp>

// SVG, through plutosvg.
//
// NOTE WHAT IS NOT INCLUDED ABOVE. Unlike raster/ttf.hpp - which puts
// <SDL_ttf.h> in a public header and pays for it with an entry in
// tests/api_surface's allow-list - plutosvg is confined entirely to
// src/raster/svg.cpp. Two declarations is the whole surface, so no consumer of
// this header parses a third-party one and no allow-list entry is needed.
// core/cpu_time.hpp is the same pattern for the same reason.
//
// RENDERS AT THE SIZE ASKED FOR, which is the point of the thing. paint's
// draw_image scales nearest-neighbour (paint/command.hpp), so a vector graphic
// decoded once at its natural size and then blown up looks WORSE than a PNG -
// stair-stepped along every diagonal. The caller passes the size the box
// actually got and gets pixels for that size; shell/svg.hpp is where the
// resulting bitmaps are cached so this is not paid per frame.
//
// OPTIONAL, like SDL3_ttf and everything else here: without plutosvg this still
// compiles, `svg_available()` is false, `render_svg` returns an empty bitmap,
// and display_list::draw_image already ignores those. A page lays out
// identically either way - the natural-size scan in shell/svg.hpp is in-engine
// and does not go through plutosvg - it simply draws nothing where the graphic
// would be.

namespace ctbrowser::raster {

// Whether this build has plutosvg. A build-time fact, so a caller can branch on
// it in a constant expression - and tests can skip their pixel assertions on a
// machine (CI, for one) that has no plutosvg at all.
[[nodiscard]] constexpr bool svg_available() noexcept {
#if CTBROWSER_WITH_SVG
    return true;
#else
    return false;
#endif
}

// `source` is SVG markup - the ORIGINAL bytes, not a re-serialised DOM, which
// is what keeps `viewBox` and `linearGradient` spelled the way the author wrote
// them regardless of what the HTML tokenizer did to the tree's copy.
//
// Returns an empty bitmap on any failure: no plutosvg, malformed markup, a
// non-positive size. Callers do not need to distinguish those - the display
// list drops an empty bitmap on the floor - and a page with a broken graphic
// should render the rest of itself rather than refuse.
[[nodiscard]] paint::bitmap render_svg(std::string_view source, int width, int height);

} // namespace ctbrowser::raster
