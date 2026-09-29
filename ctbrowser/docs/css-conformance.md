# The CSS suites, measured

`ctbrowser/docs/wpt.md` is the instrument and the whole-corpus baseline. This
file is the **two CSS suites**, in more detail than that table has room for:
what the numbers are, what moved them, and — the part that matters most here —
**which subsystem each remaining failure actually belongs to.**

    tools/wpt/run-wpt.py --dir css/cssom     --jobs 4
    tools/wpt/run-wpt.py --dir css/css-values --jobs 4

Everything below was measured on the devbox against WPT `3f6b09ae`, four
workers, a 4 GB `ulimit -v` per driver, `CTBROWSER_GL_DRIVER=deterministic`.

## Round fifteen CSS recovery — 2026-09-29

The 13 CSS6 commits `d3cdab27b` through `e080a9b49`, on `r15-css6`
at `caa4624b8`, are included in recovery merge `401b2af11` on
`codex-ctbrowser`. Preserved before/final JSON records 25 affected CSS
directories at **1,329 → 1,388 passing files** and **40,024 → 40,182
passing subtests**. Settings match the instrument above. The saved
post-merge build and selected CTests passed **182/182 steps and 31/31 tests**;
fresh combined recovery also passed the explicit 214-step build and all
45 selected CTests (see `wpt.md` for the exact list and commands). The targeted
`css/selectors/focus-visible-028.html` now passes its one subtest with four
workers, 4096 MiB and deterministic GL. The 25-directory table above remains
historical; that selection was not rerun.

The sole passing-file loss is `css/cssom/flex-serialization.html`:
`c5f6b054a` serializes the omitted flex basis as `0%`, matching the cascade,
where that test expects `0px`. CSSStyleDeclaration prototype members and
receiver checks, keyframe variable substitution and layer precedence, scope
roots, selector grammar and CSSOM rule validation account for the recovered
changes. Exact selections, commands and outputs remain in
`/home/alex/.ctbrowser-auto/claude/ctbrowser/r15/css6/`.
See `wpt.md` for combined recovery status and the next focus/HTML failures.

## Round thirteen — 2026-09-26

Full `css/` at `037fbaae`: **2,234/3,014 runnable files PASS, 101,800
subtests PASS**, from 2,187 and 99,154 at `6011bf8d` - +47 files, +2,646
subtests. Round thirteen's CSS4 (16 commits, merged whole at its last green
gate when the session limit cut it off): calc-size() and interpolate-size
(css-values +1,737 subtests - `calc-size/animation/*` and the keyword
sizes), css-sizing +230, css-transforms +220 (palettes, track lists,
origins, em transforms, singular matrices), css-fonts +88, css-grid +80,
cssom-view +76 (a reversed flex container scrolls from its end), selectors
+59 (`:focus-visible`'s UA ring, `:placeholder-shown`, dir=auto on text
controls), and a pseudo-element's box hit-testing as its originating element.
**Three files lost**: `css-transforms/animation/transform-interpolation-007`
(a 2D-equivalent matrix3d now serialises as matrix3d),
`transform-non-invertible-no-transition` and
`css-values/viewport-units-scroll-no-cycles-001.tentative` - round
fourteen's CSS5 has them first. CSS4's ungated last batch (every size
keyword through calc-size, more easing) went to CSS5 as a patch.

## The outage batch — 2026-09-26

Full `css/` at `6011bf8d`: **2,187/3,014 runnable files PASS, 99,154
subtests PASS**, from 2,168/2,926 and 98,450 at `5ca8e8a1` - zero files
lost at any of the batch's steps (`wpt.md` has the per-commit table):

- `acc14107` (GC pins, ASan fixes, WebIDL globals): +1 file, +544
  subtests. 520 are the five idlharness files the corpus re-fetch made
  runnable (`css/cssom` 277, `css/cssom-view` 167, `css/css-conditional`
  35, `css/css-cascade` 26, `container-queries` 15 - all still FAIL on
  their remaining members), 21 of those from interface objects no longer
  being enumerable globals; 24 are `css/css-values/viewport-units-
  keyframes.html`, which passes. The ASan sweep found `lone_value`
  (`lib/Style/css/calc/units.cpp`) reading past `eof` on empty text in 257
  pages, and a frame's CSS animation records marked nowhere; after the
  fixes `css/` ran under ASan with no report.
