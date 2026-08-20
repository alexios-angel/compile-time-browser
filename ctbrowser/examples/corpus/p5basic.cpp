// p5.js, running on this engine.
//
// The whole 4.5 MB bundle loads, defines its ~200 drawing functions, builds a
// sketch, runs its setup() and drives its draw() from requestAnimationFrame -
// and what comes out is compared against a golden, byte for byte.
//
// test/corpus/p5/p5_ratchet.cpp measures how far the bundle GETS; this measures what it
// DRAWS, which is a different question and the one that actually matters. A
// sketch whose draw() is called and paints nothing passes the ratchet and fails
// here.
//
// The page is deterministic on purpose: noLoop(), no random(), no text, and no
// image - so one golden holds on every platform. Math.random is seeded here
// anyway (docs/architecture), but a sketch that leans on it has no business
// having a golden.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "p5.js";
    options.width = 360;
    options.height = 260;
    return ctbrowser::run_app_file("examples/pages/p5-basic.html", options);
}
