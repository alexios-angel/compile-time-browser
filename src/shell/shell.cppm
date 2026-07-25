export module ctbrowser.shell;

// The shell: the engine assembled into something that loads a page.
//
//   input    events described without SDL, so the whole browser is drivable
//            headlessly
//   browser  the assembly - parse, style, layout, paint, raster, composite -
//            and, more importantly, what each kind of change lets a frame SKIP
//
// The SDL3 window and event loop are in ctbrowser.app, which is optional: the
// browser here needs no display, which is what lets tests render whole pages
// and compare them byte for byte.

export import :input;
export import :browser;
