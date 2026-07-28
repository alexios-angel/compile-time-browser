module;
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <ctbrowser/core/core.hpp>

export module ctbrowser.raster:surface;

// A block of 0xAARRGGBB pixels, addressed as span + explicit stride.
//
// span + stride rather than a 2D view because libstdc++ 13 has no std::mdspan
// and Boost has no replacement. It is not a hardship: a tile is written by
// exactly one thread, row by row, and the row span is the shape that raster
// loops actually want.

export namespace ctbrowser::raster {

using ctbrowser::color;
using ctbrowser::rect;

class surface {
public:
    surface() = default;
    surface(int width, int height)
        : width_(width < 0 ? 0 : width), height_(height < 0 ? 0 : height),
          pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0) {}

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::size_t stride() const noexcept { return static_cast<std::size_t>(width_); }
    [[nodiscard]] bool empty() const noexcept { return pixels_.empty(); }

    [[nodiscard]] std::span<std::uint32_t> row(int y) noexcept {
        return std::span<std::uint32_t>{pixels_}.subspan(static_cast<std::size_t>(y) * stride(),
                                                         stride());
    }
    [[nodiscard]] std::span<const std::uint32_t> row(int y) const noexcept {
        return std::span<const std::uint32_t>{pixels_}.subspan(
            static_cast<std::size_t>(y) * stride(), stride());
    }
    [[nodiscard]] std::span<const std::uint32_t> pixels() const noexcept { return pixels_; }

    void fill(color c) noexcept {
        for (std::uint32_t & p : pixels_) { p = c.argb; }
    }

    [[nodiscard]] friend bool operator==(const surface & a, const surface & b) noexcept {
        return a.width_ == b.width_ && a.height_ == b.height_ && a.pixels_ == b.pixels_;
    }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint32_t> pixels_;
};

// Source-over, the only blend mode that exists so far. Written out rather than
// pulled from a table because it is the innermost loop of every fill.
[[nodiscard]] inline std::uint32_t blend_over(std::uint32_t dst, color src) noexcept {
    const std::uint32_t a = src.alpha();
    if (a == 255) { return src.argb; }
    if (a == 0) { return dst; }
    const std::uint32_t inv = 255 - a;
    const auto channel = [&](unsigned shift) {
        const std::uint32_t s = (src.argb >> shift) & 0xFFu;
        const std::uint32_t d = (dst >> shift) & 0xFFu;
        return ((s * a + d * inv + 127) / 255) << shift;
    };
    const std::uint32_t da = (dst >> 24) & 0xFFu;
    const std::uint32_t out_a = a + da * inv / 255;
    return (out_a << 24) | channel(16) | channel(8) | channel(0);
}

// Pixel bounds of a rect, clipped to a surface. Rounding happens HERE and
// nowhere else: layout is float all the way down, and the single place that
// turns fractions into pixels is what keeps two backends agreeing.
struct pixel_rect {
    int left = 0, top = 0, right = 0, bottom = 0;
    [[nodiscard]] bool empty() const noexcept { return right <= left || bottom <= top; }
};

// THE rounding. Every float-to-pixel conversion in the rasterizer goes through
// here, and it is not a style preference: static_cast<int>(f + 0.5f) truncates
// toward zero, so it rounds -27.5 to -27 instead of -28. Negative coordinates
// are not an edge case here - they are what a tile sees for content starting to
// its left, and what a blit sees for a scrolled layer - so an inconsistent
// rounding shows up as tile seams and as a scroll that lands one pixel short.
[[nodiscard]] constexpr int round_to_pixel(float f) noexcept {
    return static_cast<int>(f < 0 ? f - 0.5f : f + 0.5f);
}

[[nodiscard]] inline pixel_rect to_pixels(const rect & r, int clip_width,
                                          int clip_height) noexcept {
    pixel_rect out;
    out.left = round_to_pixel(r.x);
    out.top = round_to_pixel(r.y);
    out.right = round_to_pixel(r.right());
    out.bottom = round_to_pixel(r.bottom());
    if (out.left < 0) { out.left = 0; }
    if (out.top < 0) { out.top = 0; }
    if (out.right > clip_width) { out.right = clip_width; }
    if (out.bottom > clip_height) { out.bottom = clip_height; }
    return out;
}

} // namespace ctbrowser::raster
