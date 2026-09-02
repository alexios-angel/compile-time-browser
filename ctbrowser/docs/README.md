# ctbrowser documentation

Read the one that matches what you are touching, not all of them.

**Start with [`architecture.md`](architecture.md) if you do not know where
something lives.**

## Reference — how the engine works today

| | |
|---|---|
| [`architecture.md`](architecture.md) | where everything lives, the ten subsystems, and how to add a file to one |
| [`build.md`](build.md) | why the build takes as long as it does, the formatting gate, the runtime profiler |
| [`platform.md`](platform.md) | **a Linux binary here sees only lavapipe** — real hardware needs the Windows `.exe`. The cross-build and the devbox. Read before drawing conclusions from a Linux run |
| [`performance.md`](performance.md) | **where the time actually goes, measured** — how to profile on WSL2, what landed, and the three confident hypotheses that measured wrong. **Read before optimising anything** |
| [`script.md`](script.md) | the JS compiler, the VM, the standard library — what the language supports and what it rejects by name |
| [`test262.md`](test262.md) | **the official ECMAScript conformance suite, and what it measures**: the harness decision, the pinned corpus, `$262`, the negative proofs, and the baseline per area with its date |
| [`shell.md`](shell.md) | the application API, form controls, editing, input, navigation, resources — anything a page can do |
| [`style-layout.md`](style-layout.md) | the cascade and the `style` attribute; tables, generated content, whitespace collapsing |
| [`raster.md`](raster.md) | fonts, glyph rasterisation, the font8x8 fallback, SVG, PNG/JPEG. **Its last section is marked as describing deleted code** |
| [`wpt.md`](wpt.md) | **web-platform-tests — the suite every browser is measured against, pointed at this one.** The pinned corpus, the results hook, the per-directory baseline with its date, the expectations file, and the five gaps WPT found in the engine on its way in |

## Plans — work that is not finished

| | |
|---|---|
| [`plans/bootstrap.md`](plans/bootstrap.md) | **Bootstrap 5.3.8 at Chrome parity, and the CSS front end rewritten to get there.** The harness measures (S0 done); 7,820 of 30,288 compared values differ today. **Start here for anything CSS** |
| [`plans/webgl-rewrite.md`](plans/webgl-rewrite.md) | **the WebGL stack rebuilt on ANGLE.** Ratchet 9/10, one rung left. **Start here for anything WebGL** |
| [`plans/angle.md`](plans/angle.md) | **ANGLE as the WebGL back end** — stop implementing GLES and call the one Chrome ships. Stage 0 is DONE on both platforms: 192 M frag/s on Linux, 332 M on Windows, against the interpreter's 1.03 M, and both render the identical pixel |
| [`plans/babylon.md`](plans/babylon.md) | **Babylon.js, from "renders a box" to functional** — the twelve-rung ladder, measured one feature at a time. Reads 10/12 |
| [`plans/modules.md`](plans/modules.md) | **ES modules — running ordinary JavaScript with nothing shimmed.** Reads 8/9; rung 9 is Babylon's ES build, which is not vendored |
| [`plans/ada-url.md`](plans/ada-url.md) | **this engine parses URLs by the wrong standard** — `shell/net/url.cpp` is RFC 3986 where browsers are WHATWG. 8 of 15 measured cases differ. Read before touching `shell/net/url` |

## History — done, superseded, or deliberately stale

Kept for the reasoning and the numbers. Several describe code that no longer
exists and say so in a banner at the top; **do not "fix" their paths.** The
measurements are what makes them worth keeping, and they are the main defence
against redoing work that was already tried and rejected.

| | |
|---|---|
| [`history/webgl.md`](history/webgl.md) | the original one-front-end/two-back-ends design. **Describes ~8,500 deleted lines.** Kept for the interpreter's measured ceiling and the libshaderc answer |
| [`history/webgl2.md`](history/webgl2.md) | WebGL 2: the subset p5.js actually uses, scoped by measurement. Stages 0–5 done |
| [`history/gpu-shaders.md`](history/gpu-shaders.md) | running page shaders on the GPU with libshaderc. **Superseded by ANGLE**; kept for why patching glslang was the real cost |
| [`history/computed-goto.md`](history/computed-goto.md) | computed-goto dispatch for the VM. DONE 2026-08-02, behind a flag that is OFF — the measurement nearly cancelled it |
| [`history/lexer.md`](history/lexer.md) | the JS lexer. **Stage 0 ran and cancelled stages 1–5**, having already got the 1.95x |
| [`history/phaser.md`](history/phaser.md) | Phaser as a second corpus. DONE, 10/10 — kept because the reasoning about *why a second corpus* is the part that was right |
| [`history/v1-retirement.md`](history/v1-retirement.md) | what the deleted compile-time engine had that this does not |

## Not in this directory

- [`../../CLAUDE.md`](../../CLAUDE.md) — the invariants, and how to build and test.
  The things that are easy to break.
- [`../vendor/README.md`](../vendor/README.md) — the three JavaScript corpora,
  what they are for, and how to update one.
- [`../../NOTICE`](../../NOTICE) — attributions, and what an install of this package
  actually contains.
