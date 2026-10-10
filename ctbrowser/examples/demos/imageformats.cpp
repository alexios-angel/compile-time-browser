//===- ctbrowser/examples/demos/imageformats.cpp - PNG decoding demo ------===//
//
// Renders a PNG badge at three sizes and compares it with a golden image, so
// `<img src=x.png>` decoding is covered by a test. The badge's four coloured
// quadrants make a flipped, channel-swapped or alpha-less decode obvious.
//
// PNG rather than JPEG for the golden because PNG is lossless and decodes
// identically on every platform; two libjpeg builds may differ in the last bit
// (docs/raster.md).
//
// The golden is registered only when SDL3_image is found or
// CTBROWSER_WITH_IMAGE is set (examples/CMakeLists.txt).
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "image formats";
    options.width = 120;
    options.height = 200;
    options.confine_assets = false; // Trusted in-tree page; it loads from examples/assets/.
    return ctbrowser::run_app_file("examples/pages/image-formats.html", options);
}
