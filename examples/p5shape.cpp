// p5.js shapes: beginShape/vertex, bezier vertices, and the fill rule.
//
// The one that would have caught the hollow-star bug. p5 builds every shape as
// a Path2D and hands it to the canvas, so this is the path pipeline end to end:
// its own vertex list, through its shape visitor, into a recording, into a
// scanline fill that has to decide which points are inside a path that crosses
// itself. Nonzero and even-odd differ on exactly that, and the difference is
// the whole middle of the star.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "p5.js shapes";
    options.width = 360;
    options.height = 220;
    return ctbrowser::run_app_file("examples/pages/p5-shape.html", options);
}
