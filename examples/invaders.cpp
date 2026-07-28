// Sprites, sound and a game loop: space invaders at 320x240, letterboxed into
// whatever size the window is.
//
// This is the the previous engine example ported OFF the previous engine's shorthand. The page now
// uses the same APIs it would in any browser - an <img> for the sprite sheet, the nine-argument
// drawImage for the sheet cells, keydown/keyup for input, requestAnimationFrame for the clock. The
// one thing that is not a web API is `playSound`, because there is no <audio> element yet; run_app
// installs it as an embedder native, which is at least honest about being an extension.
//
// Assets are the the previous engine ones (tools/gen-assets.py generates them, deterministically)
// and are resolved relative to the repository root, which is where the test
// runs from.

import ctbrowser;

int main() {
    ctbrowser::app_options options;
    options.title = "ctinvaders";
    options.width = 960;
    options.height = 720;
    // Render at the playfield's size and letterbox it: the game is written in
    // 320x240 coordinates and should not care what the window is.
    options.logical_width = 320;
    options.logical_height = 240;
    options.max_fps = 60;
    return ctbrowser::run_app_file("examples/pages/invaders.html", options);
}
