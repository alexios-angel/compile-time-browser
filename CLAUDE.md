# CLAUDE.md — ctbrowser

A browser engine in C++23. `include/ctbrowser.hpp` is the one-include public
API, `include/ctbrowser/` the engine's headers and `src/` its implementations;
`tests/` is the suite, `examples/` the programs that use it. Namespace
`ctbrowser`. **CMake + Ninja is THE build**, CMake >=
3.20, an ordinary clang or gcc with C++23 - the system default will do. Work on
`main`. Prefer `rg`.

It was C++20 named modules until 2026-07-28. If you find `import ctbrowser;`
or a `.cppm` anywhere outside git history, it is stale.

The compile-time engine this repository is named for is GONE from the tree
(2026-07-27) and lives in the git history: the page was a structural NTTP and
the parsers ran in constant evaluation. What that cost and what it left behind
is in `docs/v1-retirement.md`. Two bricks remain as submodules doing runtime
work - ctcss parses CSS, ctjs parses script. cthtml does not, and is no longer
a submodule at all: the DOM has its own WHATWG tokenizer and tree builder, and
`include/ctbrowser/dom/entities.hpp` is the entity table carried forward from it.

## Build & test
```bash
git submodule update --init --recursive    # ctcss + ctjs (+ nested ctc)
cmake --preset default && cmake --build --preset default && ctest --preset default
cmake --preset tsan && ctest --preset tsan     # and asan
# examples build when SDL3 is found; tests are always headless
```
Flags: `-O2 -pedantic -Wall -Wextra -Werror -Wconversion`. Tests are
EXECUTABLES, SDL-free, headless. `tools/format.sh --check` is the formatting
gate and CI runs it.

## Tooling
- `tools/gen-assets.py` — regenerates `examples/assets/` (sprites.bmp, blip.wav)
  deterministically, so no foreign binary is committed.
- `tools/gen-shaders.py` — GLSL -> the SPIR-V in `include/ctbrowser/gpu/shaders/tile_spv.hpp`.
- `tools/format.sh`, `tools/check-package.sh`, `tools/check-render.cmake`,
  `tools/remote-build.sh`.

## Invariants — the things that are easy to break

- **The engine is SDL-FREE.** `shell/app.hpp` and `src/shell/app.cpp` are the
  only places that know SDL exists, and SDL3 is optional at build time.
  `tests/api_surface` lints both halves: an application source must contain
  exactly one engine include - the umbrella header - and no SDL symbol, and the
  rest of `include/` and `src/` must stay clean. That test carries an explicit
  allow-list for the files that may include SDL.
- **No third-party header in a public header.** Boost/SDL/FreeType includes
  belong in a `.cpp`: every consumer parses what a header includes, and
  `<windows.h>` or `<boost/asio.hpp>` in one is a cost paid by everyone who
  touches the engine. `core/cpu_time.hpp` is the pattern - it declares one
  function and its `.cpp` owns the platform headers. See `docs/build.md`.
- **`Math.random` is seeded and DETERMINISTIC** by default. Two example pages
  byte-compare their render against `tests/golden/*.ppm`; a page drawing with
  random cannot have a golden otherwise. `REGOLDEN=1` regenerates.
- **Goldens are test data, not build output.** Render output goes to
  `build*/render-*.ppm`. The ignore files are per-directory and the root's
  patterns are anchored (`/*.ppm`), so nothing reaches into `tests/golden/`.
- **The build asks nothing unusual of the compiler.** No modules, so no import
  graph to report, no BMI, no CMake 3.28 floor, and no search for a specific
  clang before `project()`. `CXX=` still overrides.
- **`.clang-format` is LLVM with measured deviations** — spaces four wide (not
  LLVM's two), 100 columns, `const rect & box`, one-line `if (x) { return; }`,
  unindented namespaces. They are what the code already was; do not "fix" them
  toward stock LLVM. Generated and vendored files are in `.clang-format-ignore`.
- **A frame runs only what changed** — a scroll re-composites, a canvas draw
  re-rasters without re-recording, an idle page blocks on the event queue.
  Reaching for a full relayout is almost always the wrong fix.

## The tree

```
include/ctbrowser.hpp      the one-include API: includes all of it
include/ctbrowser/<sub>/   the engine's headers, one directory per subsystem
src/<sub>/                 its implementations, where a subsystem has any

core         slab, generation-tagged handles, epochs, atoms, thread pool, geometry
dom          WHATWG tokenizer + tree builder, the document
style        selector matching, the cascade, computed values, UA sheet
layout       block, inline and table formatting contexts -> placed geometry
paint        the display list, in layers
raster       tiles across the pool; software always, SDL3_ttf for real fonts
gpu          SDL_GPUDevice composition, and the fallback when there is none
script       JS -> bytecode -> register VM over NaN-boxed values, + stdlib
shell        the assembly: browser, page bindings, forms, canvas, input, net
tests/       one executable per file; golden/ is test data
examples/    counter, pong, invaders, widgets, elements, fetchboard, ctbrowse
external/    ctcss + ctjs submodules (runtime parsing)
```

Every subsystem is one directory, one aggregate header, one CMake target.
`docs/architecture.md` has the full map and where to start reading in each.

## Where to read next

Read the one that matches what you are touching — not all of them.

| | |
|---|---|
| `docs/architecture.md` | where everything lives, and how to add a file to a subsystem. **Start here if you do not know where something lives.** |
| `docs/script.md` | the JS compiler, the VM, the standard library — what the language supports and what it rejects by name |
| `docs/shell.md` | the application API, form controls, editing, input, navigation, resources — anything a page can do |
| `docs/style-layout.md` | the cascade and the `style` attribute; tables, generated content, whitespace collapsing |
| `docs/raster.md` | fonts, glyph rasterisation, the font8x8 fallback |
| `docs/build.md` | why the build takes as long as it does, the formatting gate, the runtime profiler |
| `docs/platform.md` | **the GPU here reports no adapter** (WSL2), the Windows cross-build, the devbox. Read before drawing conclusions from a Linux run. |
| `docs/v1-retirement.md` | what the deleted compile-time engine had that this does not |
