//===- ctbrowser/examples/demos/svg.cpp - SVG gallery demo ---- -*- C++ -*-===//
//
// The SVG gallery: shapes, gradients, groups and clipping, both inline in the
// markup and loaded through <img>. No script: like the element gallery, this
// example is about what the engine draws.
//
// The first row draws one graphic at 48, 96 and 192 pixels, each rasterised at
// the size its box got. Decoding once at natural size and scaling the bitmap
// would make the 192 version visibly stair-stepped.
//
// The page also shows what is missing. plutosvg 0.0.8 draws no <text> and
// silently ignores clip-path, mask, filters, patterns and markers (checked:
// `clip-path` on a shape and on a group both leave it unclipped); `opacity` and
// `transform` work. The unsupported features are shown beside a plain shape so
// the gap is visible.
//
// Without plutosvg the page still lays out identically (the natural-size scan
// is in the engine) and draws no graphics.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "svg gallery";
    options.width = 900;
    options.height = 700;
    options.confine_assets = false; // Trusted in-tree page; it loads from examples/assets/.
    return ctbrowser::run_app_file("examples/pages/svg.html", options);
}
