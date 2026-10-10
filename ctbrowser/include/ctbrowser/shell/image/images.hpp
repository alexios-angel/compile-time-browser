//===- images.hpp - Image decoding and the image store ----------*- C++ -*-===//
//
// Decoding images into the bitmap the display list already carries.
//
// BMP is decoded here by hand (uncompressed 24/32bpp, which every image tool
// can write). PNG goes through libpng (`image/png.cpp`) and JPEG through
// libjpeg-turbo (`image/jpeg.cpp`), both part of the SDL-free engine: a format
// whose result depended on whether SDL was found is one no golden can compare.
// Everything else (GIF, WebP, TIFF) arrives through image_store's decoder, a
// hook the application layer fills from SDL3_image when it was found.
//
// No third-party header is included here: png.cpp is the only translation
// unit that sees libpng, and jpeg.cpp the only one that sees libjpeg-turbo.
//
//===----------------------------------------------------------------------===//
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

namespace ctbrowser::shell {

/// Decodes a PNG of any colour type and bit depth, interlaced included, to the
/// engine's 8-bit ARGB. Returns an empty bitmap for anything truncated,
/// corrupt, or not a PNG at all.
[[nodiscard]] paint::bitmap decode_png(std::span<const std::byte> data);

/// Encodes \p image as an 8-bit-per-channel RGBA PNG, the encoding a decoder
/// needs no options for; what `canvas.toDataURL()` and `toBlob()` hand back.
/// Empty for an empty bitmap.
[[nodiscard]] std::vector<std::byte> encode_png(const paint::bitmap & image);

/// Decodes a baseline or progressive, greyscale or colour JPEG with any
/// subsampling to the engine's 8-bit ARGB, alpha fully opaque (JPEG has no
/// transparency; an alpha byte left at 0 would draw as nothing). Returns an
/// empty bitmap for anything that is not a JPEG this can read.
[[nodiscard]] paint::bitmap decode_jpeg(std::span<const std::byte> data);

/// Whether \p data starts with the PNG signature. Cheap enough to ask of every
/// load, which lets image_store try formats in order without a decode attempt
/// per format.
[[nodiscard]] bool looks_like_png(std::span<const std::byte> data) noexcept;
/// Whether \p data starts with the JPEG SOI marker.
[[nodiscard]] bool looks_like_jpeg(std::span<const std::byte> data) noexcept;

/// Total-pixel budget for every decoder: 2^26 (67,108,864, 8192x8192),
/// checked in 64-bit before any allocation, so a tiny header cannot demand
/// gigabytes.
inline constexpr std::uint64_t max_decode_pixels = std::uint64_t{1} << 26;

/// Decodes an uncompressed 24- or 32-bit BMP. Returns an empty bitmap on any
/// problem: truncated, or a flavour this does not read.
[[nodiscard]] inline paint::bitmap decode_bmp(std::span<const std::byte> data) {
    const auto byte_at = [&](std::size_t i) { return static_cast<std::uint32_t>(data[i]); };
    const auto u32 = [&](std::size_t i) {
        return byte_at(i) | (byte_at(i + 1) << 8) | (byte_at(i + 2) << 16) | (byte_at(i + 3) << 24);
    };
    const auto u16 = [&](std::size_t i) { return byte_at(i) | (byte_at(i + 1) << 8); };

    if (data.size() < 54 || byte_at(0) != 'B' || byte_at(1) != 'M') { return {}; }
    const std::uint32_t pixel_offset = u32(10);
    if (u32(14) < 40) { return {}; } // The header is older than BITMAPINFOHEADER.
    const auto width = static_cast<std::int32_t>(u32(18));
    const auto raw_height = static_cast<std::int32_t>(u32(22));
    const std::uint32_t bits_per_pixel = u16(28);
    const std::uint32_t compression = u32(30);
    if (width <= 0 || raw_height == 0 || raw_height == INT32_MIN ||
        (bits_per_pixel != 24 && bits_per_pixel != 32) || (compression != 0 && compression != 3)) {
        return {};
    }

    // A negative height means the rows are stored top-down; the usual positive
    // one means bottom-up, so the rows are read in reverse.
    const bool top_down = raw_height < 0;
    const std::int32_t height = top_down ? -raw_height : raw_height;
    const std::size_t bytes_per_pixel = bits_per_pixel / 8U;
    if (static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
        max_decode_pixels) {
        return {};
    }
    const std::size_t stride =
        (static_cast<std::size_t>(width) * bytes_per_pixel + 3U) & ~std::size_t{3};
    if (std::uint64_t{data.size()} <
        std::uint64_t{pixel_offset} + std::uint64_t{stride} * static_cast<std::uint64_t>(height)) {
        return {};
    }

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

/// The decoded images of a page: what a script's image handle refers to, and
/// what an <img> element resolves to. Bitmaps are shared_ptr because the
/// display list holds them too; a re-record must not copy every sprite.
///
/// Handles: 0 and up index the load cache; -1 is a failed load; -2 and below
/// are bitmaps a script made (see adopt()).
class image_store {
public:
    /// Decoder for formats the engine does not decode itself (anything other
    /// than BMP, PNG and JPEG, e.g. GIF, WebP, TIFF). The application layer
    /// installs SDL3_image here when the build found it. Without one, such an
    /// image fails to load and the page sees a zero-sized image rather than a
    /// crash.
    using decode_fn = std::function<paint::bitmap(std::span<const std::byte>, std::string_view)>;

    void set_decoder(decode_fn decoder) { decoder_ = std::move(decoder); }

    /// The bitmap for \p name, or null when it failed to load. Loads at most
    /// once per name: two <img src="x"> and a script loadImage("x") share one
    /// decode and one bitmap.
    [[nodiscard]] std::shared_ptr<const paint::bitmap> load(const asset_registry & assets,
                                                            std::string_view name) {
        return cache_[slot_for(assets, name)].second;
    }

    /// The script-facing handle for \p name: a stable index, because a script
    /// holds numbers and a vector of shared_ptr moves its elements. -1 for a
    /// load that failed.
    [[nodiscard]] int handle_for(const asset_registry & assets, std::string_view name) {
        const std::size_t slot = slot_for(assets, name);
        return cache_[slot].second ? static_cast<int>(slot) : -1;
    }
    /// The bitmap behind \p handle (loaded or adopted), or null.
    [[nodiscard]] std::shared_ptr<const paint::bitmap> at(int handle) const {
        if (handle <= -2) {
            const auto slot = static_cast<std::size_t>(-2 - static_cast<long long>(handle));
            return slot < adopted_.size() ? adopted_[slot] : nullptr;
        }
        if (handle < 0 || static_cast<std::size_t>(handle) >= cache_.size()) { return nullptr; }
        return cache_[static_cast<std::size_t>(handle)].second;
    }

    /// Decodes bytes that came from no asset (a Blob's, for createImageBitmap)
    /// with the same formats in the same order as a load. Empty for bytes that
    /// are no image.
    [[nodiscard]] paint::bitmap decode(std::span<const std::byte> bytes,
                                       std::string_view name) const {
        if (bytes.empty()) { return {}; }
        paint::bitmap decoded = decode_bmp(bytes);
        // PNG and JPEG are tried before the hook, so a headless test and an
        // application with SDL3_image decode the same file with the same
        // library and get the same pixels.
        if (decoded.empty() && looks_like_png(bytes)) { decoded = decode_png(bytes); }
        if (decoded.empty() && looks_like_jpeg(bytes)) { decoded = decode_jpeg(bytes); }
        if (decoded.empty() && decoder_) { decoded = decoder_(bytes, name); }
        return decoded;
    }

    /// Stores a bitmap a script made (an ImageBitmap) under a handle of its own
    /// below -1, so at(), which drawImage asks, answers it too. ponytail:
    /// released on close() only; an ImageBitmap the page drops keeps its pixels
    /// until the store goes (a weak slot swept with the wrapper when that
    /// matters).
    [[nodiscard]] int adopt(std::shared_ptr<const paint::bitmap> image) {
        adopted_.push_back(std::move(image));
        return -2 - static_cast<int>(adopted_.size() - 1);
    }
    /// Frees an adopted bitmap (ImageBitmap.close()); other handles are ignored.
    void release(int handle) {
        if (handle > -2) { return; }
        const auto slot = static_cast<std::size_t>(-2 - static_cast<long long>(handle));
        if (slot < adopted_.size()) { adopted_[slot] = nullptr; }
    }

private:
    // The cache only ever appends, so an entry's index is its handle.
    [[nodiscard]] std::size_t slot_for(const asset_registry & assets, std::string_view name) {
        for (std::size_t i = 0; i < cache_.size(); ++i) {
            if (cache_[i].first == name) { return i; }
        }
        const std::vector<std::byte> bytes = assets.load(name);
        std::shared_ptr<const paint::bitmap> image;
        if (paint::bitmap decoded = decode(bytes, name); !decoded.empty()) {
            image = std::make_shared<const paint::bitmap>(std::move(decoded));
        }
        // A failed load is cached too, as a null. Otherwise a page with a
        // missing sprite re-reads the filesystem every frame.
        cache_.emplace_back(std::string{name}, std::move(image));
        return cache_.size() - 1;
    }

    std::vector<std::pair<std::string, std::shared_ptr<const paint::bitmap>>> cache_;
    std::vector<std::shared_ptr<const paint::bitmap>> adopted_; // See adopt().
    decode_fn decoder_;
};

} // namespace ctbrowser::shell
