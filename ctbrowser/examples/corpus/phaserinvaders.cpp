// Space Invaders, in Phaser 4, on this engine.
//
// THE POINT OF THE WHOLE PHASER CORPUS. test/corpus/phaser/phaser_ratchet.cpp says how far
// the bundle gets and test/corpus/phaser/phaser_api.cpp says how wide the surface is;
// neither is a game. This runs the combination only a real game exercises -
// sprites under arcade physics, a formation stepping on a timer, bullets,
// overlap callbacks destroying objects mid-frame, a score that updates and a
// scene that can end. Every piece passes a probe on its own; whether they work
// TOGETHER is a different claim, and this is the one that makes it.
//
// The page has no asset files: every texture is drawn at boot with a Graphics
// object and generateTexture. It is deterministic - no Math.random, a scripted
// player sweep when no key is held - so the frame it reaches is the same
// everywhere and can have a golden.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "Phaser Space Invaders";
    options.width = 320;
    options.height = 240;
    return ctbrowser::run_app_file("examples/pages/phaser-invaders.html", options);
}
