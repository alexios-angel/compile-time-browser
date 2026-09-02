# The tools

Everything in `tools/`, by job. `format.sh`, `remote-build.sh` and
`fetch-angle.sh` stay at the top level because they are the everyday entry
points; the rest are foldered:

    tools/mingw/    build-*-mingw.sh, the Windows cross sysroot
    tools/gen/      generators that write into the tree
    tools/corpus/   the ratchets and API probes for the vendored libraries
    tools/check/    verification: package, PNG, SPIR-V, render, browser parity

**`tools/remote-build.sh` is the whole verification gate.** There is no CI - the
GitHub workflow was deleted on 2026-08-08 - so nothing checks formatting or runs
the suite unless a person does.

Each of these scripts finds the repository by counting directories up from its
own file, so a script that moves between these folders needs that count changed
with it.

- `tools/gen/gen-assets.py` — regenerates `examples/assets/` (sprites.bmp, blip.wav)
  deterministically, so no foreign binary is committed.
- `tools/gen/gen-shaders.py` — GLSL -> the SPIR-V in `include/ctbrowser/gpu/shaders/tile_spv.hpp`.
- `tools/check/compare.py` — drives ctbrowser AND Chrome/Firefox through the same
  clicks and keystrokes, live, so parity can be seen rather than guessed.
  `--headed --delay` makes it watchable; `ctbrowser/tools/ctdrive/ctdrive.cpp` is the
  ctbrowser half. See `docs/build.md`.
- `tools/check/check-spirv.py` — runs `spirv-val` over SPIR-V this engine produced,
  given the files. OPTIONAL, and it says plainly when the validator is absent:
  the driver accepting a module proves nothing, which `gpu_basics` measures
  rather than assumes. What it validates today is the TILE shaders
  (`gen-shaders.py`'s output); the WebGL front end it was written for went to
  ANGLE on 2026-08-04 and emits no SPIR-V of its own.
- `tools/check/css-parity.py` — **how far a Bootstrap page is from Chrome, per
  element and per property.** Drives both engines through `compare.py`'s daemon,
  runs `css-dump.js` in each, normalises both sides identically and ranks the
  differences so the biggest CAUSE surfaces first rather than its consequences.
  Two ratcheted numbers per fixture in `css-parity.txt`: `differ` falls as layout
  gets right, `substituted` falls as properties get modelled — the second is what
  stops "no difference" being mistaken for "implemented". `--advance` is the only
  writer. NOT a ctest, deliberately; see `docs/plans/bootstrap.md`.
- `tools/check/css-dump.js` — the dump both engines run, prepended with `PROPS`
  by the above. Walks `documentElement.children` and never a complex selector,
  because ctbrowser's selector engine is the thing under test.
- `tools/check/ctdrive-reply.py` — does `ctdrive` return a large `eval` reply
  intact? Two cases, and the second is the only one that can fail: a client that
  reads continuously keeps the kernel buffer draining, so an unlooped `send()`
  gets 262 KB out in one call and looks correct, while a client that sleeps
  before reading lost 4 MB down to 2,588,672 bytes and no newline. Keep both —
  `stall` alone is a test with no explanation, `drain` alone cannot fail.
- `tools/check/check-png.py` — decodes a PNG this engine wrote using Python's own
  zlib. `encode_png` uses STORED deflate blocks and no compression library, so
  "the chunk names look right" is not evidence; the CRCs and the Adler-32 are
  silent when wrong.
- `tools/check/type-oracle.py` — **the type oracle's checker**, ctcompile Phase
  54B. `--record-types` on the interpreter writes down every type each
  `(function, register)` actually held while a corpus ran; this compares a
  static type claim against that recording and reports SOUNDNESS (did the claim
  ever say something NARROWER than reality - a defect, named by function and
  register) and PRECISION (how often it beat "boxed" - a backlog item)
  separately, plus a third number that is neither: how many registers no
  execution ever reached. **`--infer all-i32` and `--infer all-boxed` are
  deliberately wrong and deliberately trivial stubs**, and running them is what
  proves the tool measures anything - a checker that has never caught something
  is not known to work. `ctcompile-test-type-oracle` produces the recordings and
  is a SECOND implementation of the same check, compared against this one by
  `ctcompile/test/check-type-oracle.cmake`.
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
  **10/12** — a PBR material renders; glTF import and the GUI are next. `tools/corpus/babylon-api.py`
  is its width counterpart, 39/43 probes. See `docs/plans/babylon.md`.
- `tools/corpus/module-ratchet.py` — the same loop for ES MODULES. Reads **8/9**: a
  graph links, bindings are LIVE, cycles resolve, module scripts defer like page
  scripts and relative specifiers resolve against the importer, and dynamic
  `import()` resolves to a live namespace object. Rung 9 is Babylon's ES build,
  which is not vendored. `--advance` records. See
  `docs/plans/modules.md`.
- `tools/fetch-angle.sh` — downloads the PINNED ANGLE release into
  `third-party/angle/`. ANGLE is fetched rather than built: it needs GN,
  depot_tools and, on Windows, clang-cl and the Windows SDK. `-DCTBROWSER_WITH_ANGLE=ON`
  then gives `raster/gles.hpp` a real GLES 3.1 device. See `docs/plans/angle.md`.
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
- `tools/fetch-test262.sh` — shallow-fetches the OFFICIAL ECMAScript conformance
  suite at a PINNED commit into `~/.cache/ctbrowser/test262` and verifies the
  hash. The corpus is 53,580 files and is NEVER vendored; this is the one place
  the pin lives. See `docs/test262.md`.
- `tools/check/test262.py` — runs test262 against the engine through
  `ctbrowser/tools/ct262`, ONE DIRECTORY AT A TIME (`--dir test/language/statements/for-of`
  prints a table), 4 workers, a 10 s timeout and a 2 GB address-space cap per
  test. `--self-test` plants eleven answers it must classify correctly and is
  registered as a ctest, because a harness that says PASS to everything looks
  exactly like a conforming engine; `--gate` is the regression subset, which
  fails on an unexpected PASS as well as on a regression. Opt in with
  `-DCTBROWSER_TEST262=ON`.
- `tools/check/test262-baseline.sh` — the ten areas `docs/test262.md` records a
  number for, run one after another with identical flags. Sequential: four
  workers is the cap the whole devbox shares, and two of these at once is eight.
- `tools/wpt/fetch-wpt.sh`, `tools/wpt/run-wpt.py` — **web-platform-tests**, the
  standards suite every browser is measured against, run against this engine one
  directory at a time. The corpus is fetched sparse and shallow at a pinned
  commit to `~/.cache/wpt` and is never vendored; `run-wpt.py --dir dom/nodes`
  prints a table and `--selftest` proves the harness reports a failure as a
  failure before any of it is believed. `tools/wpt/expectations.txt` is the
  known-failure file and the gate fails on an unexpected PASS as well as on a
  regression. See **`docs/wpt.md`**.
- `tools/format.sh`, `tools/check/check-package.sh`, `tools/check/check-render.cmake`,
  `tools/remote-build.sh`.
