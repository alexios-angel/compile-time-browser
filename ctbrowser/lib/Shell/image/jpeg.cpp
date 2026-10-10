//===- ctbrowser/lib/Shell/image/jpeg.cpp - JPEG decoding -----------------===//
//
// JPEG decoding through libjpeg-turbo. This is the only translation unit that
// includes turbojpeg.h; images.hpp declares looks_like_jpeg and decode_jpeg
// without any third-party include, as png.cpp does for libpng.
//
// libjpeg-turbo is libjpeg's API with SIMD underneath and the decoder
// essentially everything ships: browsers, Android, and every distribution's
// libjpeg. A JPEG decoder is an entropy-coded DCT with baseline and
// progressive scan orders, four subsampling modes, restart markers and colour
// transforms, and none of it is worth writing here.
//
// It uses the TurboJPEG API (`turbojpeg.h`), not libjpeg's own: it decodes
// from a memory buffer in three calls with no source manager to implement, and
// reports errors by return code rather than through the setjmp/longjmp error
// handler libjpeg's native API requires. A longjmp out of a C++ frame would
// skip the destructors of everything in between.
//
// It uses the 2.x spelling (`tjInitDecompress`/`tjDecompressHeader3`/
// `tjDecompress2`), not the tj3_* functions, for portability: the tj3 API
// arrived in libjpeg-turbo 3.0, while Ubuntu 24.04 LTS ships 2.1.5. The 2.x
// entry points are still exported by 3.x without a deprecation attribute, so
// one code path covers 2.1.5, Homebrew's 3.2 and the 3.1.2 in the mingw
// sysroot without a version #ifdef.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/image/images.hpp>

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

    tjhandle handle = tjInitDecompress();
    if (handle == nullptr) { return {}; }
    // Scope guard so the handle is destroyed on every early return below.
    struct closer {
        tjhandle h;
        ~closer() { (void)tjDestroy(h); }
    } const guard{handle};

    const auto * bytes = reinterpret_cast<const unsigned char *>(data.data());
    int width = 0;
    int height = 0;
    int subsampling = 0;
    int colourspace = 0;
    if (tjDecompressHeader3(handle, bytes, static_cast<unsigned long>(data.size()), &width, &height,
                            &subsampling, &colourspace) != 0) {
        return {};
    }
    if (width <= 0 || height <= 0) { return {}; }
    // A guard on the arithmetic rather than on the image: a JPEG large enough
    // to overflow the engine's pixel coordinates is refused rather than wrapped
    // into a small one.
    constexpr int coordinate_limit = 1 << 30;
    if (width >= coordinate_limit || height >= coordinate_limit) { return {}; }
    if (static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
        max_decode_pixels) {
        return {};
    }

    // Decode to RGB (three bytes per pixel), not TJPF_RGBA: TurboJPEG's RGBA
    // pixel formats leave the fourth byte undefined, since JPEG has no alpha,
    // and trusting it could draw the image fully transparent. The alpha is set
    // to opaque in the loop below.
    const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<unsigned char> rgb(pixels * 3U);
    // Pitch 0 means "tightly packed", which is what the loop below assumes.
    if (tjDecompress2(handle, bytes, static_cast<unsigned long>(data.size()), rgb.data(), width, 0,
                      height, TJPF_RGB, 0) != 0) {
        return {};
    }

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
