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
**BUILD ON THE DEVBOX**, not here: `tools/remote-build.sh` syncs and builds on
the shared box (`../infra/azure-build-server/server.sh start`, then `allow-ip`
when the home IP has rotated). This WSL instance has ~7.5 GiB and a full build —
the Windows cross-build especially — takes it down. The devbox is also a
SECOND set of assumptions: GCC 13 rather than clang, and no SDL at all, which
found four real defects the first time it ran. See `docs/build.md`.

```bash
git submodule update --init --recursive    # ctcss + ctjs (+ nested ctc)
cmake --preset default && cmake --build --preset default && ctest --preset default
cmake --preset tsan && ctest --preset tsan     # and asan
# examples build when SDL3 is found; tests are always headless
# SVG needs plutosvg: `brew bundle --file tools/Brewfile` (PINNED versions -
# the golden compares across Linux and Windows). Without it everything still
# builds and passes; svg_basics skips its pixel assertions, as CI does.
```
Flags: `-O2 -pedantic -Wall -Wextra -Werror -Wconversion`. Tests are
EXECUTABLES, SDL-free, headless. `tools/format.sh --check` is the formatting
gate and CI runs it.

## Tooling
- `tools/gen-assets.py` — regenerates `examples/assets/` (sprites.bmp, blip.wav)
  deterministically, so no foreign binary is committed.
- `tools/gen-shaders.py` — GLSL -> the SPIR-V in `include/ctbrowser/gpu/shaders/tile_spv.hpp`.
- `tools/compare.py` — drives ctbrowser AND Chrome/Firefox through the same
  clicks and keystrokes, live, so parity can be seen rather than guessed.
  `--headed --delay` makes it watchable; `examples/ctdrive.cpp` is the
  ctbrowser half. See `docs/build.md`.
- `tools/gen-glsl-fixtures.py` — extracts the sixteen shaders p5.js ships into
  `tests/glsl/`, plus the preamble it prepends. They are the GLSL front end's
  parse corpus: somebody else's shaders, which is the only kind worth testing a
  parser against.
- `tools/check-spirv.py` — runs `spirv-val` over the SPIR-V the WebGL back end
  emits. OPTIONAL, and it says plainly when the validator is absent: the driver
  accepting a module proves nothing, which `gpu_basics` measures rather than
  assumes.
- `tools/check-png.py` — decodes a PNG this engine wrote using Python's own
  zlib. `encode_png` uses STORED deflate blocks and no compression library, so
  "the chunk names look right" is not evidence; the CRCs and the Adler-32 are
  silent when wrong.
- `tools/build-boost-mingw.sh` — compiles Boost.URL for the llvm-mingw target
  into the cross sysroot. Boost.URL is the one COMPILED Boost library the engine
  links (it cannot be header-only), so the Windows presets need this run once.
  See `docs/build.md` for what else was considered and turned down.
- `tools/phaser-ratchet.py` — the same loop for Phaser 4 that `p5-ratchet.py`
  runs for p5.js: build, measure, `--advance` to record. A SECOND CORPUS, and
  it earned its keep in a day — see `docs/script.md`. No `--bisect`: Phaser
  clears every language rung, so there is nothing to carve.
- `tools/phaser-api.py` — how WIDE the Phaser surface is, to `p5-api.py`'s
  shape. `--coverage` lists the namespaces no probe mentions, which is the work
  queue. The ratchet read 10/10 while `(5).hasOwnProperty` was undefined,
  because nothing on the ladder asked a number for a property.
- `tools/build-image-libs-mingw.sh` — its sibling, for zlib, libpng and
  libjpeg-turbo. PNG and JPEG decode in the SDL-FREE engine, so the Windows
  presets need this run once too. Versions are pinned on purpose; see
  `docs/build.md`.
- `tools/format.sh`, `tools/check-package.sh`, `tools/check-render.cmake`,
  `tools/remote-build.sh`.

## Invariants — the things that are easy to break

- **The engine is SDL-FREE.** `shell/app.hpp` and `src/shell/app.cpp` are the
  only places that know SDL exists, and SDL3 is optional at build time.
  `tests/api_surface` lints both halves: an application source must contain
  exactly one engine include - the umbrella header - and no SDL symbol, and the
  rest of `include/` and `src/` must stay clean. That test carries an explicit
  allow-list for the files that may include SDL.
- **Small shared algorithms live in `core/algorithms.hpp`** — ASCII case
  folding, hex digits, whitespace trimming, base64. Everything there had at
  least three copies before it moved, except `base64_decode`, which went there
  with two on the grounds that `atob` and `data:` URLs decoding base64
  *differently* is the same bug the two URL parsers were. It is **ASCII-only on purpose**: goldens are
  byte-compared across Linux and Windows, so a locale-aware fold would make a
  render depend on `LC_ALL`. The whitespace SET is a parameter, because HTML,
  JavaScript and the GLSL preprocessor genuinely disagree about what whitespace
  is and unifying them would be a bug.
- **Images decode WITHOUT SDL**, all the way down: BMP by hand, PNG through
  libpng, JPEG through libjpeg-turbo, each in one `.cpp` behind a two-function
  header. SDL3_image remains an optional hook for the rest. A format that only
  works when SDL was found is one `tests/` cannot assert on and no golden can
  compare — which is exactly how PNG stayed broken until Phaser arrived.
