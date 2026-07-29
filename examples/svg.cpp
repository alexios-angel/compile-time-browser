// The SVG gallery: shapes, gradients, groups and clipping, both inline in the
// markup and loaded through <img>. No script - like the element gallery, this
// example is about what the engine DRAWS.
//
// The first row is the one to look at, and it is the reason the feature was
// built this way: one graphic at 48, 96 and 192 pixels, each rasterised for the
// box it actually got. An engine that decodes a vector graphic once at its
// natural size and lets the image scaler stretch it shows a visibly
// stair-stepped 192, which is worse than a PNG would have looked.
//
// It is also the honest list of what is missing, because the page renders it.
// plutosvg 0.0.8 draws no <text> at all, and SILENTLY IGNORES clip-path, mask,
// filters, patterns and markers - measured, not assumed: `clip-path` on a shape
// and on a group both leave the shape unclipped. `opacity` and `transform` do
// work. The page shows the unsupported ones side by side with a plain shape so
// the gap is visible rather than documented somewhere nobody reads.
//
// Without plutosvg the page still lays out identically - the natural-size scan
// is in-engine - and simply draws no graphics.

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "svg gallery";
    options.width = 900;
    options.height = 700;
    return ctbrowser::run_app_file("examples/pages/svg.html", options);
}
