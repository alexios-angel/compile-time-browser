export module ctbrowser.core;

// The foundation every other subsystem sits on. Nothing here knows what a
// document, a style or a pixel is.
//
//   handle     generation-tagged references, so a stale reference FAILS a
//              lookup instead of resolving to a recycled object
//   slab       chunked storage with lock-free reads behind those handles
//   epoch      the reclamation scheme that lets those reads take no locks
//   atom       interned strings, so name comparison is an integer compare
//   scheduler  work-stealing pool for style, layout and raster
//   geometry   points, rects, sides, colors
//
// Modules note: this stack uses the global-module-fragment form
// (`module;` + #include + `export module`) rather than `import std;`.
// libstdc++ 13 ships no std module, so `import std;` does not compile with
// this toolchain - the .cppm files inherited from v1's bricks all use it and
// none of them currently build. Revisit when libstdc++ catches up.

export import :handle;
export import :epoch;
export import :slab;
export import :atom;
export import :scheduler;
export import :geometry;
