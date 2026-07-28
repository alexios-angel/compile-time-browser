#pragma once
#include <algorithm>
#include <compare>
#include <cstdint>

// Layout geometry. Deliberately float, not the int32 pixels the previous engine used: a
// fractional box model is required for zoom, device pixel ratios and
// transforms, and rounding only at raster time is what keeps sub-pixel text
// positioning possible.

namespace ctbrowser {

struct point {
    float x = 0, y = 0;
    [[nodiscard]] friend constexpr bool operator==(point, point) = default;
};

struct size {
    float width = 0, height = 0;
    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] friend constexpr bool operator==(size, size) = default;
};

struct rect {
    float x = 0, y = 0, width = 0, height = 0;

    [[nodiscard]] constexpr float left() const noexcept { return x; }
    [[nodiscard]] constexpr float top() const noexcept { return y; }
    [[nodiscard]] constexpr float right() const noexcept { return x + width; }
    [[nodiscard]] constexpr float bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }

    [[nodiscard]] constexpr bool contains(point p) const noexcept {
        return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
    }
    [[nodiscard]] constexpr bool intersects(const rect & o) const noexcept {
        return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
    }
    [[nodiscard]] constexpr rect intersected(const rect & o) const noexcept {
        const float l = std::max(x, o.x), t = std::max(y, o.y);
        const float r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
        return r > l && b > t ? rect{l, t, r - l, b - t} : rect{};
    }
    [[nodiscard]] constexpr rect united(const rect & o) const noexcept {
        if (empty()) { return o; }
        if (o.empty()) { return *this; }
        const float l = std::min(x, o.x), t = std::min(y, o.y);
        const float r = std::max(right(), o.right()), b = std::max(bottom(), o.bottom());
        return rect{l, t, r - l, b - t};
    }
    [[nodiscard]] constexpr rect translated(float dx, float dy) const noexcept {
        return rect{x + dx, y + dy, width, height};
    }
    [[nodiscard]] friend constexpr bool operator==(const rect &, const rect &) = default;
};

// CSS box sides, in the order the shorthand expands
struct sides {
    float top = 0, right = 0, bottom = 0, left = 0;
    [[nodiscard]] constexpr float horizontal() const noexcept { return left + right; }
    [[nodiscard]] constexpr float vertical() const noexcept { return top + bottom; }
    [[nodiscard]] friend constexpr bool operator==(const sides &, const sides &) = default;
};

// 0xAARRGGBB, the packing the canvas and the compositor already share
struct color {
    std::uint32_t argb = 0;

    [[nodiscard]] static constexpr color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                              std::uint8_t a = 255) noexcept {
        return color{(static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
                     (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b)};
    }
    [[nodiscard]] constexpr std::uint8_t alpha() const noexcept {
        return static_cast<std::uint8_t>((argb >> 24) & 0xFFu);
    }
    [[nodiscard]] constexpr std::uint8_t red() const noexcept {
        return static_cast<std::uint8_t>((argb >> 16) & 0xFFu);
    }
    [[nodiscard]] constexpr std::uint8_t green() const noexcept {
        return static_cast<std::uint8_t>((argb >> 8) & 0xFFu);
    }
    [[nodiscard]] constexpr std::uint8_t blue() const noexcept {
        return static_cast<std::uint8_t>(argb & 0xFFu);
    }
    [[nodiscard]] constexpr bool opaque() const noexcept { return alpha() == 255; }
    [[nodiscard]] constexpr bool transparent() const noexcept { return alpha() == 0; }
    [[nodiscard]] friend constexpr bool operator==(color, color) = default;
};

} // namespace ctbrowser
