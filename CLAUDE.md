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
# builds and passes; svg_basics skips its pixel assertions.
```
**mimalloc backs `operator new`/`delete`** and is REQUIRED by default — `brew
install mimalloc` (v3, pinned in `tools/Brewfile`), or
`tools/mingw/build-mimalloc-mingw.sh` for the Windows sysroot. It measured -4.2%
instructions on Linux and **-11.7% wall on the Windows .exe**, because Windows'
CRT allocator is much further behind than glibc's. Opt out with
`-DCTBROWSER_USE_MIMALLOC=OFF`; `tests/core_basics` asks
`ctbrowser::allocator_name()` which allocator is ACTUALLY linked, because a
global `operator new` in a static archive can be silently dropped by link order.

Flags: `-O2 -pedantic -Wall -Wextra -Werror -Wconversion`. Tests are
EXECUTABLES, SDL-free, headless. `tools/format.sh --check` is the formatting
gate - **run it yourself before committing**. There is NO CI: the GitHub
workflow was deleted on 2026-08-08, so nothing checks formatting or runs the
suite unless a person does. `tools/remote-build.sh` is the whole gate now.

## Tooling
- `tools/gen/gen-assets.py` — regenerates `examples/assets/` (sprites.bmp, blip.wav)
  deterministically, so no foreign binary is committed.
- `tools/gen/gen-shaders.py` — GLSL -> the SPIR-V in `include/ctbrowser/gpu/shaders/tile_spv.hpp`.
- `tools/check/compare.py` — drives ctbrowser AND Chrome/Firefox through the same
  clicks and keystrokes, live, so parity can be seen rather than guessed.
  `--headed --delay` makes it watchable; `examples/ctdrive.cpp` is the
  ctbrowser half. See `docs/build.md`.
- `tools/check/check-spirv.py` — runs `spirv-val` over SPIR-V this engine produced,
  given the files. OPTIONAL, and it says plainly when the validator is absent:
  the driver accepting a module proves nothing, which `gpu_basics` measures
  rather than assumes. What it validates today is the TILE shaders
  (`gen-shaders.py`'s output); the WebGL front end it was written for went to
  ANGLE on 2026-08-04 and emits no SPIR-V of its own.
- `tools/check/check-png.py` — decodes a PNG this engine wrote using Python's own
  zlib. `encode_png` uses STORED deflate blocks and no compression library, so
  "the chunk names look right" is not evidence; the CRCs and the Adler-32 are
  silent when wrong.
- `tools/mingw/build-boost-mingw.sh` — compiles Boost.URL for the llvm-mingw target
  into the cross sysroot. Boost.URL is the one COMPILED Boost library the engine
  links (it cannot be header-only), so the Windows presets need this run once.
  See `docs/build.md` for what else was considered and turned down.
- `tools/corpus/phaser-ratchet.py` — the same loop for Phaser 4 that `p5-ratchet.py`
  runs for p5.js: build, measure, `--advance` to record. A SECOND CORPUS, and
  it earned its keep in a day — see `docs/script.md`. No `--bisect`: Phaser
  clears every language rung, so there is nothing to carve.
- `tools/corpus/phaser-api.py` — how WIDE the Phaser surface is, to `p5-api.py`'s
  shape. `--coverage` lists the namespaces no probe mentions, which is the work
  queue. The ratchet read 10/10 while `(5).hasOwnProperty` was undefined,
  because nothing on the ladder asked a number for a property.
- `tools/corpus/babylon-ratchet.py` — the ladder for BABYLON.JS, the third corpus, and
  the second ladder over the same bundle: `webgl2-ratchet.py` asks whether
  Babylon draws AT ALL (10/10) and this asks what a scene can CONTAIN. Reads
  **8/12** — through post-processes; shadows are next. `tools/corpus/babylon-api.py`
  is its width counterpart, 39/43 probes. See `docs/babylon-plan.md`.
- `tools/corpus/module-ratchet.py` — the same loop for ES MODULES. Reads **8/9**: a
  graph links, bindings are LIVE, cycles resolve, module scripts defer like page
  scripts and relative specifiers resolve against the importer, and dynamic
  `import()` resolves to a live namespace object. Rung 9 is Babylon's ES build,
  which is not vendored. `--advance` records. See
  `docs/modules-plan.md`.
- `tools/fetch-angle.sh` — downloads the PINNED ANGLE release into
  `third_party/angle/`. ANGLE is fetched rather than built: it needs GN,
  depot_tools and, on Windows, clang-cl and the Windows SDK. `-DCTBROWSER_WITH_ANGLE=ON`
  then gives `raster/gles.hpp` a real GLES 3.1 device. See `docs/angle-plan.md`.
- `tools/mingw/build-cpptrace-mingw.sh` — cpptrace for the Windows sysroot. TESTS
  ONLY and optional: a missing trace makes a failure harder to read, not wrong.
  It is here because llvm-mingw has no `<stacktrace>` at all, so the platform
  where most of this project's expensive bugs have lived had no trace when a
  test died.
- `tools/mingw/build-mimalloc-mingw.sh` — mimalloc v3 for the Windows sysroot. The
  allocator is not optional in the default build, so the cross build needs this
  run once; `tools/remote-build.sh windows` runs it.
- `tools/mingw/build-gmp-mingw.sh` — GNU GMP for the Windows sysroot, for the
  OPTIONAL BigInt backend (`-DCTBROWSER_WITH_GMP=ON`). **Nothing needs it**:
  BigInt runs on header-only `cpp_int` by default. It is off because GMP is
  LGPL and this engine links statically (see `NOTICE`), and because it is
  SLOWER here — 2.9x on Linux and 5.5x on Windows at 64 bits, the width a
  JavaScript BigInt actually has, and tuning it for a modern CPU does not
  change that. `docs/script.md` has the table. It does cross-compile, assembly
  and all.
- `tools/mingw/build-image-libs-mingw.sh` — its sibling, for zlib, libpng and
  libjpeg-turbo. PNG and JPEG decode in the SDL-FREE engine, so the Windows
  presets need this run once too. Versions are pinned on purpose; see
  `docs/build.md`.
- `tools/format.sh`, `tools/check/check-package.sh`, `tools/check/check-render.cmake`,
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
| `docs/script.md` | the JS compiler, the VM, the standard library — what the language supports and what it rejects by name. **p5.js v2.3.1 runs**: `tests/p5_ratchet.cpp` records how FAR the bundle gets and `tests/p5_api.cpp` how WIDE the working surface is; `tools/corpus/p5-ratchet.py` and `tools/corpus/p5-api.py` drive them. **Phaser 4.2.1 runs too** — a second corpus, 10/10, and the four engine bugs it found that p5 could not |
| `docs/shell.md` | the application API, form controls, editing, input, navigation, resources — anything a page can do |
| `docs/style-layout.md` | the cascade and the `style` attribute; tables, generated content, whitespace collapsing |
| `docs/raster.md` | fonts, glyph rasterisation, the font8x8 fallback, **SVG**, and **WebGL** — note that `glsl.hpp` and `softgl.hpp` were DELETED on 2026-08-04 and WebGL now goes through `raster/gl.hpp` to ANGLE. **p5.js WEBGL mode works**: `examples/pages/p5-webgl.html` draws a cube and a sphere through p5's own shaders, with a golden. `docs/webgl-plan.md` is the design and the staging |
| `docs/webgl-plan.md` | **STALE since 2026-08-04** - the software rasteriser and its GLSL it describes were deleted. Kept for the measurements and the reasoning; read `docs/webgl-rewrite-plan.md` for what exists |
| `docs/webgl-rewrite-plan.md` | **THE WEBGL STACK WAS DELETED AND REWRITTEN ON ANGLE (2026-08-04).** ~8,500 lines went - the GLSL front end, its evaluator, the SPIR-V emitter, the software rasteriser - because a context that mirrored GL state AND hand-forwarded some calls to ANGLE had nineteen methods that silently forwarded nothing. The new `webgl_context` translates and returns, keeping no state GL can be asked for. **Ratchet 9/10, suite 65/73**, one rung left. **Start here for anything WebGL** - `docs/webgl-plan.md`, `docs/webgl2-plan.md` and `docs/gpu-shaders-plan.md` describe code that no longer exists |
| `docs/angle-plan.md` | **ANGLE as the WebGL back end** — stop implementing GLES and call the one Chrome ships. **Stage 0 is DONE and it passed: 191 M frag/s against the interpreter's 1.03 M, a 186x, on SwiftShader with no GPU at all** — and the readback that was supposed to eat the win costs 8%. ~5,700 lines would go and the 1,264-line JavaScript surface stays. **Stage 0 is DONE on BOTH platforms**: 192 M frag/s on Linux, 332 M on Windows, against the interpreter's 1.03 M — and both render the IDENTICAL pixel, so the byte-compared goldens can survive. Built ANGLE is published at `alexios-angel/angle`, release `ctbrowser-angle-25c80ccab4`, five files per platform |
| `docs/gpu-shaders-plan.md` | **running page shaders on the GPU with libshaderc, instead of interpreting them.** The interpreter is **1.03 M frag/s** — one full-screen pass at 420x300 costs 120 ms — and that is a ceiling no tuning lifts. The translation glslang needs is three mechanical rewrites whose numbers this engine already assigns at link time; the shader compiler is the SMALL part; and the byte-compared goldens mean a GPU path must be opt-in with software as the oracle |
| `docs/webgl2-plan.md` | WebGL 2: the SUBSET p5.js actually uses, scoped by measurement, what refuses by name, and why the p5-webgl golden moving would mean the new path is wrong. **Babylon.js renders a scene (ratchet 10/10)** - which took uniform BUFFER objects, moved into scope after "out of scope" turned out to mean every matrix reads zero while nothing errors |
| `docs/performance.md` | **where the time actually goes, measured** — how to profile on WSL2 (callgrind, because `perf` cannot work), what landed, and the three confident hypotheses that measured wrong. **Read before optimising anything.** |
| `docs/modules-plan.md` | **ES modules — running ordinary JavaScript with nothing shimmed.** `import`/`export` are not even keywords in ctjs today, and `run_scripts` concatenates every `<script>` into ONE program, which is what modules cannot be. Babylon's UMD build rejects its own dynamic `import()`, which is what blocks webgl2 rung 10 |
| `docs/babylon-plan.md` | **Babylon.js, from "renders a box" to functional** — what a scene can contain, measured one feature at a time rather than listed. Textures sample BLACK, a post-process blanks the canvas, `wireframe` draws nothing, `PBRMaterial` throws on one missing string method, and the GUI is a separate bundle. The twelve-rung ladder, the surface probe and the example page |
| `docs/ada-url-plan.md` | **this engine parses URLs by the wrong standard** — `shell/url.cpp` is RFC 3986 (Boost.URL) where browsers are WHATWG. Measured against ada: 8 of 15 cases differ, including backslash URLs losing the host and IDNA hosts never resolving. Read before touching `shell/url` |
| `docs/computed-goto-plan.md` | replacing the VM's `switch` dispatch with computed gotos: the macro layer that falls back to `switch` off GNU, the pragmas that make it survive `-pedantic -Werror` on clang AND gcc, and **the measurement that will probably cancel it** — the whole interpreter is 1.4% of a page render |
| `docs/lexer-plan.md` | the JS lexer this engine is writing to replace ctjs's, why, and how it is verified. **Read before touching `script/lexer`** |
| `docs/build.md` | why the build takes as long as it does, the formatting gate, the runtime profiler |
| `docs/platform.md` | **a Linux binary here sees only lavapipe** (CPU Vulkan) — real hardware needs the Windows `.exe`, which gets an Intel Arc. The cross-build and the devbox. Read before drawing conclusions from a Linux run. |
| `docs/v1-retirement.md` | what the deleted compile-time engine had that this does not |