- **No third-party header in a public header.** Boost/SDL/FreeType includes
  belong in a `.cpp`: every consumer parses what a header includes, and
  `<windows.h>` or `<boost/asio.hpp>` in one is a cost paid by everyone who
  touches the engine. `core/cpu_time.hpp` is the pattern - it declares one
  function and its `.cpp` owns the platform headers. See `docs/build.md`.
- **`Math.random` is seeded and DETERMINISTIC** by default. Three example pages
  (widgets, elements, svg) byte-compare their render against
  `tests/golden/*.ppm`; a page drawing with random cannot have a golden
  otherwise. `REGOLDEN=1` regenerates — then OPEN THE IMAGE AND LOOK AT IT. A
  golden accepted without being seen is how the empty-button and missing-caret
  renders shipped.
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
- **An SVG is rasterised at the size its box got**, never decoded once and
  scaled — `draw_image` is nearest-neighbour, so the scaled version looks worse
  than a PNG. `shell/svg.hpp` caches by `(content, width, height)` and the
  painter passes the *snapped* rect. Optional: with no plutosvg a page lays out
  IDENTICALLY and simply draws no graphics.
- **`<svg>` keeps its capitals.** The tokenizer preserves case inside foreign
  content, so `viewBox` never becomes `viewbox` and the spec's ~95 adjustment
  tables are unnecessary. Separately, every token carries its source span, so
  the RASTERISER gets the author's bytes while the DOM gets a real parsed,
  namespaced subtree. Anything walking the DOM for `<title>`, `<style>` or
  `<script>` must check `element_ns` — SVG has all three and they intern to the
  same atoms.

## The tree

```
include/ctbrowser.hpp      the one-include API: includes all of it
include/ctbrowser/<sub>/   the engine's headers, one directory per subsystem
src/<sub>/                 its implementations, where a subsystem has any

core         slab, generation-tagged handles, epochs, atoms, thread pool, geometry
dom          WHATWG tokenizer + tree builder (incl. SVG foreign content), the document
style        selector matching, the cascade, computed values, UA sheet
layout       block, inline and table formatting contexts -> placed geometry
paint        the display list, in layers
raster       tiles across the pool; software always, SDL3_ttf for real fonts,
             plutosvg for SVG, and GLSL ES (glsl.hpp) for WebGL - see
             docs/webgl-plan.md
gpu          SDL_GPUDevice composition, and the fallback when there is none
script       JS -> bytecode -> register VM over NaN-boxed values, + stdlib
shell        the assembly: browser, page bindings, forms, canvas, input, net
tests/       one executable per file; golden/ is test data
examples/    counter, pong, invaders, widgets, elements, svg, fetchboard, ctbrowse
external/    ctcss + ctjs submodules (runtime parsing)
```

Every subsystem is one directory, one aggregate header, one CMake target.
`docs/architecture.md` has the full map and where to start reading in each.

## Where to read next

Read the one that matches what you are touching — not all of them.

| | |
|---|---|
| `docs/architecture.md` | where everything lives, and how to add a file to a subsystem. **Start here if you do not know where something lives.** |
| `docs/script.md` | the JS compiler, the VM, the standard library — what the language supports and what it rejects by name. **p5.js v2.3.1 runs**: `tests/p5_ratchet.cpp` records how FAR the bundle gets and `tests/p5_api.cpp` how WIDE the working surface is; `tools/p5-ratchet.py` and `tools/p5-api.py` drive them. **Phaser 4.2.1 runs too** — a second corpus, 10/10, and the four engine bugs it found that p5 could not |
| `docs/shell.md` | the application API, form controls, editing, input, navigation, resources — anything a page can do |
| `docs/style-layout.md` | the cascade and the `style` attribute; tables, generated content, whitespace collapsing |
| `docs/raster.md` | fonts, glyph rasterisation, the font8x8 fallback, **SVG**, and **WebGL** — GLSL ES in `glsl.hpp` and the software rasteriser in `softgl.hpp`. **p5.js WEBGL mode works**: `examples/pages/p5-webgl.html` draws a cube and a sphere through p5's own shaders, with a golden. `docs/webgl-plan.md` is the design and the staging |
| `docs/webgl2-plan.md` | WebGL 2: the SUBSET p5.js actually uses, scoped by measurement (four WebGL-2-only calls in its whole bundle, and no VAOs), what refuses by name, and why the p5-webgl golden moving would mean the new path is wrong |
| `docs/performance.md` | **where the time actually goes, measured** — how to profile on WSL2 (callgrind, because `perf` cannot work), what landed, and the three confident hypotheses that measured wrong. **Read before optimising anything.** |
| `docs/computed-goto-plan.md` | replacing the VM's `switch` dispatch with computed gotos: the macro layer that falls back to `switch` off GNU, the pragmas that make it survive `-pedantic -Werror` on clang AND gcc, and **the measurement that will probably cancel it** — the whole interpreter is 1.4% of a page render |
| `docs/lexer-plan.md` | the JS lexer this engine is writing to replace ctjs's, why, and how it is verified. **Read before touching `script/lexer`** |
| `docs/build.md` | why the build takes as long as it does, the formatting gate, the runtime profiler |
| `docs/platform.md` | **a Linux binary here sees only lavapipe** (CPU Vulkan) — real hardware needs the Windows `.exe`, which gets an Intel Arc. The cross-build and the devbox. Read before drawing conclusions from a Linux run. |
| `docs/v1-retirement.md` | what the deleted compile-time engine had that this does not |
