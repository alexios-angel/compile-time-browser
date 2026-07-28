#pragma once
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

#include <ctbrowser/shell/assets.hpp>

// Decoding images into the bitmap the display list already carries.
//
// BMP is built in - uncompressed 24/32bpp, which every image tool can write and
// which needs no library at all. Everything else arrives through `decoder`, a
// hook the application layer fills in from SDL3_image when it was found. That
// split is deliberate: this module is part of the SDL-FREE engine, so sprite
// loading and drawing are testable headless, and a build without SDL3_image
// still shows BMPs rather than nothing.

namespace ctbrowser::shell {

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
    [[nodiscard]] bool has_decoder() const noexcept { return static_cast<bool>(decoder_); }

    // Loads at most once per name: two <img src="x"> and a script loadImage("x")
    // share one decode and one bitmap.
    [[nodiscard]] std::shared_ptr<const paint::bitmap> load(const asset_registry & assets,
                                                            std::string_view name) {
        for (const auto & [cached, image] : cache_) {
            if (cached == name) { return image; }
        }
        const std::vector<std::byte> bytes = assets.load(name);
        std::shared_ptr<const paint::bitmap> image;
        if (!bytes.empty()) {
            paint::bitmap decoded = decode_bmp(bytes);
            if (decoded.empty() && decoder_) { decoded = decoder_(bytes, name); }
            if (!decoded.empty()) {
                image = std::make_shared<const paint::bitmap>(std::move(decoded));
            }
        }
        // A FAILED load is cached too, as a null. Otherwise a page with a
        // missing sprite re-reads the filesystem every frame.
        cache_.emplace_back(std::string{name}, image);
        return image;
    }

    // The script-facing handle: a stable index, because a script holds numbers
    // and a vector of shared_ptr moves its elements.
    [[nodiscard]] int handle_for(const asset_registry & assets, std::string_view name) {
        const std::shared_ptr<const paint::bitmap> image = load(assets, name);
        if (!image) { return -1; }
        for (std::size_t i = 0; i < handles_.size(); ++i) {
            if (handles_[i] == image) { return static_cast<int>(i); }
        }
        handles_.push_back(image);
        return static_cast<int>(handles_.size()) - 1;
    }
    [[nodiscard]] std::shared_ptr<const paint::bitmap> at(int handle) const {
        if (handle < 0 || static_cast<std::size_t>(handle) >= handles_.size()) { return nullptr; }
        return handles_[static_cast<std::size_t>(handle)];
    }

private:
    std::vector<std::pair<std::string, std::shared_ptr<const paint::bitmap>>> cache_;
    std::vector<std::shared_ptr<const paint::bitmap>> handles_;
    decode_fn decoder_;
};

} // namespace ctbrowser::shell
