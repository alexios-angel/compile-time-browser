#include <ctbrowser/shell/image/jpeg.hpp>

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
// THE TurboJPEG API (`turbojpeg.h`), not libjpeg's own: it decodes from a
// memory buffer in three calls with no source manager to implement, and it
// reports errors by return code rather than through the setjmp/longjmp error
// handler libjpeg's native API requires a caller to install. A longjmp out of a
// C++ frame would skip the destructors of everything between here and there,
// which is precisely what this engine must not do.
//
// THE 2.x SPELLING (`tjInitDecompress`/`tjDecompressHeader3`/`tjDecompress2`),
// NOT the tj3_* generation, and that is a portability decision rather than a
// stylistic one. The tj3 API arrived in libjpeg-turbo 3.0; Ubuntu 24.04 LTS -
// which is what the shared devbox runs - ships 2.1.5, so a tj3 call is simply
// not declared there and the build stops dead. The 2.x entry points are still
// exported by 3.x and carry no deprecation attribute, so ONE code path covers
// noble's 2.1.5, the 3.2 this machine has through brew, and the 3.1.2 in the
// mingw sysroot. A version #ifdef would be two paths where one does.
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
    // ONE EXIT, so the handle cannot leak past an early return. There are four
    // failure points below and a scope guard is the only way this reads.
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

    // RGB, THREE BYTES, NOT TJPF_RGBA. TurboJPEG's RGBA pixel formats leave the
    // fourth byte UNDEFINED - the format has no alpha to put there - so asking
    // for RGBA and trusting it is how an image decodes correctly and then draws
    // as fully transparent. The alpha is set here, deliberately, to opaque.
    const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<unsigned char> rgb(pixels * 3U);
    // pitch 0 means "tightly packed", which is what the loop below assumes.
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