- `6f4d39b8` (`::before`/`::after` generate boxes): +1 file
  (`css/css-conditional`), +63 subtests. Six subtests went PASS → FAIL in
  files that already failed, each an accidental pass: with no pseudo box a
  test element's `auto` height was 0, so a `0px` → `auto` transition
  (`height-composition`, `interpolate-size-height-interpolation`) read
  0px whatever the engine did. Bootstrap's geometry baseline moved and was
  read line by line; it exposed a float's padding counted twice in its
  item's max-content width (a breadcrumb item grows 24px, not 16px).
- `db57406e` (testdriver runs): 88 files became runnable; +17 PASS
  (`css/selectors` 13 - ten `focus-visible-*`, `active-after-relayouts`,
  two `:has()` invalidations - and `css/css-conditional` 4
  `scroll-state-scrolled-*`), +97 subtests.

## Round eleven — 2026-09-26

Full `css/` at `5ca8e8a1`: **2,168/2,926 runnable files PASS, 98,450
subtests PASS**, from 2,041 and 90,003 at `b9c6662e` - **+127 files,
+8,447 subtests, zero files lost.** Same instrument. Round eleven's CSS3
(53 commits; cut off by a usage limit after its fifth green gate, which
measured the same numbers) - modules that gained three files or a hundred
subtests:

| module | files PASS `b9c6662e` / `5ca8e8a1` | subtests PASS `b9c6662e` / `5ca8e8a1` |
|---|---:|---:|
| `css/motion` | 21 / 37 | 3,504 / 4,892 |
| `css/css-grid` | 28 / 41 | 2,662 / 3,215 |
| `css/css-masking` | 33 / 45 | 4,733 / 6,077 |
| `css/css-transforms` | 35 / 45 | 3,539 / 4,940 |
| `css/css-fonts` | 63 / 73 | 3,555 / 3,969 |
| `css/filter-effects` | 17 / 25 | 2,020 / 2,338 |
| `css/css-transitions` | 20 / 27 | 811 / 902 |
| `css/css-shapes` | 13 / 19 | 1,750 / 2,340 |
| `css/css-lists` | 13 / 17 | 396 / 440 |
| `css/css-ui` | 43 / 47 | 1,313 / 1,355 |
| `css/selectors` | 142 / 145 | 4,641 / 5,148 |
| `css/cssom` | 160 / 163 | 3,299 / 3,444 |
| `css/css-animations` | 33 / 36 | 442 / 579 |
| `css/css-tables` | 14 / 17 | 40 / 167 |
| `css/css-images` | 26 / 29 | 3,081 / 3,207 |
| `css/css-text` | 83 / 86 | 1,794 / 1,910 |
| `css/css-rhythm` | 12 / 15 | 89 / 155 |
| `css/css-anchor-position` | 3 / 6 | 124 / 186 |
| `css/css-gaps` | 70 / 73 | 4,097 / 4,145 |
| `css/css-content` | 2 / 4 | 97 / 205 |
| `css/fill-stroke` | 0 / 1 | 0 / 356 |
| `css/mediaqueries` | 16 / 16 | 1,381 / 1,565 |

