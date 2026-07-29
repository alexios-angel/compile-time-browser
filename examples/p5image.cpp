// p5.js images: loadImage, end to end and on screen.
//
// The golden is the point. `loadImage` is a chain - fetch, Blob, object URL,
// Image, onload, revoke, draw - and every link fails the same way: nothing
// appears and nothing is reported. A rendered frame compared byte for byte is
// the only check that covers all of it at once, including the trap in the
// middle: p5 revokes the object URL INSIDE onload, before the image is drawn, so
// this page only works because a decode is cached and revoking frees the bytes
// rather than the bitmap.
//
// BMP, so the page means the same thing with and without SDL3_image -
// examples/pages/image-formats.html is where PNG and JPEG are checked.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "p5.js images";
    options.width = 320;
    options.height = 220;
    return ctbrowser::run_app_file("examples/pages/p5-image.html", options);
}
