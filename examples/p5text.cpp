// p5.js text: alignment, baselines, and p5's own measurement of a run.
//
// The companion to p5basic. That page proves the bundle paints at all; this
// one proves the text half of it, which is a different pipeline: p5 positions
// every string itself, from metrics it asks the canvas for, and a measurement
// that is merely plausible produces a page that looks fine until something
// has to line up with something else.
//
// The page draws the reference lines it aligns to, so the golden shows whether
// the alignment is right rather than only that text appeared.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "p5.js text";
    options.width = 340;
    options.height = 260;
    return ctbrowser::run_app_file("examples/pages/p5-text.html", options);
}
