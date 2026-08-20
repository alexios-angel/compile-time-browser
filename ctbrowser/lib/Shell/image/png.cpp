#include <ctbrowser/shell/image/png.hpp>

// THE ONLY TRANSLATION UNIT THAT KNOWS LIBPNG EXISTS. png.hpp declares two
// functions and includes nothing third-party, which is the rule url.cpp follows
// for Boost.URL and net_curl.cpp for curl.h.
//
// LIBPNG, the reference implementation - the one the format's own maintainers
// write, shipped by every distribution and linked by every browser. A PNG
// decoder is a deflate stream, five filter types, seven colour-type-and-depth
// combinations, palettes, tRNS, gamma and an interlace pass order, and the parts
// that go wrong are the ones no test corpus here would cover.
//
// THE SIMPLIFIED API (libpng 1.6+), not the traditional callback one: it is
// twelve lines instead of eighty, it does the format conversion this engine
// wants in the library rather than in a loop here, and it has no setjmp in the
// caller. `png_image` is a stack struct per call, so nothing here is shared
// between threads.
#include <png.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ctbrowser::shell {

bool looks_like_png(std::span<const std::byte> data) noexcept {
    // libpng's own answer, rather than eight bytes retyped here.
    if (data.size() < 8) { return false; }
    return png_sig_cmp(reinterpret_cast<png_const_bytep>(data.data()), 0, 8) == 0;
}

paint::bitmap decode_png(std::span<const std::byte> data) {
    if (!looks_like_png(data)) { return {}; }

    png_image image;
    std::memset(&image, 0, sizeof image);
    image.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_memory(&image, data.data(), data.size()) == 0) {
        // Nothing was allocated when begin_read fails, and calling png_image_free
        // here would be wrong rather than merely unnecessary.
        return {};
    }

    // RGBA8 OUT, WHATEVER WENT IN. libpng converts from the file's own colour
    // type and bit depth - palette, greyscale, 16-bit, interlaced - so the
    // engine never sees any of them. Phaser's `__DEFAULT` texture is 1-bit
    // palette with a tRNS chunk, which is exactly what a hand-written decoder
    // gets wrong and then calls a corrupt file.
    image.format = PNG_FORMAT_RGBA;

    // A guard on the arithmetic below rather than on the image: a PNG large
    // enough to overflow the engine's signed pixel coordinates is refused
    // rather than wrapped into a small one.
    constexpr png_uint_32 coordinate_limit = 1U << 30;
    if (image.width == 0 || image.height == 0 || image.width >= coordinate_limit ||
        image.height >= coordinate_limit) {
        png_image_free(&image);
        return {};
    }

    std::vector<unsigned char> rgba(PNG_IMAGE_SIZE(image));
    // finish_read frees the read structure on both paths, success and failure,
    // so there is no png_image_free after it.
    if (png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr) == 0) { return {}; }

    paint::bitmap out{static_cast<std::int32_t>(image.width),
                      static_cast<std::int32_t>(image.height)};
    // libpng hands back RGBA bytes in reading order; the display list carries
    // packed ARGB. NOT premultiplied - `paint` composites with straight alpha,
    // which is what decode_bmp produces too.
    std::size_t at = 0;
    for (std::int32_t y = 0; y < static_cast<std::int32_t>(image.height); ++y) {
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(image.width); ++x, at += 4) {
            const std::uint32_t red = rgba[at];
            const std::uint32_t green = rgba[at + 1];
            const std::uint32_t blue = rgba[at + 2];
            const std::uint32_t alpha = rgba[at + 3];
            out.put(x, y, (alpha << 24) | (red << 16) | (green << 8) | blue);
        }
    }
    return out;
}

} // namespace ctbrowser::shell
