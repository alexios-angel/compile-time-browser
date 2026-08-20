// p5.js in WEBGL mode: the library driving this engine's WebGL, not a page.
//
// The raw corpus page (webgl-triangle.html) writes its own GLSL and calls
// drawArrays by hand, which proves the context works for a page that already
// knows exactly what it wants. This one proves something different and harder:
// that p5's RendererGL - somebody else's shaders, somebody else's matrices,
// somebody else's idea of what a WebGL 1 implementation owes it - gets what it
// asks for. Every gap between the two is a gap a real sketch would fall into.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "p5.js WEBGL";
    options.width = 360;
    options.height = 260;
    return ctbrowser::run_app_file("examples/pages/p5-webgl.html", options);
}