A frame's style sheets and computed style are the frame's now (the
attribute-case selector files' iframe halves), `shape()` and paths
interpolate, 3D transforms compute as `matrix3d`, grid tracks interpolate
track by track, and the shorthands `grid`, `transition`, `animation-range`,
`mask` and `block-step` split into their longhands. The largest failures
left: `calc-size()` / `interpolate-size` interpolation (~2,000 subtests),
`scrollWidth`/`scrollHeight` with negative margins (420), `color-mix()`
with `none` and powerless hues (356).

## Rounds eight to ten — 2026-09-25

Full `css/` at the round-ten merge `b9c6662e`: **2,041/2,926 runnable files
PASS, 90,003 subtests PASS**, from **1,648 and 71,216** at `c246c9b5` - **+393
files, +18,787 subtests** over three rounds of a CSS agent, each merged and
gated by the root. Same WPT `3f6b09ae`, devbox, four workers, 4 GB cap,
deterministic GL driver. Every module that gained three files or a hundred
subtests, and any that lost a file:

| module | files PASS `c246c9b5` / `b9c6662e` | subtests PASS `c246c9b5` / `b9c6662e` |
|---|---:|---:|
| `css/css-conditional` | 8 / 130 | 1,676 / 2,839 |
| `css/css-gaps` | 14 / 70 | 363 / 4,097 |
| `css/css-properties-values-api` | 5 / 48 | 129 / 205 |
| `css/selectors` | 130 / 142 | 4,545 / 4,641 |
| `css/css-ui` | 31 / 43 | 1,281 / 1,313 |
| `css/css-text-decor` | 18 / 29 | 451 / 1,175 |
| `css/css-sizing` | 20 / 30 | 2,540 / 2,890 |
| `css/css-text` | 73 / 83 | 1,470 / 1,794 |
| `css/css-masking` | 24 / 33 | 1,325 / 4,733 |
| `css/css-fonts` | 54 / 63 | 3,130 / 3,555 |
| `css/css-cascade` | 52 / 61 | 1,016 / 1,068 |
| `css/motion` | 13 / 21 | 1,643 / 3,504 |
| `css/css-overflow` | 19 / 27 | 286 / 349 |
| `css/mediaqueries` | 9 / 16 | 277 / 1,381 |
| `css/css-shapes` | 7 / 13 | 328 / 1,750 |
| `css/css-values` | 177 / 183 | 7,678 / 7,903 |
| `css/css-inline` | 13 / 19 | 254 / 291 |
| `css/css-lists` | 9 / 13 | 365 / 396 |
| `css/filter-effects` | 14 / 17 | 350 / 2,020 |
| `css/css-grid` | 25 / 28 | 1,869 / 2,662 |
| `css/css-transitions` | 17 / 20 | 759 / 811 |
| `css/css-break` | 19 / 22 | 514 / 540 |
| `css/css-variables` | 31 / 34 | 451 / 461 |
| `css/css-images` | 23 / 26 | 3,074 / 3,081 |
| `css/css-animations` | 30 / 33 | 438 / 442 |
| `css/css-view-transitions` | 8 / 10 | 337 / 967 |
| `css/css-transforms` | 33 / 35 | 3,310 / 3,539 |
| `css/cssom` | 161 / 160 | 3,178 / 3,299 |

The largest movers are whole features the three rounds implemented:
container queries - `@container` chosen by style over the laid-out
container, the `cq*` units, `style()` and `scroll-state()` queries
(`css-conditional` 8 -> 130); the gap-rule lists of `css-gaps`; registered
custom properties (`@property` and `CSS.registerProperty`, including inside
`@container`); `<basic-shape>` and `path()` for `clip-path`,
`shape-outside` and `offset-path` (with `ray()`); the `mask-border`
longhands; `@supports` `selector()`, `at-rule()`, `font-format()` and
`font-tech()`.

Two files that passed at `c246c9b5` do not at `b9c6662e`:
`css/css-values/random-item-in-container-query.tentative.html` (handed to
round eleven's CSS agent) and `css/cssom/serialize-values.html` (open);
`css-color` lost 14 subtests with its files unchanged. `wpt.md` has the
eight-suite table this is one row of.

## Background shorthand and typed position grammar — 2026-09-18

Full `css/` replay at `fd54d8b1`: **1,647/2,926 runnable files PASS,
71,205 subtests PASS**. Against `bd840fed`: **+7 files and
+70 passing subtests, zero passing files or subtests lost**. Same WPT
`3f6b09ae`, devbox, four workers, 4 GB cap and deterministic GL driver;
213.9 seconds.

| module | files PASS before / after | subtests PASS before / after |
|---|---:|---:|
| `css/css-backgrounds` | 91 / 94 | 5,837 / 5,892 |
| `css/css-variables` | 27 / 31 | 436 / 451 |

Background layers now expand into their longhands in CSSOM and the cascade.
Omitted values reset, colors occur only in the final layer, and size slashes
must immediately follow positions. Serialization omits initial components
and retains clip/origin combinations. Calculated sizes retain their range
through substitution and inheritance. Position math must fit the position
grammar; an image or color function cannot stand in for a length.
Substituted CSS-wide custom-property keywords now use their own cascade
position, including `revert-layer`, before other properties consume them.
An invalid shorthand resets every longhand. Expansion refuses unsupported
image functions; this restores the attr-security case without claiming
`image-set()` or complete attribute-taint support. Guaranteed-invalid custom
properties serialize as empty strings instead of exposing an internal sentinel.

Fresh backgrounds measured **91 -> 94 files PASS** and
**5,837 -> 5,892 subtests PASS**, with no losses. The computed, invalid and
shorthand-serialization files now pass. Background-valid retains inconsistent
transparent-color spelling assertions; no special-case spelling was added.
The browser gate passed **216/216** (93.67 seconds), excluding
compiler tests; formatting passed. Full statuses and evidence: `wpt.md` and
`/tmp/ctbrowser29/`. No WPT or test262 expectation files changed.

Next: computed three-component positions, logical axis keywords, relative
image URLs and container-size layout integration. Runtime measurements are
recorded separately in `test262.md`.

## Computed images and String coercion errors — 2026-09-18

Full CSS at `bd840fed`: **1,640/2,926 runnable files PASS,
71,135 subtests PASS**; **+3 files and +31 subtests** since
`ad6f9147`, with zero passing file or subtest losses. The browser gate passed
**216/216** (70.14 seconds), excluding compiler tests; formatting passed.

| module | files PASS before / after | subtests PASS before / after |
|---|---:|---:|
| `css/css-backgrounds` | 90 / 91 | 5,833 / 5,837 |
| `css/css-color` | 46 / 46 | 12,050 / 12,070 |
| `css/css-lists` | 9 / 9 | 362 / 365 |
| `css/css-masking` | 23 / 24 | 1,322 / 1,325 |
| `css/css-values` | 176 / 177 | 7,677 / 7,678 |

Radial radii clamp negative calculated lengths and percentages after the
shared math fold. Substituted negative literals remain invalid, including
beside unresolved calculations. Light/dark images use the element's color
scheme, retain nested color context and compute selected `none` values to
transparent images. Color functions remain stops when parsing a gradient
header; quoted URLs use the shared serializer.

Fresh backgrounds measured **90 -> 91 files PASS** and
**5,833 -> 5,837 subtests PASS**, with no losses. The background-image
computed-value file now passes. The merged runtime also fixes String static
coercion errors; its separate measured gain is in `test262.md`.
Full statuses and evidence: `wpt.md` and `/tmp/ctbrowser28/`.
No expectations changed.

Next: background shorthand expansion (`background: none` leaves longhands
empty), relative image URLs and the container-size layout handoff.

## Computed layer sizes, cross-fade grammar and URL escapes — 2026-09-18

Full CSS at `ad6f9147`: **1,637/2,926 runnable files PASS,
71,104 subtests PASS**; **+4 files and +38 subtests** since `c7332149`,
with zero passing file or subtest losses. The browser gate passed
**216/216** (67.07 seconds), excluding compiler tests; formatting passed.

| module | files PASS before / after | subtests PASS before / after |
|---|---:|---:|
| `css/css-backgrounds` | 87 / 90 | 5,815 / 5,833 |
| `css/css-lists` | 9 / 9 | 344 / 362 |
| `css/css-masking` | 22 / 23 | 1,320 / 1,322 |
| `css/css-values` | 176 / 176 | 7,677 / 7,677 |

Background and mask sizes resolve each layer before inheritance and clamp
negative calculated lengths and pure percentages. Substituted negative
literals remain invalid beside unresolved math. Random ordinals, finite
bounds and unresolved length/percentage mixes are preserved.

Cross-fade accepts percentages inside nested images and rejects malformed
items. Unquoted URL tokens remain images, including color-like URL bodies
and escaped characters. The URL scanner consumes complete hexadecimal
escapes with their optional terminating whitespace.

Fresh backgrounds measured **87 -> 90 files PASS** and
**5,815 -> 5,833 subtests PASS**, with no losses. Background-image valid and
invalid parsing files and background-size computed values now pass. Added
regressions failed before their fixes. Full status and evidence: `wpt.md`
and `/tmp/ctbrowser27/`. No expectations changed; test262 was not replayed.

The unchanged css-values passing counts hide a status change: three
`ch-empty-pseudo-recalc-on-font-load` subtests changed from TIMEOUT to FAIL.

Next: radial-gradient calculated radii still reach the image parser as
negative literals. Relative image URLs, gradient precision and the
container-size layout handoff remain separate failures.

## Border-image sides, layer repetition and size ranges — 2026-09-18

Full CSS at `c7332149`: **1,633/2,926 runnable files PASS,
71,066 subtests PASS**; **+8 files and +254 subtests** since `a7d76d12`,
with zero passing file or subtest losses. The browser gate passed
**216/216** (66.72 seconds), excluding compiler tests; formatting passed.

| module | files PASS before / after | subtests PASS before / after |
|---|---:|---:|
| `css/css-backgrounds` | 80 / 87 | 5,589 / 5,815 |
| `css/css-masking` | 21 / 22 | 1,292 / 1,320 |

Border-image side lists expand before composition and compress afterward,
including `fill`. Repeatable layer lists cycle over their original lengths
when expanded to a common length. Background and mask animation sizes clamp
negative lengths and pure percentages through their property range metadata.
Unresolved length/percentage mixes retain both terms.

Fresh focused backgrounds measured **80 -> 87 files PASS** and
**5,589 -> 5,815 subtests PASS**, with no losses. The border-image composition
and background-position interpolation files now pass. Each regression failed
before its fix. Full status and evidence: `wpt.md` and `/tmp/ctbrowser26/`.
No expectations changed; test262 was not replayed.

Next: static `background-size` calculations can still become negative after
early math folding. The gradient precision and relative URL failures remain;
container units still need the layout/style handoff described below.

## Animation ranges, addition and repeating gradients — 2026-09-18

Full CSS at `a7d76d12`: **1,625/2,926 runnable files PASS, 70,812 subtests PASS**;
**+4 files and +185 subtests** since `22ff781c`, with zero passing
file or subtest losses. The browser gate passed **216/216** (66.47 seconds),
excluding compiler tests; formatting passed.

| module | files PASS before / after | subtests PASS before / after |
|---|---:|---:|
| `css/css-backgrounds` | 77 / 80 | 5,480 / 5,589 |
| `css/css-flexbox` | 29 / 30 | 955 / 959 |
| `css/css-grid` | 25 / 25 | 1,805 / 1,869 |
| `css/css-masking` | 21 / 21 | 1,290 / 1,292 |
| `css/css-size-adjust` | 2 / 2 | 197 / 201 |
| `css/motion` | 13 / 13 | 1,641 / 1,643 |

Nonnegative pure percentages now clamp after animation arithmetic. Equal
numeric components now add in lists, preserving equal keywords. Repeating
linear, radial and conic gradients now report their existing support through
`CSS.supports()`. Backgrounds gains the slice/width interpolation files and
background-size composition; its focused run gains **109 passing subtests**.

Next: the border-image outset, slice and width composition files each retain
30 failing subtests, including unexpanded lists and uncompressed zero values.
Container-relative slice/outset calculations still use viewport dimensions:
layout size containment and the browser's container-size callback handoff are
missing. The diagnosis is preserved without a source workaround.
Full status and evidence: `wpt.md` and `/tmp/ctbrowser25/`.
No expectations changed; test262 was not replayed.

## Border-image grammar and computed ranges — 2026-09-18

Full CSS at `22ff781c`: **1,621/2,926 files PASS, 70,627 subtests PASS**;
**+14 files and +209 subtests** since `47d281a0`, with zero passing file or
subtest losses. The browser gate passed **216/216** (69.42 seconds), excluding
compiler tests; formatting passed.

| module | files PASS before / after | subtests PASS before / after |
|---|---:|---:|
| `css/css-align` | 51 / 53 | 1,309 / 1,313 |
| `css/css-backgrounds` | 66 / 77 | 5,319 / 5,480 |
| `css/css-box` | 32 / 32 | 877 / 878 |
| `css/css-flexbox` | 29 / 29 | 952 / 955 |
| `css/css-fonts` | 53 / 54 | 3,090 / 3,130 |

Border-image now validates its numeric components and single image source,
expands all longhands, serializes slash groups and resets omitted values.
Computed border widths clamp after expansion. Image components resolve their
lengths and clamp independently before inheritance, including values supplied
through variables. Repeated computed values use the existing CSSOM serializer.
The shared final clamp also corrects gaps, padding, flex components and font sizes.

Next: `border-image-slice-interpolation` still has 24 failing subtests and
`border-image-width-interpolation` four, including unclamped negative
percentages. Image slice/outset computed-value files each retain two failures
using container-relative units. Additive image composition remains separate.
Full status and evidence: `wpt.md` and `/tmp/ctbrowser24/`.
No expectations changed; test262 was not replayed.

## Border-radius grammar and computed values — 2026-09-18

Full CSS at `47d281a0`: **1,607/2,926 files PASS, 70,418 subtests PASS**;
**+5 files and +37 subtests** since `92fda299`, with zero passing file or
subtest losses. The browser gate passed **216/216** (68.82 seconds), excluding
compiler tests; formatting passed.

| module | files PASS before / after | subtests PASS before / after |
|---|---:|---:|
| `css/css-backgrounds` | 61 / 66 | 5,282 / 5,319 |

Radius shorthand parsing now validates both axes, expands four corner pairs
and compresses repeated values. WebKit aliases share the same declarations.
The cascade retains vertical radii and computes each component before
inheritance; radius animation results respect the nonnegative range.
The computed, invalid, valid, WebKit-valid and interpolation files all pass.
All other CSS modules match the preceding measurement.

Next: border-image grammar and shorthand expansion. Its invalid-value file
has eleven failing subtests; the shorthand file has twenty-one. Negative
computed widths remain too: `border-width-computed` has one failing subtest,
`border-image-width-computed` one and `border-image-outset-computed` three.
Full status and evidence: `wpt.md` and `/tmp/ctbrowser23/`.
No expectations changed; test262 was not replayed.

## Column-rule widths and layout coordinates — 2026-09-18

Full CSS at `92fda299`: **1,602/2,926 files PASS, 70,381 subtests PASS**;
**+3 files and +6 subtests** since `7cfe95b5`, with zero passing file or
subtest losses. The browser gate passed **216/216** (66.98 seconds), excluding
compiler tests; formatting passed.

| module | files PASS before / after | subtests PASS before / after |
|---|---:|---:|
| `css/css-multicol` | 34 / 36 | 1,456 / 1,460 |
| `css/css-gaps` | 14 / 14 | 362 / 363 |
| `css/cssom-view` | 78 / 79 | 1,141 / 1,142 |

Column-rule width keywords now compute to pixels before shorthand folding,
including the initial value and rules with `none` or `hidden` styles. Image
coordinates and offset metrics subtract accumulated layout translations;
their regression also covers percentage translations, scrolling and missing
boxes. All other CSS modules match the preceding measurement.

Next: border-radius has six failing computed-value subtests, eleven invalid
value subtests and three valid serialization subtests. The first failures
lose the vertical radii in `5em / 1px 2% 3px 4%`, accept `auto`, and fail to
compress repeated vertical radii. Multicol's ten remaining subtest failures
are in its three pseudo-selector files. Full status and evidence: `wpt.md`
and `/tmp/ctbrowser22/`. No expectations changed; test262 was not replayed.

## Column-wrap and transformed geometry — 2026-09-18

Full CSS at `7cfe95b5`: **1,599/2,926 files PASS, 70,375 subtests PASS**;
**+3 files and +47 subtests** since `39f651a3`, with zero passing file or
subtest losses. The final browser gate passed **216/216** (70.52 seconds),
excluding compiler tests; formatting passed.

| module | files PASS before / after | subtests PASS before / after |
|---|---:|---:|
| `css/css-multicol` | 32 / 34 | 1,413 / 1,456 |
| `css/css-values` | 175 / 176 | 7,676 / 7,677 |
| `css/css-cascade` | 52 / 52 | 1,015 / 1,016 |
| `css/cssom-view` | 78 / 78 | 1,139 / 1,141 |

`column-wrap` now parses and resets through `columns`; its two animation
files pass. The cascade gain is its new initial-value check. Composed 2D
transforms fix scaled viewport rectangles, and shared geometry keeps
`getBoxQuads` and coordinate conversions consistent with those rectangles.
The first replay's seven GeometryUtils losses were corrected before this
measurement; rotated corners and zero-scale quads gain two subtests.
All other CSS modules match the preceding measurement.

Next: multicol has four column-rule computed/default-value failures and
ten pseudo-selector failures. Image `x`/`y` still fail the transform-ignoring
coordinate test; elliptical radius expansion remains open. Transform
geometry still has the existing 2D/px parser limit.
Full status and evidence: `wpt.md` and `/tmp/ctbrowser21/`. No expectations changed.

## Detailed sections

The following sections retain the complete original text and measurements.

<a id="columns-recovery--2026-09-18"></a>
- [Columns recovery — 2026-09-18](css-conformance/01-columns-recovery--2026-09-18.md#columns-recovery--2026-09-18)
<a id="positive-integer-animation-follow-up--2026-09-18"></a>
- [Positive integer animation follow-up — 2026-09-18](css-conformance/01-columns-recovery--2026-09-18.md#positive-integer-animation-follow-up--2026-09-18)
<a id="css-recovery--2026-09-18"></a>
- [CSS recovery — 2026-09-18](css-conformance/01-columns-recovery--2026-09-18.md#css-recovery--2026-09-18)
<a id="1-the-measurement-was-wrong-before-it-was-low"></a>
- [1. The measurement was wrong before it was low](css-conformance/01-columns-recovery--2026-09-18.md#1-the-measurement-was-wrong-before-it-was-low)
<a id="2-wide-every-css-module-measured--2026-09-18-round-seven"></a>
- [2-wide. Every CSS module, measured — 2026-09-18: round seven](css-conformance/01-columns-recovery--2026-09-18.md#2-wide-every-css-module-measured--2026-09-18-round-seven)
<a id="2-wide-every-css-module-measured--2026-09-17-midday-round-six-v-cssom-view"></a>
- [2-wide. Every CSS module, measured — 2026-09-17, midday: round six (V: CSSOM View)](css-conformance/01-columns-recovery--2026-09-18.md#2-wide-every-css-module-measured--2026-09-17-midday-round-six-v-cssom-view)
<a id="2-wide-every-css-module-measured--2026-09-16-evening-round-four-g2"></a>
- [2-wide. Every CSS module, measured — 2026-09-16, evening: round four (G2)](css-conformance/01-columns-recovery--2026-09-18.md#2-wide-every-css-module-measured--2026-09-16-evening-round-four-g2)
<a id="2-wide-every-css-module-measured--2026-09-16-after-rounds-two-and-three"></a>
- [2-wide. Every CSS module, measured — 2026-09-16, after rounds two and three](css-conformance/01-columns-recovery--2026-09-18.md#2-wide-every-css-module-measured--2026-09-16-after-rounds-two-and-three)
<a id="2-wide-every-css-module-measured--2026-09-13"></a>
- [2-wide. Every CSS module, measured — 2026-09-13](css-conformance/01-columns-recovery--2026-09-18.md#2-wide-every-css-module-measured--2026-09-13)
<a id="2-where-the-two-suites-stand--2026-09-12"></a>
- [2. Where the two suites stand — 2026-09-12](css-conformance/01-columns-recovery--2026-09-18.md#2-where-the-two-suites-stand--2026-09-12)
<a id="2-late-where-the-two-suites-stood--2026-09-10-late"></a>
- [2-late. Where the two suites stood — 2026-09-10, late](css-conformance/01-columns-recovery--2026-09-18.md#2-late-where-the-two-suites-stood--2026-09-10-late)
<a id="2-evening-where-the-two-suites-stood--2026-09-10-evening"></a>
- [2-evening. Where the two suites stood — 2026-09-10, evening](css-conformance/01-columns-recovery--2026-09-18.md#2-evening-where-the-two-suites-stood--2026-09-10-evening)
<a id="2-morning-where-the-two-suites-stood--2026-09-10-morning"></a>
- [2-morning. Where the two suites stood — 2026-09-10, morning](css-conformance/01-columns-recovery--2026-09-18.md#2-morning-where-the-two-suites-stood--2026-09-10-morning)
<a id="2a-prev-where-the-two-suites-stood--2026-09-07-night"></a>
- [2a-prev. Where the two suites stood — 2026-09-07, night](css-conformance/01-columns-recovery--2026-09-18.md#2a-prev-where-the-two-suites-stood--2026-09-07-night)
<a id="the-two-blocks-in-front-of-both-suites-and-neither-is-a-css-bug"></a>
- [THE TWO BLOCKS IN FRONT OF BOTH SUITES, and neither is a CSS bug](css-conformance/01-columns-recovery--2026-09-18.md#the-two-blocks-in-front-of-both-suites-and-neither-is-a-css-bug)
<a id="2a-where-the-two-suites-stood--2026-09-07-evening"></a>
- [2a. Where the two suites stood — 2026-09-07, evening](css-conformance/01-columns-recovery--2026-09-18.md#2a-where-the-two-suites-stood--2026-09-07-evening)
<a id="what-moved-csscssom-39---54-files"></a>
- [What moved `css/cssom`, 39 -> 54 files](css-conformance/01-columns-recovery--2026-09-18.md#what-moved-csscssom-39---54-files)
<a id="what-moved-csscss-values-29---48-files"></a>
- [What moved `css/css-values`, 29 -> 48 files](css-conformance/01-columns-recovery--2026-09-18.md#what-moved-csscss-values-29---48-files)
<a id="what-is-still-in-front-of-csscss-values"></a>
- [What is still in front of `css/css-values`](css-conformance/01-columns-recovery--2026-09-18.md#what-is-still-in-front-of-csscss-values)
<a id="2b-the-baseline-2026-09-03--and-where-it-stood-then"></a>
- [2b. The baseline, 2026-09-03 — and where it stood then](css-conformance/01-columns-recovery--2026-09-18.md#2b-the-baseline-2026-09-03--and-where-it-stood-then)
<a id="the-2026-09-03-baseline"></a>
- [The 2026-09-03 baseline](css-conformance/02-the-2026-09-03-baseline.md#the-2026-09-03-baseline)
<a id="3-the-skip-audit"></a>
- [3. The skip audit](css-conformance/02-the-2026-09-03-baseline.md#3-the-skip-audit)
<a id="4-where-the-failures-actually-live"></a>
- [4. Where the failures actually live](css-conformance/02-the-2026-09-03-baseline.md#4-where-the-failures-actually-live)
<a id="5-what-was-fixed-in-the-css-engine-and-what-it-was-worth"></a>
- [5. What was fixed in the CSS engine, and what it was worth](css-conformance/02-the-2026-09-03-baseline.md#5-what-was-fixed-in-the-css-engine-and-what-it-was-worth)
<a id="calc-may-resolve-to-a-number--css-values-3-81"></a>
- [`calc()` may resolve to a `<number>` — CSS Values 3 §8.1](css-conformance/02-the-2026-09-03-baseline.md#calc-may-resolve-to-a-number--css-values-3-81)
<a id="min-max-and-clamp--css-values-4-103"></a>
- [`min()`, `max()` and `clamp()` — CSS Values 4 §10.3](css-conformance/02-the-2026-09-03-baseline.md#min-max-and-clamp--css-values-4-103)
<a id="it-works-and-wpt-cannot-see-it"></a>
- [It works, and WPT cannot see it](css-conformance/02-the-2026-09-03-baseline.md#it-works-and-wpt-cannot-see-it)
<a id="proved-load-bearing-three-ways"></a>
- [Proved load-bearing, three ways](css-conformance/02-the-2026-09-03-baseline.md#proved-load-bearing-three-ways)
<a id="the-cssom-wall-priced"></a>
- [The CSSOM wall, priced](css-conformance/02-the-2026-09-03-baseline.md#the-cssom-wall-priced)
<a id="6-the-wall-came-down--2026-09-07"></a>
- [6. The wall came down — 2026-09-07](css-conformance/02-the-2026-09-03-baseline.md#6-the-wall-came-down--2026-09-07)
<a id="a-one-word-invalidation-bug"></a>
- [A one-word invalidation bug](css-conformance/02-the-2026-09-03-baseline.md#a-one-word-invalidation-bug)
<a id="and-the-reason-that-was-invisible"></a>
- [And the reason that was invisible](css-conformance/02-the-2026-09-03-baseline.md#and-the-reason-that-was-invisible)
<a id="the-object"></a>
- [The object](css-conformance/02-the-2026-09-03-baseline.md#the-object)
<a id="elstyle-is-a-cssstyledeclaration"></a>
- [`el.style` is a CSSStyleDeclaration](css-conformance/02-the-2026-09-03-baseline.md#elstyle-is-a-cssstyledeclaration)
<a id="it-cost-six-files-and-every-one-of-them-was-passing-by-not-testing"></a>
- [It cost six files, and every one of them was passing by not testing](css-conformance/02-the-2026-09-03-baseline.md#it-cost-six-files-and-every-one-of-them-was-passing-by-not-testing)
<a id="the-bootstrap-baseline-moved-and-was-read"></a>
- [The Bootstrap baseline moved and was read](css-conformance/02-the-2026-09-03-baseline.md#the-bootstrap-baseline-moved-and-was-read)
<a id="what-is-still-not-done-here"></a>
- [What is still not done here](css-conformance/02-the-2026-09-03-baseline.md#what-is-still-not-done-here)
<a id="7-re-running-this"></a>
- [7. Re-running this](css-conformance/02-the-2026-09-03-baseline.md#7-re-running-this)
