//===- ctbrowser/lib/Raster/svg.cpp - SVG through plutosvg ------*- C++ -*-===//
//
// render_svg: rasterises an SVG document into a straight-alpha bitmap. The one
// file in the tree that includes a plutosvg header; everything the engine
// knows about SVG rasterisation is the declarations in svg.hpp.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/raster/svg.hpp>

#if CTBROWSER_WITH_SVG
#include <plutosvg.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#endif

namespace ctbrowser::raster {

#if CTBROWSER_WITH_SVG
namespace {

/// Owning handles for plutosvg's C objects, so the early exits in render_svg
/// cannot leak them.
using document_ptr = std::unique_ptr<plutosvg_document_t, decltype(&plutosvg_document_destroy)>;
using surface_ptr = std::unique_ptr<plutovg_surface_t, decltype(&plutovg_surface_destroy)>;

/// Converts one premultiplied ARGB pixel to straight ARGB.
///
/// plutovg stores premultiplied ARGB; paint::bitmap is straight ARGB, and
/// raster/draw.cpp's image path calls blend_over() on it as straight alpha.
/// Copying premultiplied pixels into a straight bitmap does not look broken: it
/// looks slightly dark wherever alpha is partial, which on an antialiased edge
/// is every edge pixel and nowhere else. unittests/unit/svg_basics.cpp asserts
/// that a half-transparent red stays r=255 rather than becoming r=128.
[[nodiscard]] std::uint32_t unpremultiply(std::uint32_t argb) noexcept {
    const std::uint32_t a = argb >> 24;
    if (a == 0) { return 0; }      // fully transparent: colour is meaningless
    if (a == 255) { return argb; } // opaque: premultiplied and straight agree
    const auto channel = [a](std::uint32_t c) {
        // +a/2 rounds to nearest rather than truncating, so a round trip
        // through premultiply/unpremultiply does not drift a level per pass.
        return std::min<std::uint32_t>(255, (c * 255 + a / 2) / a);
    };
    return (a << 24) | (channel((argb >> 16) & 0xFF) << 16) | (channel((argb >> 8) & 0xFF) << 8) |
           channel(argb & 0xFF);
}

} // namespace
#endif

paint::bitmap render_svg([[maybe_unused]] std::string_view source, [[maybe_unused]] int width,
                         [[maybe_unused]] int height) {
#if CTBROWSER_WITH_SVG
    if (width <= 0 || height <= 0 || source.empty()) { return {}; }
    // The same 2^26-pixel budget as shell::max_decode_pixels, which raster
    // cannot include: svg_store::pixels_for scales down to it, so only a
    // caller that skipped that gets nothing rather than a 400 TB surface.
    if (static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
        (std::uint64_t{1} << 26)) {
        return {};
    }

    // The container size, which is what resolves a `width="100%"` on the root.
    // Passing the size we are about to render at means a percentage-sized
    // document fills the box the layout engine gave it, rather than resolving
    // against nothing and collapsing.
    document_ptr document{
        plutosvg_document_load_from_data(source.data(), static_cast<int>(source.size()),
                                         static_cast<float>(width), static_cast<float>(height),
                                         nullptr, nullptr),
        &plutosvg_document_destroy};
    if (!document) { return {}; }

    // nullptr id: the whole document rather than one element by id.
    surface_ptr surface{plutosvg_document_render_to_surface(document.get(), nullptr, width, height,
                                                            nullptr, nullptr, nullptr),
                        &plutovg_surface_destroy};
    if (!surface) { return {}; }

    const int got_w = plutovg_surface_get_width(surface.get());
    const int got_h = plutovg_surface_get_height(surface.get());
    const int stride = plutovg_surface_get_stride(surface.get());
    const unsigned char * data = plutovg_surface_get_data(surface.get());
    if (got_w <= 0 || got_h <= 0 || data == nullptr) { return {}; }

    // Trust the surface's own dimensions over the ones asked for. They agree in
    // every case seen so far, but a bitmap whose header disagrees with its
    // pixels is a buffer overrun rather than a wrong picture.
    paint::bitmap out{got_w, got_h};
    for (int y = 0; y < got_h; ++y) {
        // Use the stride, not width * 4: plutovg is free to pad rows, and assuming it
        // does not is a bug that only appears at certain widths.
        const auto * row =
            reinterpret_cast<const std::uint32_t *>(data + static_cast<std::ptrdiff_t>(y) * stride);
        for (int x = 0; x < got_w; ++x) { out.put(x, y, unpremultiply(row[x])); }
    }
    return out;
#else
    return {};
#endif
}

} // namespace ctbrowser::raster
