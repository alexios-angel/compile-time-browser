#include <ctbrowser/shell/jpeg.hpp>

// THE ONLY TRANSLATION UNIT THAT KNOWS LIBJPEG-TURBO EXISTS. jpeg.hpp declares
// two functions and includes nothing third-party, which is the rule url.cpp
// follows for Boost.URL and png.cpp for libpng.
//
// LIBJPEG-TURBO, which is libjpeg's API with SIMD underneath and the decoder
// essentially everything ships: browsers, Android, and every distribution's
// libjpeg for over a decade. A JPEG decoder is an entropy-coded DCT with
// baseline and progressive scan orders, four subsampling modes, restart markers
// and colour transforms, and none of it is worth writing here.
//
// THE TurboJPEG API (`turbojpeg.h`, the tj3_* generation), not libjpeg's own:
// it decodes from a memory buffer in four calls with no source manager to
// implement, and it reports errors by return code rather than through the
// setjmp/longjmp error handler libjpeg's native API requires a caller to
// install. A longjmp out of a C++ frame would skip the destructors of
// everything between here and there, which is precisely what this engine must
// not do.
#include <turbojpeg.h>

#include <cstdint>
#include <vector>

namespace ctbrowser::shell {

bool looks_like_jpeg(std::span<const std::byte> data) noexcept {
    // SOI: every JPEG starts FF D8, and the next marker byte is always FF too.
    // Two bytes alone would accept a great deal that is not a JPEG.
    if (data.size() < 3) { return false; }
    return static_cast<unsigned char>(data[0]) == 0xFF &&
           static_cast<unsigned char>(data[1]) == 0xD8 &&
           static_cast<unsigned char>(data[2]) == 0xFF;
}

paint::bitmap decode_jpeg(std::span<const std::byte> data) {
    if (!looks_like_jpeg(data)) { return {}; }

    tjhandle handle = tj3Init(TJINIT_DECOMPRESS);
    if (handle == nullptr) { return {}; }
    // ONE EXIT, so the handle cannot leak past an early return. There are four
    // failure points below and a scope guard is the only way this reads.
    struct closer {
        tjhandle h;
        ~closer() { tj3Destroy(h); }
    } const guard{handle};

    const auto * bytes = reinterpret_cast<const unsigned char *>(data.data());
    if (tj3DecompressHeader(handle, bytes, data.size()) != 0) { return {}; }

    const int width = tj3Get(handle, TJPARAM_JPEGWIDTH);
    const int height = tj3Get(handle, TJPARAM_JPEGHEIGHT);
    if (width <= 0 || height <= 0) { return {}; }
    // A guard on the arithmetic rather than on the image: a JPEG large enough
    // to overflow the engine's pixel coordinates is refused rather than wrapped
    // into a small one.
    constexpr int coordinate_limit = 1 << 30;
    if (width >= coordinate_limit || height >= coordinate_limit) { return {}; }

    // RGB, THREE BYTES, NOT TJPF_RGBA. TurboJPEG's RGBA pixel formats leave the
    // fourth byte UNDEFINED - the format has no alpha to put there - so asking
    // for RGBA and trusting it is how an image decodes correctly and then draws
    // as fully transparent. The alpha is set here, deliberately, to opaque.
    const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<unsigned char> rgb(pixels * 3U);
    if (tj3Decompress8(handle, bytes, data.size(), rgb.data(), 0, TJPF_RGB) != 0) { return {}; }

    paint::bitmap out{width, height};
    std::size_t at = 0;
    for (std::int32_t y = 0; y < height; ++y) {
        for (std::int32_t x = 0; x < width; ++x, at += 3) {
            const std::uint32_t red = rgb[at];
            const std::uint32_t green = rgb[at + 1];
            const std::uint32_t blue = rgb[at + 2];
            out.put(x, y, (0xFFU << 24) | (red << 16) | (green << 8) | blue);
        }
    }
    return out;
}

} // namespace ctbrowser::shell
