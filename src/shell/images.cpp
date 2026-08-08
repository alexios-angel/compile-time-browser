#include <ctbrowser/shell/images.hpp>

#include <boost/crc.hpp>

// PNG ENCODING, split out of images.hpp on 2026-08-08.
//
// It was `inline` in the public header, which made it the odd one out: BMP,
// PNG and JPEG DECODING each already live in one .cpp behind a two-function
// header, and CLAUDE.md's rule is that no third-party header belongs in a
// public one. Wanting Boost.CRC here is what made the inconsistency cost
// something, so the function moved rather than the rule bending.

namespace ctbrowser::shell {

std::vector<std::byte> encode_png(const paint::bitmap & image) {
    if (image.empty()) { return {}; }
    const auto width = static_cast<std::uint32_t>(image.width);
    const auto height = static_cast<std::uint32_t>(image.height);

    // BOOST.CRC, not a 256-entry table rebuilt on every call. `boost::crc_32_type`
    // IS the PNG polynomial - CRC-32/ISO-HDLC, 0x04C11DB7 reflected, which is
    // 0xEDB88320 the way the old loop spelled it, init and final xor 0xFFFFFFFF -
    // so this is the same checksum from a library rather than from memory.
    // tools/check-png.py verifies the bytes with Python's own zlib, which is
    // what says so independently of this file.
    const auto crc_of = [](std::span<const unsigned char> bytes) {
        boost::crc_32_type crc;
        crc.process_bytes(bytes.data(), bytes.size());
        return crc.checksum();
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

} // namespace ctbrowser::shell
