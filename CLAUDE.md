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
is in `docs/history/v1-retirement.md`. Two bricks remain as submodules doing runtime
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
`-DCTBROWSER_USE_MIMALLOC=OFF`; `tests/unit/core_basics` asks
`ctbrowser::allocator_name()` which allocator is ACTUALLY linked, because a
global `operator new` in a static archive can be silently dropped by link order.

Flags: `-O2 -pedantic -Wall -Wextra -Werror -Wconversion`. Tests are
EXECUTABLES, SDL-free, headless. `tools/format.sh --check` is the formatting
gate - **run it yourself before committing**. There is NO CI: the GitHub
workflow was deleted on 2026-08-08, so nothing checks formatting or runs the
suite unless a person does. `tools/remote-build.sh` is the whole gate now.

## Tooling

Every script in `tools/`, what it is for and which of them are
load-bearing: **`docs/tools.md`**.

## Invariants — the things that are easy to break

- **The engine is SDL-FREE.** `app/app.hpp` and `src/app/app.cpp` are the
  only places that know SDL exists, and SDL3 is optional at build time.
  `tests/lint/api_surface` lints both halves: an application source must contain
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
  than a PNG. `shell/page/svg_cache.hpp` caches by `(content, width, height)` and the
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
include/ctbrowser.hpp      the one-include public API
include/ctbrowser/<sub>/   the engine's headers, one directory per subsystem
src/<sub>/                 its implementations, mirroring that layout
tests/                     support/ unit/ js/ corpus/ stress/ bench/ lint/,
                           plus golden/ which is test DATA, not output
examples/                  demos/ corpus/ cli/, plus pages/ and assets/
vendor/                    p5.js, Phaser, Babylon.js - the test corpora
external/                  ctcss + ctjs submodules (runtime parsing)
tools/                     mingw/ gen/ corpus/ check/ - see docs/tools.md
```

Ten subsystems: core, dom, style, layout, paint, raster, gpu, script, shell,
app. Every one is one directory, one aggregate header, one CMake target and one
`src/<sub>/CMakeLists.txt`. **`docs/architecture.md` is the full map** - what
each owns, where to start reading in it, and which three have subdirectories.

## Where to read next

**`docs/README.md` is the index.** It is grouped: reference for how the engine
works today, `docs/plans/` for work that is not finished, and `docs/history/`
for what was done, superseded, or is deliberately stale.

Several files in `docs/history/` describe code that no longer exists and carry a
banner saying so. **Do not "fix" their paths** - the measurements in them are
the point, and are what stops the same thing being attempted twice.
