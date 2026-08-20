// WebGL through the real API, with a golden.
//
// The first page whose picture proves the whole stack: the GLSL preprocessor
// and parser, the reference evaluator running a shader per vertex and per
// fragment, the software rasteriser, the context's state machine and the
// binding layer. Any one of them failing draws a blank canvas rather than
// raising anything, which is exactly why this is compared byte for byte.
//
// No p5: its own WEBGL path still selects its 2D renderer for reasons inside
// p5, and a corpus page depending on that would test the library.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "WebGL: a rotated triangle";
    options.width = 180;
    options.height = 180;
    return ctbrowser::run_app_file("examples/pages/webgl-triangle.html", options);
}
