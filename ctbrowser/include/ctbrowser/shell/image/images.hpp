#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/paint/paint.hpp>

#include <ctbrowser/shell/page/assets.hpp>

// Decoding images into the bitmap the display list already carries.
//
// BMP is built in - uncompressed 24/32bpp, which every image tool can write and
// which needs no library at all. PNG goes through libpng (`image/png.cpp`) and
// JPEG through libjpeg-turbo (`image/jpeg.cpp`), both part of the SDL-free
// engine: a format whose result depended on whether SDL was found is one no
// golden can compare. Everything else - GIF, WEBP, TIFF - arrives through
// `decoder`, a hook the application layer fills in from SDL3_image when it was
// found.
//
// NOTHING THIRD-PARTY IS INCLUDED ABOVE, the rule url.hpp states for Boost.URL
// and net.hpp for curl.h: png.cpp is the only translation unit that has heard
// of libpng, and jpeg.cpp of libjpeg-turbo.

namespace ctbrowser::shell {

// An empty bitmap for anything that is not a PNG this can read - truncated,
// corrupt, or not a PNG at all. Every colour type and bit depth the format has
// arrives as the engine's 8-bit ARGB, interlaced images included, because the
// alternative is a decoder that is right about most PNGs.
[[nodiscard]] paint::bitmap decode_png(std::span<const std::byte> data);

// RGBA, 8 bits per channel: the encoding a decoder needs no options for. Empty
// for an empty bitmap. What `canvas.toDataURL()` and `toBlob()` hand back.
[[nodiscard]] std::vector<std::byte> encode_png(const paint::bitmap & image);

// An empty bitmap for anything that is not a JPEG this can read. Baseline and
// progressive, greyscale and colour, and every subsampling mode arrive as the
// engine's 8-bit ARGB with alpha fully opaque: JPEG has no transparency, and
// leaving the alpha byte to chance is how an image decodes and then draws as
// nothing.
[[nodiscard]] paint::bitmap decode_jpeg(std::span<const std::byte> data);

// The PNG signature and the JPEG SOI marker, checked before decoding is
// attempted. Cheap enough to ask of every load, which is what lets
// `image_store` try formats in order without a decode attempt per format.
[[nodiscard]] bool looks_like_png(std::span<const std::byte> data) noexcept;
[[nodiscard]] bool looks_like_jpeg(std::span<const std::byte> data) noexcept;

// Decode an uncompressed 24- or 32-bit BMP. An empty bitmap on any problem -
// truncated, or a flavour this does not read.
[[nodiscard]] inline paint::bitmap decode_bmp(std::span<const std::byte> data) {
    const auto byte_at = [&](std::size_t i) { return static_cast<std::uint32_t>(data[i]); };
    const auto u32 = [&](std::size_t i) {
        return byte_at(i) | (byte_at(i + 1) << 8) | (byte_at(i + 2) << 16) | (byte_at(i + 3) << 24);
    };
    const auto u16 = [&](std::size_t i) { return byte_at(i) | (byte_at(i + 1) << 8); };

    if (data.size() < 54 || byte_at(0) != 'B' || byte_at(1) != 'M') { return {}; }
    const std::uint32_t pixel_offset = u32(10);
    if (u32(14) < 40) { return {}; } // the header is older than BITMAPINFOHEADER
    const auto width = static_cast<std::int32_t>(u32(18));
    const auto raw_height = static_cast<std::int32_t>(u32(22));
    const std::uint32_t bits_per_pixel = u16(28);
    const std::uint32_t compression = u32(30);
    if (width <= 0 || raw_height == 0 || (bits_per_pixel != 24 && bits_per_pixel != 32) ||
        (compression != 0 && compression != 3)) {
        return {};
    }

    // A NEGATIVE height means the rows are stored top-down; the usual positive
    // one means bottom-up, which is why this reads them in reverse.
    const bool top_down = raw_height < 0;
    const std::int32_t height = top_down ? -raw_height : raw_height;
    const std::size_t bytes_per_pixel = bits_per_pixel / 8U;
    const std::size_t stride =
        (static_cast<std::size_t>(width) * bytes_per_pixel + 3U) & ~std::size_t{3};
    if (data.size() < pixel_offset + stride * static_cast<std::size_t>(height)) { return {}; }

    paint::bitmap out{width, height};
    for (std::int32_t y = 0; y < height; ++y) {
        const std::int32_t source_row = top_down ? y : height - 1 - y;
        const std::size_t line = pixel_offset + stride * static_cast<std::size_t>(source_row);
        for (std::int32_t x = 0; x < width; ++x) {
            const std::size_t at = line + static_cast<std::size_t>(x) * bytes_per_pixel;
            const std::uint32_t blue = byte_at(at);
            const std::uint32_t green = byte_at(at + 1);
            const std::uint32_t red = byte_at(at + 2);
            const std::uint32_t alpha = bytes_per_pixel == 4 ? byte_at(at + 3) : 0xFFU;
            out.put(x, y, (alpha << 24) | (red << 16) | (green << 8) | blue);
        }
    }
    return out;
}

// What a script's image handle refers to, and what an <img> element resolves
// to. Bitmaps are shared_ptr because the display list holds them too - a
// re-record must not copy every sprite in the page.
class image_store {
public:
    // Formats past BMP. The application layer installs SDL3_image here when the
    // build found it; without one, a PNG simply fails to load and the page sees
    // a zero-sized image rather than a crash.
    using decode_fn = std::function<paint::bitmap(std::span<const std::byte>, std::string_view)>;

