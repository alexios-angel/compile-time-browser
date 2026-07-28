// The web-compat proof: the MDN breakout tutorial, UNMODIFIED.
//
// examples/pages/pong.html is a byte-for-byte copy of the tutorial's final
// source. Nothing in it was written for this engine, and nothing in it was
// changed to suit one. It uses canvas.getContext("2d"), paths and arcs,
// requestAnimationFrame, document.addEventListener for keys and the mouse,
// Math, and canvas.width - which is precisely why it is worth keeping around:
// every one of those had to work before it would run at all.
//
// The page path is relative to the repository root, the same convention the previous engine's
// examples used for their assets.

import ctbrowser;

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
