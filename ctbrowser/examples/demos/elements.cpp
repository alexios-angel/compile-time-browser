//===- ctbrowser/examples/demos/elements.cpp - Rendering gallery demo -----===//
//
// The rendering gallery: headings, wrapping prose, inline flow, three font
// families, a page @font-face, lists, blockquote, pre, a rule, a link, tables
// and <details>. No script: this example is about what the engine draws.
//
// It is the conformance page, so it also shows what the engine does not do yet.
// Fonts are real: the vendored Tinos/Fira Sans/Cousine faces back
// serif/sans-serif/monospace, weights and styles resolve per element,
// underlines and strikethroughs are drawn, and the page's own @font-face file
// loads. Tables still lay out as ordinary boxes rather than grids, and list
// markers and disclosure triangles are not drawn.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

int main() {
    ctbrowser::app_options options;
    options.title = "element gallery";
    options.width = 900;
    options.height = 700;
    // The page is taller than the window on purpose: scroll it with the wheel,
    // PageUp/PageDown, Home and End.
    options.confine_assets = false; // Trusted in-tree page; it loads from examples/assets/.
    return ctbrowser::run_app_file("examples/pages/elements.html", options);
}