    void set_decoder(decode_fn decoder) { decoder_ = std::move(decoder); }

    // Loads at most once per name: two <img src="x"> and a script loadImage("x")
    // share one decode and one bitmap.
    [[nodiscard]] std::shared_ptr<const paint::bitmap> load(const asset_registry & assets,
                                                            std::string_view name) {
        return cache_[slot_for(assets, name)].second;
    }

    // The script-facing handle: a stable index, because a script holds numbers
    // and a vector of shared_ptr moves its elements. -1 for a load that failed.
    [[nodiscard]] int handle_for(const asset_registry & assets, std::string_view name) {
        const std::size_t slot = slot_for(assets, name);
        return cache_[slot].second ? static_cast<int>(slot) : -1;
    }
    [[nodiscard]] std::shared_ptr<const paint::bitmap> at(int handle) const {
        if (handle < 0 || static_cast<std::size_t>(handle) >= cache_.size()) { return nullptr; }
        return cache_[static_cast<std::size_t>(handle)].second;
    }

private:
    // The cache only ever appends, so an entry's index is its handle.
    [[nodiscard]] std::size_t slot_for(const asset_registry & assets, std::string_view name) {
        for (std::size_t i = 0; i < cache_.size(); ++i) {
            if (cache_[i].first == name) { return i; }
        }
        const std::vector<std::byte> bytes = assets.load(name);
        std::shared_ptr<const paint::bitmap> image;
        if (!bytes.empty()) {
            paint::bitmap decoded = decode_bmp(bytes);
            // PNG BEFORE THE HOOK, deliberately: it means a headless test and an
            // application with SDL3_image decode the same file with the same
            // library and get the same pixels. A format whose result depended on
            // whether SDL was found is one a golden cannot compare.
            if (decoded.empty() && looks_like_png(bytes)) { decoded = decode_png(bytes); }
            if (decoded.empty() && looks_like_jpeg(bytes)) { decoded = decode_jpeg(bytes); }
            if (decoded.empty() && decoder_) { decoded = decoder_(bytes, name); }
            if (!decoded.empty()) {
                image = std::make_shared<const paint::bitmap>(std::move(decoded));
            }
        }
        // A FAILED load is cached too, as a null. Otherwise a page with a
        // missing sprite re-reads the filesystem every frame.
        cache_.emplace_back(std::string{name}, std::move(image));
        return cache_.size() - 1;
    }

    std::vector<std::pair<std::string, std::shared_ptr<const paint::bitmap>>> cache_;
    decode_fn decoder_;
};

} // namespace ctbrowser::shell
