# CLAUDE.md — ctbrowser

A browser engine in C++23 named modules. `src/` is the engine, `tests/` the
suite, `examples/` the programs that use it. Namespace `ctbrowser`.
**CMake + Ninja is THE build**, CMake >= 3.28 for modules; an ordinary clang or
gcc with C++23. Work on `main`. Prefer `rg`.

The compile-time engine this repository is named for is GONE from the tree
(2026-07-27) and lives in the git history: the page was a structural NTTP and
the parsers ran in constant evaluation. What that cost and what it left behind
is in `docs/v1-retirement.md`. Two bricks remain as submodules doing runtime
work - ctcss parses CSS, ctjs parses script. cthtml does not, and is no longer
a submodule at all: the DOM has its own WHATWG tokenizer and tree builder, and
`src/dom/entities.hpp` is the entity table carried forward from it.

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
- `tools/gen-shaders.py` — GLSL -> the SPIR-V in `src/gpu/shaders/tile_spv.hpp`.
- `tools/format.sh`, `tools/check-package.sh`, `tools/check-render.cmake`,
  `tools/remote-build.sh`.

## Invariants — the things that are easy to break

- **The engine is SDL-FREE.** `ctbrowser.app` is the only module that knows SDL
  exists, and SDL3 is optional at build time. `tests/api_surface` lints both
  halves: an application source must contain exactly one `import ctbrowser;`
  and no SDL symbol, and the engine modules must stay clean. There is an
  explicit allow-list in that test for the four files that may include SDL.
- **No third-party header in a `.cppm` interface's global module fragment.** It
  is serialized into the BMI: `<boost/asio.hpp>` in `:net` made that BMI 27 MB.
  Boost/SDL/FreeType includes belong in a `.cpp`. See `docs/build.md`.
- **`Math.random` is seeded and DETERMINISTIC** by default. Two example pages
  byte-compare their render against `tests/golden/*.ppm`; a page drawing with
  random cannot have a golden otherwise. `REGOLDEN=1` regenerates.
- **Goldens are test data, not build output.** Render output goes to
  `build*/render-*.ppm`. The ignore files are per-directory and the root's
  patterns are anchored (`/*.ppm`), so nothing reaches into `tests/golden/`.
- **The compiler must report its import graph**, or CMake cannot build modules
  — and it says so in terms mentioning neither modules nor a version, so the
  root `CMakeLists.txt` searches for a new-enough clang *before* `project()`
  locks the toolchain. `CXX=` or `-DCMAKE_CXX_COMPILER=` overrides that.
- **`.clang-format` is LLVM with measured deviations** — spaces four wide (not
  LLVM's two), 100 columns, `const rect & box`, one-line `if (x) { return; }`,
  unindented namespaces. They are what the code already was; do not "fix" them
  toward stock LLVM. Generated and vendored files are in `.clang-format-ignore`.
- **A frame runs only what changed** — a scroll re-composites, a canvas draw
  re-rasters without re-recording, an idle page blocks on the event queue.
  Reaching for a full relayout is almost always the wrong fix.

## The tree

```
src/core     slab, generation-tagged handles, epochs, atoms, thread pool, geometry
src/dom      WHATWG tokenizer + tree builder, the document
src/style    selector matching, the cascade, computed values, UA sheet
src/layout   block, inline and table formatting contexts -> placed geometry
src/paint    the display list, in layers
src/raster   tiles across the pool; software always, SDL3_ttf for real fonts
src/gpu      SDL_GPUDevice composition, and the fallback when there is none
src/script   JS -> bytecode -> register VM over NaN-boxed values, + stdlib
src/shell    the assembly: browser, page bindings, forms, canvas, input, net
src/ctbrowser.cppm   re-exports all of it, which is why one import is the API
tests/       one executable per file; golden/ is test data
examples/    counter, pong, invaders, widgets, elements, fetchboard, ctbrowse
external/    ctcss + ctjs submodules (runtime parsing)
```

Every subsystem is one module with partitions, one CMake target, one directory.
`docs/architecture.md` has the full map and where to start reading in each.

## Where to read next

Read the one that matches what you are touching — not all of them.

| | |
|---|---|
| `docs/architecture.md` | the module graph, and how to add a file to a subsystem. **Start here if you do not know where something lives.** |
| `docs/script.md` | the JS compiler, the VM, the standard library — what the language supports and what it rejects by name |
| `docs/shell.md` | the application API, form controls, editing, input, navigation, resources — anything a page can do |
| `docs/style-layout.md` | the cascade and the `style` attribute; tables, generated content, whitespace collapsing |
| `docs/raster.md` | fonts, glyph rasterisation, the font8x8 fallback |
| `docs/build.md` | why the build takes as long as it does, the formatting gate, the runtime profiler |
| `docs/platform.md` | **the GPU here reports no adapter** (WSL2), the Windows cross-build, the devbox. Read before drawing conclusions from a Linux run. |
| `docs/v1-retirement.md` | what the deleted compile-time engine had that this does not |
