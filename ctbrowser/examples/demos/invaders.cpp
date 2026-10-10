//===- ctbrowser/examples/demos/invaders.cpp - Space invaders demo --------===//
//
// Sprites, sound and a game loop: space invaders at 320x240, letterboxed into
// whatever size the window is.
//
// The page uses the same APIs it would in any browser: an <img> for the sprite
// sheet, the nine-argument drawImage for sheet cells, keydown/keyup for input
// and requestAnimationFrame for the clock. The one non-web API is `playSound`,
// an embedder native that run_app installs because there is no <audio> element
// yet.
//
// The assets are generated deterministically by tools/gen/gen-assets.py and
// resolved relative to the ctbrowser root, where the test runs.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

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
    options.confine_assets = false; // Trusted in-tree page; it loads from examples/assets/.
    return ctbrowser::run_app_file("examples/pages/invaders.html", options);
}
