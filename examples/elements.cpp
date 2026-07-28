// The rendering gallery: headings, wrapping prose, inline flow, three font
// families, a page @font-face, lists, blockquote, pre, a rule, a link, tables
// and <details>. No script at all - this example is about what the engine draws.
//
// It is the conformance page, so it is also the honest list of what the engine does not
// do yet. As of stage 6 the FONTS are real: the vendored Tinos/Fira Sans/Cousine
// faces back serif/sans-serif/monospace, weights and styles resolve per element,
// underlines and strikethroughs are drawn, and the page's own @font-face file
// loads. TABLES still lay out as ordinary boxes rather than as grids, and list
// markers and disclosure triangles are not drawn - both are stage 7.

import ctbrowser;

int main() {
	ctbrowser::app_options options;
	options.title = "element gallery";
	options.width = 900;
	options.height = 700;
	// The page is taller than the window on purpose: scroll it with the wheel,
	// PageUp/PageDown, Home and End.
	return ctbrowser::run_app_file("examples/pages/elements.html", options);
}
