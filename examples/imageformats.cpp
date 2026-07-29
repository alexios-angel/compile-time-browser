// PNG through SDL3_image, with a golden.
//
// The engine decodes BMP itself; everything else arrives through the optional
// SDL3_image, and nothing in the tree exercised that path - so `<img src=x.png>`
// could have been blank on every build and no test would have said so.
//
// PNG rather than JPEG for the golden, and the reason is byte-exactness: PNG is
// LOSSLESS, so two platforms decode it identically. JPEG works too - it goes
// through the same decoder - but two libjpeg builds may differ in the last bit,
// which would make the golden platform-dependent for a reason that is not a
// regression. docs/raster.md says so.
//
// Gated on CTBROWSER_WITH_IMAGE in examples/CMakeLists.txt, exactly as the SVG
// golden is gated on plutosvg: without the dependency the page lays out the same
// and simply draws no image, and comparing then would fail for the wrong reason.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "image formats";
    options.width = 120;
    options.height = 200;
    return ctbrowser::run_app_file("examples/pages/image-formats.html", options);
}
