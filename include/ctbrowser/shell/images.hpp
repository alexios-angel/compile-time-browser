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

#include <ctbrowser/shell/assets.hpp>
#include <ctbrowser/shell/jpeg.hpp>
#include <ctbrowser/shell/png.hpp>

// Decoding images into the bitmap the display list already carries.
//
// BMP is built in - uncompressed 24/32bpp, which every image tool can write and
// which needs no library at all. PNG goes through libpng (`png.hpp`) and JPEG
// through libjpeg-turbo (`jpeg.hpp`), both part of the SDL-free engine.
// Everything else - GIF, WEBP, TIFF - arrives through `decoder`, a hook the
// application layer fills in from SDL3_image when it was found.
//
// PNG MOVED OUT OF THAT HOOK on 2026-08-01. Leaving it there meant `tests/`,
// which is SDL-free by an invariant `tests/api_surface` lints for, saw every
// PNG as a zero-sized image - and nothing in the suite said so, because the
// pages in this tree load BMPs. Phaser found it: its texture manager loads
// three base64 PNGs during boot and will not start until all three settle.

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

// Encode a bitmap as a PNG, with NO COMPRESSION LIBRARY.
//
// `canvas.toBlob()` and `canvas.toDataURL()` mean PNG - that is what p5's
// save() asks for and what a page expects to get - so an engine that cannot
// write one cannot export anything.
//
// A PNG's pixel data is a zlib stream, and a zlib stream may be made entirely of
// STORED blocks: a five-byte header per block and the bytes verbatim. That is
// valid deflate, so every decoder in the world reads this, and it needs no zlib.
// The file is bigger than a compressed one - about 1.05x the raw pixels - which
// is the whole cost, and it buys the engine one fewer dependency in a header
// that is part of the SDL-free core.
//
// RGBA, 8 bits per channel, filter 0 on every row: the encoding a decoder needs
// no options for.
[[nodiscard]] inline std::vector<std::byte> encode_png(const paint::bitmap & image) {
    if (image.empty()) { return {}; }
    const auto width = static_cast<std::uint32_t>(image.width);
    const auto height = static_cast<std::uint32_t>(image.height);

    static constexpr auto crc_of = [](std::span<const unsigned char> bytes) {
        // The table is built on the fly: 256 entries computed once per call is
        // nothing beside the pixels, and it keeps this a header with no state.
        std::uint32_t table[256];
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) {
                c = (c & 1) != 0 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        std::uint32_t crc = 0xFFFFFFFFU;
        for (const unsigned char byte : bytes) { crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8); }
        return crc ^ 0xFFFFFFFFU;
    };

    std::vector<unsigned char> out;
    const auto put = [&out](std::initializer_list<unsigned char> bytes) {
        out.insert(out.end(), bytes.begin(), bytes.end());
    };
    const auto put_be32 = [&out](std::uint32_t v) {
        out.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
        out.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
        out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
        out.push_back(static_cast<unsigned char>(v & 0xFF));
    };
    // A chunk is length, type, data, then a CRC over the TYPE AND DATA - not
    // over the length, which is the mistake that makes a file no decoder opens.
    const auto chunk = [&](const char (&type)[5], const std::vector<unsigned char> & data) {
        put_be32(static_cast<std::uint32_t>(data.size()));
        const std::size_t crc_from = out.size();
        for (int i = 0; i < 4; ++i) { out.push_back(static_cast<unsigned char>(type[i])); }
        out.insert(out.end(), data.begin(), data.end());
        put_be32(
            crc_of(std::span<const unsigned char>{out.data() + crc_from, out.size() - crc_from}));
    };

    put({0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A});
    {
        std::vector<unsigned char> header;
        const auto be32 = [&header](std::uint32_t v) {
            header.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
            header.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
            header.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
            header.push_back(static_cast<unsigned char>(v & 0xFF));
        };
        be32(width);
        be32(height);
        header.push_back(8); // bits per channel
        header.push_back(6); // colour type 6 = RGBA
        header.push_back(0); // deflate
        header.push_back(0); // filter method 0
        header.push_back(0); // not interlaced
        chunk("IHDR", header);
    }

    // The raw scanlines: a filter byte then RGBA, in that order. bitmap::at is
    // 0xAARRGGBB, so the channels are pulled out rather than memcpy'd.
    std::vector<unsigned char> raw;
    raw.reserve((static_cast<std::size_t>(width) * 4U + 1U) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0); // filter: none
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint32_t pixel = image.at(static_cast<int>(x), static_cast<int>(y));
            raw.push_back(static_cast<unsigned char>((pixel >> 16) & 0xFF));
            raw.push_back(static_cast<unsigned char>((pixel >> 8) & 0xFF));
            raw.push_back(static_cast<unsigned char>(pixel & 0xFF));
            raw.push_back(static_cast<unsigned char>((pixel >> 24) & 0xFF));
        }
    }

    {
        std::vector<unsigned char> zlib;
        zlib.push_back(0x78); // deflate, 32K window
        zlib.push_back(0x01); // no preset dictionary, fastest
        // Stored blocks, 65535 bytes at a time: LEN then its one's complement,
        // which is what tells a decoder the block really is uncompressed.
        for (std::size_t at = 0; at < raw.size(); at += 65535U) {
            const auto len =
                static_cast<std::uint16_t>(std::min<std::size_t>(65535U, raw.size() - at));
            const bool last = at + len >= raw.size();
            zlib.push_back(last ? 1 : 0);
            zlib.push_back(static_cast<unsigned char>(len & 0xFF));
            zlib.push_back(static_cast<unsigned char>((len >> 8) & 0xFF));
            zlib.push_back(static_cast<unsigned char>(~len & 0xFF));
            zlib.push_back(static_cast<unsigned char>((~len >> 8) & 0xFF));
            zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(at),
                        raw.begin() + static_cast<std::ptrdiff_t>(at + len));
        }
        // ADLER-32 over the UNCOMPRESSED bytes, big-endian, and it is what a
        // decoder checks to decide the stream was not corrupted.
        std::uint32_t a = 1;
        std::uint32_t b = 0;
        for (const unsigned char byte : raw) {
            a = (a + byte) % 65521U;
            b = (b + a) % 65521U;
        }
        zlib.push_back(static_cast<unsigned char>((b >> 8) & 0xFF));
        zlib.push_back(static_cast<unsigned char>(b & 0xFF));
        zlib.push_back(static_cast<unsigned char>((a >> 8) & 0xFF));
        zlib.push_back(static_cast<unsigned char>(a & 0xFF));
        chunk("IDAT", zlib);
    }
    chunk("IEND", {});

    std::vector<std::byte> bytes(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) { bytes[i] = static_cast<std::byte>(out[i]); }
    return bytes;
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
