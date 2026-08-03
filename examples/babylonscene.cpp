// Babylon.js, the third library, driving this engine.
//
// p5 and Phaser are 2D frameworks that reach WebGL; Babylon is a 3D ENGINE. It
// brings a scene graph, a camera and light model, a material system and a
// shader it assembles at run time out of a hundred `#define`s - and it delivers
// every uniform through a BUFFER, which is what made it the corpus that found
// the WebGL 2 gaps. See docs/babylon-plan.md and tests/babylon-ratchet.txt.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "Babylon.js";
    options.width = 360;
    options.height = 260;
    return ctbrowser::run_app_file("examples/pages/babylon-scene.html", options);
}
