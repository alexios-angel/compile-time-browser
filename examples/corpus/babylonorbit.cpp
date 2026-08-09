// Babylon.js with the mouse in it: drag to orbit, wheel to zoom.
//
// babylonscene.cpp proves a frame can be drawn. This proves the frame RESPONDS
// - an ArcRotateCamera reading pointer events off the canvas through Babylon's
// own input manager, with nothing in the page handling a mouse itself. The
// light is a directional one and is nowhere in the picture: what you see is the
// shading, which is the point.
//
// tests/corpus/babylon/babylon_interaction.cpp drives the same page with a synthetic drag.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "Babylon.js - drag to orbit";
    options.width = 420;
    options.height = 360;
    return ctbrowser::run_app_file("examples/pages/babylon-orbit.html", options);
}
