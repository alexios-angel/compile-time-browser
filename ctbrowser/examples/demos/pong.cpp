//===- ctbrowser/examples/demos/pong.cpp - MDN breakout demo -- -*- C++ -*-===//
//
// Web compatibility check: the MDN breakout tutorial, with one change.
//
// examples/pages/pong.html is the tutorial's final source; nothing in it was
// written for this engine. It uses canvas.getContext("2d"), paths and arcs,
// requestAnimationFrame, document.addEventListener for keys and the mouse, Math
// and canvas.width, all of which must work for it to run.
//
// The one deviation is a clamp in mouseMoveHandler. MDN's version checks that
// the cursor is inside the canvas, then sets paddleX to relativeX -
// paddleWidth/2 without clamping the paddle itself, so it hangs up to half its
// width off either edge (Chrome and Firefox do the same with that source). The
// paddle is clamped to the canvas here; do not "restore" the original.
//
// The page path is relative to the ctbrowser root, so run the example from
// there.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "pong";
    options.width = 480;
    options.height = 340;
    // The page is a fixed-step game and depends on the frame cap, exactly as
    // it does in a real browser.
    options.max_fps = 60;
    return ctbrowser::run_app_file("examples/pages/pong.html", options);
}
