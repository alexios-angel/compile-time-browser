// p5.js pixels: the round trip through a byte buffer.
//
// loadPixels/updatePixels do not go through the drawing API at all - the
// sketch reads what was rasterised, edits bytes, and writes them back. That
// makes this the one page here that tests getImageData/putImageData, the
// clamped typed array behind `pixels[]`, and the RGBA-versus-packed-ARGB
// conversion in both directions. A channel swap in either one still produces a
// picture, which is why the page draws the same shapes twice and rotates the
// channels between them: the two halves can only differ the way the sketch
// asked.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "p5.js pixels";
    options.width = 320;
    options.height = 180;
    return ctbrowser::run_app_file("examples/pages/p5-pixels.html", options);
}
