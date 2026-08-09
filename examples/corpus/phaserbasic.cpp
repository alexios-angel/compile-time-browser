// Phaser 4, running on this engine.
//
// The whole bundle loads, `new Phaser.Game(...)` boots, the scene manager
// creates the scene, its create() draws through Phaser's own Graphics object
// and Canvas renderer - and what comes out is compared against a golden, byte
// for byte.
//
// tests/corpus/phaser/phaser_ratchet.cpp measures how far the bundle GETS; this measures what
// it DRAWS, which is a different question and the one that actually matters. A
// scene whose create() is called and paints nothing passes the ratchet and
// fails here. That is exactly the relationship p5basic has with p5_ratchet, and
// the reason both exist.
//
// The page is deterministic on purpose: CANVAS asked for by name, everything
// drawn in create(), an empty update(), and no text, image, random or physics -
// so one golden holds on every platform.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "Phaser 4";
    options.width = 360;
    options.height = 280;
    return ctbrowser::run_app_file("examples/pages/phaser-basic.html", options);
}
