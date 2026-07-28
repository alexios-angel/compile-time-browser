// The web-compat proof: the MDN breakout tutorial, with ONE line changed.
//
// examples/pages/pong.html is the tutorial's final source. Nothing in it was
// written for this engine. It uses canvas.getContext("2d"), paths and arcs,
// requestAnimationFrame, document.addEventListener for keys and the mouse,
// Math, and canvas.width - which is precisely why it is worth keeping around:
// every one of those had to work before it would run at all.
//
// THE ONE DEVIATION is the clamp in mouseMoveHandler. MDN's version gates on
// the CURSOR being inside the canvas and then sets paddleX to
// relativeX - paddleWidth/2 without clamping the resulting RECT, so the paddle
// hangs up to half its width off either edge. Chrome and Firefox do the same
// thing with the same source - it is MDN's bug, not the engine's - but an
// example that visibly misbehaves is the wrong kind of evidence, so the paddle
// is clamped to the canvas here. Recorded so the next reader does not "restore"
// it and reintroduce the defect.
//
// The page path is relative to the repository root, the same convention the previous engine's
// examples used for their assets.

#include <ctbrowser/ctbrowser.hpp>

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
