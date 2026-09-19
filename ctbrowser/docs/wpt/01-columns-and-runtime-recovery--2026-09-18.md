[Back to wpt.md](../wpt.md)

## Columns and runtime recovery — 2026-09-18

Full `css/` replay at `39f651a3`: **1,596/2,926 runnable files PASS,
70,328 subtests PASS**. Against `f5a00a58`: **+4 files and +53 passing
subtests, zero passing files or subtests lost**. Same WPT `3f6b09ae`,
devbox, four workers, 4 GB cap and deterministic GL driver.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `39f651a3` | 1,596 | 1,114 | 25 | 0 | 191 | 1,488 | 70,328 / 32,761 |

`696ce3cb` parses the unordered width/count components of `columns`, its
optional `/ column-height`, and resets omitted longhands. The interpolation,
computed, valid and invalid columns files now pass. Multicol gains 52
subtests; `all-prop-initial-xml` gains the newly exposed `column-height`
check. Scaled viewport rectangles remain open.

The combined browser CTest gate passed **216/216** (68.27 seconds), with
compiler tests excluded and `CTCOMPILE_MLIR=OFF`; formatting passed. The
interrupted CSS sweep produced no JSON and was rerun after verifying the
committed source hashes. Evidence: `/tmp/ctbrowser20/` contains
`browser20-final-css.json`, `css-comparison.json`, `recovered-css.log`,
`combined-gate.log`, `combined-source.sha256` and `recovered-format.log`.
The separate runtime fixes gain seven focused test262 files, recorded in
`test262.md`; other WPT directories and whole test262 were not replayed.

## Positive integer animation follow-up — 2026-09-18

Full `css/` replay at `f5a00a58`: **1,592 of 2,926 runnable files PASS
(54.4%), 70,275 subtests PASS**. Against `73833c09`: **+1 file and +52
passing subtests, zero passing files or subtests lost**. Against the
recovered `b722aa41` baseline, the session gained **57 files and 1,334
subtests**, with no losses. The corpus, four workers, 4 GB cap and
deterministic GL driver are unchanged.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `f5a00a58` | 1,592 | 1,118 | 25 | 0 | 191 | 1,488 | 70,275 / 32,813 |

The shared numeric animation result now clamps positive integers to one,
matching their parsing grammar. `column-count-interpolation` passes;
orphans/widows also gain subtests. `columns-interpolation` still fails on
the slash grammar, and scaled viewport rectangles remain open. The browser
gate passed **233/233** (69.00 seconds); formatting passed.
Evidence: `/tmp/ctbrowser-resume/positive-integers/` contains the full CSS
JSON, before/after and session comparisons, gate log and source hashes.
The earlier CSS/BigInt recovery through `9e7b6fdf` was integrated into
`ctcompile-v1` as `b71d8034`.

## CSS recovery — 2026-09-18

**1,591 of 2,926 runnable CSS files PASS (54.4%), 70,223 subtests PASS**
at `73833c09`: **+56 files and +1,282 subtests** against `b722aa41`,
with **zero passing files or subtests lost**. This is a fresh full `css/`
measurement, not a replay of the other wide directories or all test262.
Same WPT `3f6b09ae`, devbox, four workers, 4 GB address-space cap and
`CTBROWSER_GL_DRIVER=deterministic`.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `b722aa41` | 1,535 | 1,175 | 25 | 0 | 191 | 1,488 | 68,941 / 34,147 |
| `73833c09` | 1,591 | 1,119 | 25 | 0 | 191 | 1,488 | 70,223 / 32,865 |

Cascade rollback now uses substituted, expanded physical declarations.
CSS and Web Animation keyframes use the CSSOM shorthand grammar, retaining
the old expansion for shorthands CSSOM still stores whole. Inherited
keyframes read the flat-tree parent's computed value, including animations.
This restores 21 of the 23 files lost in round seven. The remaining two
are `columns-interpolation` (positive count clamping and slash syntax) and
`viewport-relative-lengths-scaled-viewport` (scaled bounding rectangles).

The first combined replay exposed 26 lost subtests in ellipse shorthand
expansion and inherited columns. Those were fixed before this final replay;
no expectations were changed. The browser CTest gate passed **233/233**
(70.77 seconds), and formatting passed. Evidence:
`/tmp/ctbrowser-resume/corrected/{gate.log,browser-resume-corrected-css.json,comparison.json,source-sha256.json}`.
Module deltas are in `css-conformance.md`. The separate focused BigInt
measurement gained 11 test262 files; see `test262.md`.

## Wide baseline — 2026-09-18: round seven recovered

**2,860 of 5,155 runnable files PASS (55.5%), 249,277 subtests PASS** at
`b722aa41`, compared with 2,696 files and 239,070 subtests at `9f8da347`:
**187 files gained, 23 lost; net +164 files and +10,207 passing subtests**.
This completes session 19's interrupted reporting. The eight baseline
JSON files in `/tmp/w-b722aa41/` finished before the extra `encoding/`
sweep, which was stopped without a result. Encoding has no previous wide
baseline and is excluded from both sides of this comparison.

Same pinned WPT corpus, devbox, four workers, 4 GB address-space cap and
`CTBROWSER_GL_DRIVER=deterministic`. The engine's recorded gate is 233/233;
no new engine build was needed to recover these completed measurements.

| directory | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `css` | 1,535 | 1,175 | 25 | 0 | 191 | 1,488 | 68,941 / 34,147 |
| `html` | 706 | 336 | 75 | 1 | 31 | 535 | 73,566 / 1,492 |
| `dom` | 386 | 94 | 15 | 4 | 5 | 150 | 51,826 / 4,909 |
| `shadow-dom` | 66 | 101 | 12 | 0 | 3 | 163 | 8,545 / 291 |
| `custom-elements` | 70 | 106 | 1 | 0 | 3 | 13 | 3,471 / 702 |
| `domparsing` | 17 | 42 | 0 | 0 | 10 | 3 | 327 / 1,285 |
| `selection` | 41 | 46 | 3 | 0 | 6 | 87 | 33,332 / 723 |
| `url` | 39 | 7 | 1 | 0 | 2 | 0 | 9,269 / 665 |

All 23 files lost at this revision are CSS: 16 `all-prop-revert[-layer]-noop` variants,
four logical margin/padding interpolation files, `columns-interpolation`,
`outline-width-interpolation`, and the previously documented scaled
viewport test. The revert variants read zero logical margins where the UA
stylesheet supplies paragraph/heading margins. The later recovery is
recorded above; expectations were not changed to accept these failures.
The full CSS module table is in `css-conformance.md`.

## The baseline — 2026-09-18: round seven merged (five suites + test262)

**816 of the 1,102 that ran (74.0%) in the five suites; 84,108 subtests
PASS** - from 805 / 84,024 at `9f8da347` (round six): **+12 files, one
lost, zero subtests lost**. Engine at `b722aa41` on `ctbrowser-wpt`: round
seven's four agents on `92f5d7c7` - K CSS Color 4/5 (`274c693e`), A3 the
value-type interpolation and composition (`46b5f01f`), L2 logical
properties and the font/white-space/animation shorthands (`274c6b49`), J2
the runtime tail: RegExp `\p{..}` over a generated UCD table, the `v` flag,
async-generator `return()`, `import defer`, module re-exports
(`b722aa41`, ctjs `d2664e9`) - plus session 17's root work on frames,
forms, events, ranges, MediaQueryList. Gated 233/233 on the merged tree.
The one file lost, `css/css-values/viewport-relative-lengths-scaled-
viewport.html`, is NOT a regression: at `9f8da347` the iframe was a 0-wide
box (`ed460525` gave it the default object size afterwards), so `50vw *
0.01` expected 0 and got 0; the real gap - `getBoundingClientRect` ignores
`transform: scale()` (`shell/bindings/element/views.cpp` applies only the
translation) - was always there and is measured now. The recovered wide corpus row
for `b722aa41` is above (results in `/tmp/w-b722aa41/`). test262 at the
same engine: **40,832 of 48,624 (84.0%)**, rows in `docs/test262.md`.
Same instrument: devbox, 4 jobs, `CTBROWSER_GL_DRIVER=deterministic`.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `css/css-values` | **175** (+4) | 90 | 2 | 0 | 4 | 237 | 508 | 7,676 / 2,650 |
| `css/cssom` | **161** (+5) | 30 | 0 | 0 | 1 | 29 | 221 | 3,178 / 381 |
| `dom/events` | **78** (+0) | 8 | 4 | 0 | 1 | 85 | 176 | 660 / 28 |
| `dom/nodes` | **249** (+2) | 45 | 14 | 0 | 1 | 53 | 362 | 12,066 / 731 |
| `html/dom` | **153** (+0) | 70 | 13 | 0 | 3 | 137 | 376 | 60,528 / 161 |
| **total** | **816** | 243 | 33 | 0 | 10 | 541 | 1643 | 84,108 / 3,951 |

Gained: css-values `calc-in-color-001`, `calc-in-media-queries-with-mixed-
units`, `ric-invalidation`, `rlh-invalidation`, `viewport-units-extreme-
scale`; cssom `caretPositionFromPoint-in-flex-container`, `computed-style-
002/003/004`, `inline-style-001`; dom/nodes `moveBefore/child-style-
preserve`, `moveBefore/live-range-updates`.

## The baseline — 2026-09-17, midday: round six merged

**805 of the 1,102 that ran (73.0%) in the five suites; wide corpus 2,696 of
5,154 (52.3%), 239,070 subtests PASS** - from 783 / 2,404 / 231,262 at
`9cd0a9e4` (round five): five suites **+22 files**, wide **+292 files**,
+7,808 subtests. Engine at `9f8da347` on `ctbrowser-wpt` (`92f5d7c7` is the
same engine plus one corrected unit expectation): round six's four agents
on `228d80d1` - H the HTML parser tail (merged by session 16 as `05404d4d`),
and J the JS runtime (`8a993f13`), CE custom elements (`ab60122e`), V CSSOM
View (`9f8da347`), which session 16 could not merge before the `/mnt/c`
mount failed and session 17 merged first thing - plus the root's session-16
work on frames, forms and events. Gated 226/227 on the merged tree, the one
red being the root's own ungated `selectors` unit expectation (fixed in
`92f5d7c7`; the engine was right). Same instrument as every row below:
devbox, 4 jobs, `CTBROWSER_GL_DRIVER=deterministic`.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `css/css-values` | **171** (+2) | 89 | 7 | 0 | 4 | 237 | 508 | 7,611 / 2,717 |
| `css/cssom` | **156** (+5) | 36 | 0 | 0 | 0 | 29 | 221 | 3,168 / 391 |
| `dom/events` | **78** (+7) | 9 | 4 | 0 | 0 | 85 | 176 | 660 / 29 |
| `dom/nodes` | **247** (+2) | 43 | 18 | 1 | 0 | 53 | 362 | 12,061 / 731 |
| `html/dom` | **153** (+6) | 71 | 13 | 0 | 2 | 137 | 376 | 60,524 / 165 |
| **total** | **805** | 248 | 42 | 1 | 6 | 541 | 1643 | 84,024 / 4,033 |
805 of the 1102 that ran (73.0%); subtests 84,024 PASS, 4,033 FAIL, 33 NOTRUN, 39 TIMEOUT

(The deltas are against the round-four table, which is the last per-suite
table; round five's +2 were `dom/events` 70 -> 71 and `dom/nodes` 244 ->
245, already inside these numbers.)

The wide corpus, by top-level directory, files PASS (subtests PASS / FAIL):
`css` **1,413** of 2,925 (59,562 / 43,529 - the module table is in
`docs/css-conformance.md`), `html` **676** (73,206 / 1,810), `dom` **381**
(51,490 / 5,241), `shadow-dom` **62** (8,488 / 318), `custom-elements`
**70** (3,467 / 651), `domparsing` 14 (262 / 1,350), `selection` 41
(33,326 / 729), `url` 39 (9,269 / 665). The moves, each an agent's:
- **`custom-elements` 14 -> 70 files, subtests 2,356 -> 3,467** (CE: the
  HTML element constructors, customized built-ins, spec-order `define()`,
  ElementInternals with the form-associated members and CustomStateSet,
  per-frame registries, `new CustomElementRegistry()`).
- **`css/cssom-view` 26 -> 68 files, subtests 412 -> 1,084** (V: the
  scrolling area, the scroll APIs on elements and the window, scroll state,
  hit testing, `GeometryUtils`, `checkVisibility`, the viewport's overflow).
- **`html/syntax` 109 -> 209** (H: foreign content with the adjustment
  tables, `<template>`, the serialisers, encoding sniffing - session 16's
  own merge), `html/dom` 147 -> 153, `dom/events` 71 -> 78, `dom/nodes` 245
  -> 247, `css/cssom` 151 -> 156, `css` 1,352 -> 1,413.
- test262 **39,175 -> 39,731** (J; `docs/test262.md`).

**Three CRASHes, all SIGABRT under the runner's 4 GB address-space cap and
none of them new code paths** (they were TIMEOUT or FAIL before):
`dom/nodes/NodeList-static-length-getter-tampered-1.html`, `html/webappapis/
dynamic-markup-insertion/document-write/032.html`, `dom/ranges/
Range-mutations-dataChange.html`. Not yet diagnosed; the first thing to look
at next, since a crash is a crash whatever the cap.

## The baseline — 2026-09-16, night: round five merged

**783 of the 1,104 that ran (70.9%) in the five suites; wide corpus 2,404 of
5,156 (46.6%), 231,262 subtests PASS** - from 781 / 2,310 / 220,908 at
`6edb7421`: five suites **+2 files -0**, wide **+94 files, ZERO lost**.
Engine at `9cd0a9e4` on `ctbrowser-wpt` (round five on `6edb7421`: S2 the
shadow-tree serialisation + flat-tree event path, C2 the shadow cascade
selectors, T3 a VM capture bug + test262 class reading, U3 IDNA CheckBidi),
gated 221/221 green.

The moves, all in the wide corpus:
- **`html/syntax` 28 -> 109 (+81 files)**, the largest single jump this
  session - NOT a parser change but T3's VM fix (`e1538500`): a name captured
  only through an object-literal shorthand `{ x }` inside a nested function
  was left unboxed and read `undefined`, which made the html5lib test harness
  see "N duplicate test names" and error the whole file. With the capture
  fixed the per-fixture variants report their real per-case results.
- **`shadow-dom` 57 -> 60 files, subtests 1,489 -> 8,356** (S2's `getHTML`
  serialises `<template shadowrootmode>` roots - gethtml.html alone is 6,528
  subtests - and the event path now walks the flat tree).
- `custom-elements` subtests 2,175 -> 2,356, `url` 38 -> 39 (9,269 subtests,
  CheckBidi), `dom/nodes` +1. The single-activation regression of round four
  is FIXED (S2: it was a pre-existing reset-button bug - a reset control now
  fires a cancelable `reset` event before clearing).

test262 is unchanged at `39,175 / 48,624 (80.6%)`: T3's fix moved WPT
harness errors, not test262 counts, and the other three agents touch no
test262 path. `docs/test262.md`'s `6edb7421` row still stands.

## The baseline — 2026-09-16, evening: round four merged

**781 of the 1,104 that ran (70.7%); subtests 83,566 PASS, 4,462 FAIL** -
from 776 (70.3%) at `273773cd` (the row below): **+6 files, -1**. Engine at
`6edb7421` on `ctbrowser-wpt` (round four's four branches on `273773cd`: S
shadow DOM / custom elements / Selection / DOMParser, G2 the CSS value
grammars, T2 test262 `language`+`annexB`, U2 the URL surface and
TextEncoder/Decoder), gated 221/221 green, same run as the test262 row of
this SHA in `docs/test262.md`. Most of round four lands in the WIDE corpus
below, not the five suites - which is why the five-suite move is small and
the wide move is +149 files.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `css/css-values` | **169** (+6) | 91 | 7 | 0 | 4 | 237 | 508 | 7,579 / 2,749 |
| `css/cssom` | **151** (+0) | 41 | 0 | 0 | 0 | 29 | 221 | 2,822 / 737 |
| `dom/events` | **70** (-1) | 18 | 3 | 0 | 2 | 85 | 178 | 613 / 92 |
| `dom/nodes` | **244** (+0) | 45 | 18 | 0 | 2 | 53 | 362 | 12,054 / 734 |
| `html/dom` | **147** (+0) | 73 | 11 | 0 | 8 | 137 | 376 | 60,498 / 150 |
| **total** | **781** | 268 | 39 | 0 | 16 | 541 | 1645 | 83,566 / 4,462 |
781 of the 1104 that ran (70.7%); subtests 83,566 PASS, 4,462 FAIL, 76 NOTRUN, 37 TIMEOUT

**The one lost, and it is a real regression:** `dom/events/Event-dispatch-
single-activation-behavior.html` (PASS -> FAIL, 38 subtests), the form-submit
and form-reset cases only (checkbox/radio still pass): clicking an
`<input type=submit>` no longer records its form's activation. It was PASS at
`273773cd` (F's forms work) and broke in the round-four merge; the only
round-four edit to the dispatch path is `1b45c4a4` (composedPath per
listener), which is additive and should not touch activation, so the cause
is not yet found - it needs a `ctdrive` probe, which the lock did not free
for this session. NOT re-baselined: this is the first thing to fix next.

### And the wide corpus at the same SHA

**2,310 of the 5,156 that ran (44.8%); subtests 220,908 PASS, 72,033 FAIL**
- from 2,162 (41.9%) and 180,098 / 78,842 at `273773cd`: **+149 files, -1**
(the same activation regression). The four agents' suites moved:
`selection` **0 -> 41** /183 (17 -> 33,326 subtests, 48 HARNESS_ERRORs -> 5:
a real Selection over the Range), `url` **22 -> 38** /49 (5,400 -> 9,242:
`<a>`/`<area>` HyperlinkUtils, the URLSearchParams iterator, UTS #46),
`shadow-dom` **49 -> 57** /345 (350 -> 1,489: `attachShadow` options, slots,
`composedPath` per listener), `custom-elements` **9 -> 14** /193 (2,175 ->
2,345: the name rule, detached reactions), `domparsing` **10 -> 11** (the
scripting flag off for a DOMParser document), `html/webappapis` **122 ->
124**. `encoding` (dropped from the wide run since 2026-09-16 - its 1,261
files timed out on the missing TextDecoder) now has TextEncoder/TextDecoder:
measured on filters, `textdecoder` 0 -> 14 /19 (7,461 subtests), `textencoder`
0 -> 1 /2. And the 65 CSS modules (`docs/css-conformance.md` §2-wide):
**1,352 files PASS of 2,925 (from 1,276), +76, one lost**, 57,583 subtests -
G2's tightened grammars (css-animations +16, css-text +7, css-transitions
+6, css-overflow +6, css-fonts +5).

## The baseline — 2026-09-16, afternoon: rounds two and three merged

**776 of the 1,104 that ran (70.3%); subtests 83,317 PASS, 4,449 FAIL** -
from 740 (67.2%) and 45,822 / 2,025 at `9c70aaa0` (the row below):
**+43 files, -7**. Engine at `273773cd` on `ctbrowser-wpt` (the ctbrowser
tree of `316dcb34`, integrated into `ctcompile-v1` as `bee703ed`: `9c70aaa0`
plus the four round-two branches - A animations, G properties, L layout, C
cascade, session 13 - the four round-three branches - B typed-array kinds,
U URL, D parser, F forms/range, this session - and the fixes each merge's
gate demanded), same run as the test262 row of the
same SHA in `docs/test262.md`; the five suites, 4 workers,
`CTBROWSER_GL_DRIVER=deterministic`, 4 GB cap, corpus `3f6b09ae3e`. The
intermediate cut `e29e197f` (round two only) measured **761** (68.9%),
81,185 / 3,781, so round three is +16 files / -1 on top of it.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `css/css-values` | **163** (+10) | 96 | 7 | 0 | 5 | 237 | 508 | 7,293 / 2,773 |
| `css/cssom` | **151** (+7) | 41 | 0 | 0 | 0 | 29 | 221 | 2,821 / 738 |
| `dom/events` | **71** (+0) | 17 | 3 | 0 | 2 | 85 | 178 | 651 / 54 |
| `dom/nodes` | **244** (+3) | 45 | 16 | 2 | 2 | 53 | 362 | 12,054 / 734 |
| `html/dom` | **147** (+16) | 73 | 11 | 0 | 8 | 137 | 376 | 60,498 / 150 |
| **total** | **776** | 272 | 37 | 2 | 17 | 541 | 1645 | 83,317 / 4,449 |
776 of the 1104 that ran (70.3%); subtests 83,317 PASS, 4,449 FAIL, 76 NOTRUN, 37 TIMEOUT

**The 43 gained.** The four 10,000-subtest reflection pages
(`reflection-{text,forms,embedded,tabular}`: the runner's one-second
`recv` gave up on them, `41a285dc`, and the subtest total is what those add
- 26,036 -> 60,498 in html/dom); twelve `innertext-with-white-spaces`
variants (agent D's tree builder keeps the text the old one folded); the
calc-size and integer-interpolation rows (L and A); `calc-interpolation`,
`random-in-animations`, the two `moveBefore/continue-css-animation-*`
TIMEOUTs that now finish (A); `lh-unit-004`, `clamp-color-computed`, the
tree-counting functions (G); `cssstyledeclaration-nested`,
`serialize-values`, `font-family-serialization-001`, the `getComputedStyle-
insets-*` pair, `HTMLLinkElement-load-event` (C, and this session's
per-script sheet application); `ParentNode-querySelector-escapes`.

**The 7 lost, each read.** Six are agent G's, named in `docs/plans/wpt-
next.md` (G2): a property that was an expando refused everything and its
real grammar now accepts some invalid forms or serialises differently -
`sin-cos-tan-computed`, `minmax-angle-computed`, `calc-background-position-
003`, `calc-linear-radial-conic-gradient-001`, `random-serialize`, and
`css/cssom/getComputedStyle-property-order` (211 more properties on the
declaration, in table order rather than alphabetical). The seventh is THIS
session's, and the one open regression: `css/css-values/viewport-units-
invalidation` - "100vw computes to 400px after frame resize", got 200px -
was PASS at `e29e197f` and FAIL from `f1613a5c` on; a frame document's
viewport units are not re-resolved after the frame is resized, and the
change between the two is that a page's sheets are now applied per parser
script (`eb61fe9c`, `273773cd`) rather than once after the parse. First
thing to look at next.

**What the merged gate found, which no agent's gate had.** The four
round-three agents each built in their own devbox dir, and none of those
dirs had the ctc submodule, so none of them ever ran the suite: the first
gate of the merge was 282/305, and 16 of the 22 reds were one bug - the
script-by-script parse (`d358c1da`) applied the author sheets only after
the whole parse, so every page whose own `<script>` read
`getComputedStyle` or `offsetWidth` read an unstyled page. `eb61fe9c` and
`273773cd` apply the sheets before each parser script runs (and hand over
their `load`s ahead of the script's own, in document order). The rest:
`ta[i] = 1` on a BigInt kind is the TypeError on the fast path too
(`17345cee`); `select.options.length = n` reached no setter, `add`/`remove`
did not ask for a reset, `Range` was not under `AbstractRange`, and
`compatMode` was the install-time value (`f1613a5c`); a first script that
hit the call-stack ceiling made the second run as if nested; and five unit
expectations that the spec parser proved wrong (`ee0e3862`).

### And the wide corpus at the same SHA

**2,162 of the 5,156 that ran (41.9%); subtests 180,098 PASS, 78,842 FAIL**
- from 1,924 (37.3%) and 171,486 / 79,395 at `e29e197f` (round two, the
run of 13:31 UTC this day, which is the "before" of every round-three
agent): **+240 files, -2**. The eight top directories of
`tools/wpt/fetch-wpt.sh`'s corpus; `encoding` is dropped from the wide
run since this day - its 1,261 files time out one by one (TextDecoder is
not implemented) and held the box for 90 minutes. The css rows are
unchanged from `e29e197f` (round three touched no CSS) and are tabulated
against the 2026-09-13 run in `docs/css-conformance.md` §2-wide; the
rest, with the round-three deltas:

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files | subtests PASS / FAIL |
| `css/compositing` | **10** (+0) | 1 | 0 | 0 | 0 | 0 | 11 | 90 / 2 |
| `custom-elements` | **9** (+0) | 161 | 0 | 0 | 10 | 13 | 193 | 2,175 / 1,847 |
| `dom/abort` | **0** (+0) | 5 | 0 | 0 | 1 | 3 | 9 | 5 / 18 |
| `dom/collections` | **6** (+0) | 4 | 0 | 0 | 0 | 1 | 11 | 43 / 10 |
| `dom/events` | **71** (+0) | 17 | 3 | 0 | 2 | 85 | 178 | 651 / 54 |
| `dom/lists` | **2** (+0) | 3 | 0 | 0 | 0 | 0 | 5 | 172 / 17 |
| `dom/nodes` | **244** (+0) | 45 | 16 | 2 | 2 | 53 | 362 | 12,054 / 734 |
| `dom/ranges` | **24** (+1) | 37 | 1 | 0 | 3 | 8 | 73 | 34,349 / 7,014 |
| `dom/traversal` | **14** (+6) | 4 | 0 | 0 | 0 | 0 | 18 | 1,579 / 29 |
| `domparsing` | **10** (+0) | 48 | 0 | 0 | 11 | 3 | 72 | 231 / 1,341 |
| `html/dom` | **147** (+12) | 73 | 11 | 0 | 8 | 137 | 376 | 60,498 / 150 |
| `html/semantics/forms` | **138** (+100) | 165 | 21 | 1 | 8 | 315 | 648 | 2,824 / 1,789 |
| `html/syntax` | **28** (+10) | 236 | 3 | 0 | 1 | 43 | 311 | 2,657 / 6,034 |
| `html/webappapis` | **122** (+82) | 114 | 61 | 1 | 11 | 40 | 349 | 620 / 378 |
| `selection` | **0** (+0) | 46 | 2 | 0 | 48 | 87 | 183 | 17 / 280 |
| `shadow-dom` | **49** (+3) | 110 | 11 | 0 | 12 | 163 | 345 | 350 / 8,410 |
| `url` | **22** (+21) | 25 | 1 | 0 | 1 | 0 | 49 | 5,400 / 4,534 |
| **total** | **2162** | 2514 | 154 | 4 | 322 | 2439 | 7595 | 180,098 / 78,842 |

Agent F is `html/semantics/forms` 38 -> 138 and `dom/traversal` 8 -> 14;
D is `html/webappapis` 40 -> 122 (dynamic-markup-insertion: `document.
open/write/close`, the parser stopping at every `</script>`) and
`html/syntax` 18 -> 28 (236 of its 311 files still FAIL - the html5lib
`parsing/` variants, one file per fixture group, where any case failing
fails the file; the tree builder itself passes 1,723 of the 1,842 fixture
cases in `unittests/unit/html5lib_fixtures`); U is `url` 1 -> 22 (the
`urlsearchparams-*.any.js` files, `url-constructor`, `url-origin`,
`url-tojson`; the 27 still failing are the `<a>`/`<area>` half -
`a-element*` and `url-setters-a-area*`, whose `href`/`username`/`origin`
do not go through the URL record yet - `IdnaTestV2` (1,276 subtests, the
rest of UTS #46), four files on `iterator.next is not a function` over a
URLSearchParams iterator handed to `new URLSearchParams`, and
`TextEncoder`/`XMLHttpRequest` being undefined). The two lost:
`domparsing/DOMParser-parseFromString-html.html` ("must be parsed with
scripting disabled, so noscript works": the new tree builder parses a
DOMParser document with scripting ON, so `<noscript>` content is raw text
where the test wants a `<p>` child - 13.2.6.4.4's scripting flag needs to
follow the document, not the process) and `viewport-units-invalidation`
(above).

## The baseline — 2026-09-16, the five suites after the round-one merges

**740 of the 1,102 that ran (67.2%); subtests 45,822 PASS, 2,025 FAIL** -
from 788 (72.3%) and 69,993 / 2,684 at `7f9211d0` (three reflection pages alone are 23,309 of the subtest drop - point 2 below): **63 files lost, 15 gained**, and
the loss is the instrument getting honest rather than the engine getting
worse. Engine at `9c70aaa0` on `ctbrowser-wpt` (`ctcompile-v1` `09341902`
with the audit, plus the four round-one branches: T typed arrays, P promise/
proxy/iterator, W dom/html, E `test/language`), same run as the test262 row
of the same SHA in `docs/test262.md`; the five suites, 4 workers,
`CTBROWSER_GL_DRIVER=deterministic`, 4 GB cap. The before column is the wide
run at `7f9211d0`, which used the same corpus checkout (`3f6b09ae3e`).

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `css/css-values` | **153** (-5) | 98 | 15 | 0 | 5 | 237 | 508 | 5,510 / 1,015 |
| `css/cssom` | **144** (-3) | 38 | 0 | 0 | 10 | 29 | 221 | 1,604 / 95 |
| `dom/events` | **71** (-10) | 11 | 7 | 0 | 2 | 85 | 176 | 627 / 22 |
| `dom/nodes` | **241** (-12) | 45 | 21 | 0 | 2 | 53 | 362 | 12,045 / 740 |
| `html/dom` | **131** (-18) | 74 | 14 | 0 | 20 | 137 | 376 | 26,036 / 153 |
| **total** | **740** | 266 | 57 | 0 | 39 | 541 | 1643 | 45,822 / 2,025 |

**What the 63 lost files are.** Read one by one against the `7f9211d0`
JSON, they are two things, neither a regression in the engine:

1. **A false pass the whole instrument had, found and gone.** Every
   `promise_test` whose body threw or returned a rejected promise was
   reported PASS by testharness.js on this engine - a probe page confirms
   it against Codex's build of `09341902`: six `promise_test`s that throw
   synchronously, after an `await`, after a timer, after a rAF, or return a
   rejection, all six PASS there and all six FAIL at `9c70aaa0`. Agent P's
   Promise rework (`20f17da4`: two-tick thenable resolution, rejection
   tracking per 27.2) is what makes the rejection reach the harness's
   `.then(_, fail)`. So `dom/events/scrolling/scroll-event-fired-to-*`
   (`scrollTop` is not implemented - the old PASS asserted on it),
   `dom/nodes/moveBefore/focus-preserve` (`setSelectionRange` is not
   implemented), the `moveBefore/continue-css-*` and `webkit-animation-*`
   TIMEOUTs (they wait on animation events nothing fires), the
   `html/dom/partial-updates` and `render-blocking` files, `css/cssom/
   idlharness` and the rest of the list were never passing. The subtest
   counts of every earlier row in this file are inflated by the same
   amount: any async test that failed was counted as a pass.
2. **The runner gave up on a busy page after one second.** `send_command`'s
   one-second socket timeout escaped as "driver listened but answered no
   command - the page never yielded", at 1.1-2.0 s, for 18 files whose load
   script runs longer than that: `html/dom/reflection-{text,forms,embedded,
   tabular}.html` (10,000-subtest pages), the seven `calc-size/animation`
   and `animations/calc-interpolation` files, `random-computed`, and the
   six `NodeList-static-length-getter-tampered` files. Fixed in `41a285dc`
   (the recv retries until the deadline): re-run alone, `reflection-text`
   is PASS with 10,202 subtests in 1.1 s. `NodeList-static-length-getter-
   tampered-*` then hit the VM's 40,000,000-object allocation ceiling - a
   `NodeList` index read allocates, and the test reads 250 million of them -
   which is a real finding and the next thing to look at in `dom/nodes`.
   The `reflection-text.html` TIMEOUT `docs/plans/wpt-next.md` §2 chased
   "since the audit" was this, not the audit.

**The 15 gained** are agent W's (`Element-children`, the four
`getElementsByClassName-2x`, `MutationObserver-textContent`, `name-
validation`, `processing-instruction-attributes`, `Node-appendChild-
cereactions-vs-script`, `insertion-removing-steps/Node-appendChild-script-
and-iframe`) and the cascade's (`getComputedStyle-pseudo*`, `cssstyledeclaration-
csstext`, `css-style-reparse`, `computed-style-005`, `line-break-ch-unit`,
`viewport-units-*`).

**Next, in this order:** re-measure the five suites and the wide corpus
with the fixed runner behind a green gate (this SHA's gate stopped in
ctcompile's `map_flow` pipeline fixture on agent E's `__home` property,
fixed in `5865c08b`); then `scrollTop`/`scrollLeft` and the scroll event
(cssom-view, 8 files here and 40-odd in `css/cssom-view`); the
`NodeList` index allocation; `URLSearchParams` (12 HARNESS_ERRORs in
`html/dom`); `FontFace` (3).

## The baseline — 2026-09-13, the widened corpus

**1,799 of the 5,099 tests that ran, which is 35.3%**, and not one crash.
The first measurement of the corpus widened on 2026-09-13 (`c643cba8`,
`24b9b9c6`; the corpus section below says what came in and why): every CSS
module's parsing, inheritance and animation tests, and twenty more suites.
Engine at `7f9211d0` on `ctbrowser-wpt` — `b346dc0b` plus the morning's
commits (the frame layout, `ch`, the expando rule, used margins, the
`::highlight()` cascade, media-query units); one run per top-level directory
on the devbox, 4 workers, `CTBROWSER_GL_DRIVER=deterministic`, 4 GB cap, the
same instrument as every row below. A suite's row is the tests under its
path; the five suites of the earlier rows are in here unchanged and
comparable (`css/css-values` 158, `css/cssom` 147, `dom/nodes` 253,
`dom/events` 81, `html/dom` 149 - **788 of 1,090, 72.3%**).

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `css/compositing` | **3** | 8 | 0 | 0 | 0 | 0 | 11 | 7 / 85 |
| `css/css-align` | **21** | 36 | 0 | 0 | 0 | 0 | 57 | 593 / 728 |
| `css/css-anchor-position` | **0** | 10 | 0 | 0 | 0 | 0 | 10 | 41 / 179 |
| `css/css-animations` | **10** | 32 | 0 | 0 | 0 | 1 | 43 | 212 / 378 |
| `css/css-backgrounds` | **32** | 71 | 4 | 0 | 0 | 45 | 152 | 1,411 / 2,876 |
| `css/css-box` | **25** | 15 | 0 | 0 | 0 | 0 | 40 | 300 / 627 |
| `css/css-break` | **8** | 14 | 0 | 0 | 0 | 0 | 22 | 91 / 449 |
| `css/css-cascade` | **25** | 52 | 1 | 0 | 0 | 62 | 140 | 400 / 325 |
| `css/css-color` | **11** | 51 | 2 | 0 | 0 | 257 | 321 | 1,996 / 5,702 |
| `css/css-color-adjust` | **1** | 6 | 0 | 0 | 0 | 0 | 7 | 16 / 131 |
| `css/css-color-hdr` | **0** | 1 | 0 | 0 | 0 | 0 | 1 | 0 / 2 |
| `css/css-conditional` | **5** | 35 | 1 | 0 | 172 | 204 | 417 | 1,295 / 501 |
| `css/css-contain` | **1** | 4 | 0 | 0 | 0 | 0 | 5 | 14 / 31 |
| `css/css-content` | **1** | 4 | 0 | 0 | 0 | 0 | 5 | 91 / 117 |
| `css/css-display` | **1** | 7 | 0 | 0 | 0 | 0 | 8 | 142 / 218 |
| `css/css-exclusions` | **0** | 1 | 0 | 0 | 0 | 0 | 1 | 0 / 4 |
| `css/css-flexbox` | **23** | 11 | 0 | 0 | 0 | 1 | 35 | 422 / 567 |
| `css/css-fonts` | **24** | 73 | 3 | 0 | 0 | 0 | 100 | 1,423 / 2,446 |
| `css/css-forced-color-adjust` | **1** | 3 | 0 | 0 | 0 | 0 | 4 | 6 / 8 |
| `css/css-forms` | **0** | 3 | 0 | 0 | 0 | 0 | 3 | 1 / 51 |
| `css/css-gaps` | **18** | 54 | 2 | 0 | 0 | 2 | 76 | 325 / 2,477 |
| `css/css-grid` | **13** | 57 | 2 | 0 | 2 | 3 | 77 | 362 / 1,951 |
| `css/css-images` | **8** | 23 | 0 | 0 | 0 | 0 | 31 | 744 / 2,472 |
| `css/css-inline` | **6** | 14 | 0 | 0 | 0 | 0 | 20 | 78 / 255 |
| `css/css-link-params` | **0** | 1 | 0 | 0 | 0 | 0 | 1 | 0 / 2 |
| `css/css-lists` | **8** | 17 | 0 | 0 | 0 | 0 | 25 | 248 / 250 |
| `css/css-logical` | **29** | 33 | 0 | 0 | 0 | 0 | 62 | 520 / 246 |
| `css/css-masking` | **10** | 32 | 6 | 0 | 0 | 1 | 49 | 126 / 2,821 |
| `css/css-multicol` | **11** | 28 | 0 | 0 | 0 | 0 | 39 | 78 / 1,392 |
| `css/css-nesting` | **1** | 21 | 0 | 0 | 0 | 23 | 45 | 9 / 108 |
| `css/css-overflow` | **12** | 24 | 0 | 0 | 0 | 0 | 36 | 165 / 214 |
| `css/css-overscroll-behavior` | **1** | 3 | 0 | 0 | 0 | 0 | 4 | 15 / 52 |
| `css/css-page` | **4** | 8 | 0 | 0 | 0 | 0 | 12 | 31 / 42 |
| `css/css-paint-api` | **0** | 0 | 0 | 0 | 0 | 1 | 1 | 0 / 0 |
| `css/css-position` | **21** | 11 | 0 | 0 | 0 | 0 | 32 | 545 / 516 |
| `css/css-properties-values-api` | **0** | 9 | 0 | 0 | 60 | 0 | 69 | 0 / 9 |
| `css/css-pseudo` | **0** | 4 | 0 | 0 | 1 | 0 | 5 | 57 / 226 |
| `css/css-rhythm` | **5** | 10 | 0 | 0 | 0 | 0 | 15 | 55 / 100 |
| `css/css-ruby` | **4** | 5 | 0 | 0 | 0 | 0 | 9 | 26 / 21 |
| `css/css-scroll-anchoring` | **1** | 3 | 0 | 0 | 0 | 0 | 4 | 2 / 6 |
| `css/css-scroll-snap` | **7** | 19 | 0 | 0 | 0 | 0 | 26 | 183 / 290 |
| `css/css-scrollbars` | **0** | 1 | 0 | 0 | 0 | 0 | 1 | 0 / 4 |
| `css/css-shapes` | **7** | 11 | 3 | 0 | 0 | 0 | 21 | 66 / 481 |
| `css/css-size-adjust` | **1** | 4 | 0 | 0 | 0 | 0 | 5 | 4 / 211 |
| `css/css-sizing` | **11** | 23 | 3 | 0 | 0 | 0 | 37 | 1,033 / 941 |
| `css/css-syntax` | **15** | 24 | 0 | 0 | 1 | 8 | 48 | 291 / 138 |
| `css/css-tables` | **14** | 3 | 0 | 0 | 0 | 0 | 17 | 40 / 127 |
| `css/css-text` | **36** | 61 | 0 | 0 | 0 | 0 | 97 | 556 / 1,376 |
| `css/css-text-decor` | **12** | 23 | 0 | 0 | 0 | 0 | 35 | 185 / 1,007 |
| `css/css-transforms` | **7** | 55 | 4 | 0 | 0 | 27 | 93 | 336 / 3,396 |
| `css/css-transitions` | **9** | 21 | 0 | 0 | 0 | 0 | 30 | 299 / 610 |
| `css/css-ui` | **21** | 31 | 0 | 0 | 0 | 0 | 52 | 558 / 836 |
| `css/css-values` | **158** | 100 | 8 | 0 | 5 | 237 | 508 | 6,274 / 1,704 |
| `css/css-variables` | **20** | 28 | 12 | 0 | 1 | 186 | 247 | 392 / 176 |
| `css/css-view-transitions` | **3** | 9 | 0 | 0 | 0 | 0 | 12 | 80 / 944 |
| `css/css-viewport` | **0** | 0 | 0 | 0 | 0 | 1 | 1 | 0 / 0 |
| `css/css-will-change` | **1** | 3 | 0 | 0 | 0 | 0 | 4 | 127 / 45 |
| `css/css-writing-modes` | **10** | 6 | 0 | 0 | 0 | 0 | 16 | 36 / 16 |
| `css/cssom` | **147** | 35 | 0 | 0 | 10 | 29 | 221 | 1,618 / 81 |
| `css/cssom-view` | **44** | 157 | 3 | 0 | 13 | 22 | 239 | 485 / 1,489 |
| `css/fill-stroke` | **0** | 3 | 0 | 0 | 0 | 0 | 3 | 0 / 368 |
| `css/filter-effects` | **4** | 24 | 0 | 0 | 0 | 0 | 28 | 143 / 2,267 |
| `css/mediaqueries` | **5** | 25 | 0 | 0 | 0 | 63 | 93 | 834 / 864 |
| `css/motion` | **7** | 33 | 3 | 0 | 0 | 4 | 47 | 172 / 3,120 |
| `css/selectors` | **105** | 95 | 1 | 0 | 10 | 313 | 524 | 4,095 / 1,363 |
| `custom-elements` | **37** | 127 | 0 | 0 | 16 | 13 | 193 | 2,717 / 1,236 |
| `dom/abort` | **0** | 5 | 0 | 0 | 1 | 3 | 9 | 5 / 18 |
| `dom/collections` | **6** | 4 | 0 | 0 | 0 | 1 | 11 | 42 / 11 |
| `dom/events` | **81** | 7 | 1 | 0 | 2 | 85 | 176 | 676 / 13 |
| `dom/lists` | **2** | 3 | 0 | 0 | 0 | 0 | 5 | 172 / 17 |
| `dom/nodes` | **253** | 44 | 10 | 0 | 2 | 53 | 362 | 12,062 / 739 |
| `dom/ranges` | **16** | 37 | 7 | 0 | 3 | 9 | 72 | 1,418 / 9,294 |
| `dom/traversal` | **8** | 10 | 0 | 0 | 0 | 0 | 18 | 728 / 880 |
| `domparsing` | **27** | 30 | 0 | 0 | 12 | 3 | 72 | 943 / 625 |
| `encoding` | **12** | 40 | 1 | 0 | 1 | 136 | 190 | 371 / 1,616 |
| `html/dom` | **149** | 67 | 3 | 0 | 8 | 138 | 365 | 49,363 / 147 |
| `html/semantics/forms` | **58** | 236 | 10 | 0 | 27 | 316 | 647 | 1,297 / 2,949 |
| `html/syntax` | **22** | 63 | 133 | 0 | 3 | 47 | 268 | 2,325 / 573 |
| `html/webappapis` | **47** | 198 | 52 | 0 | 12 | 40 | 349 | 479 / 405 |
| `selection` | **8** | 32 | 0 | 0 | 46 | 91 | 177 | 76 / 199 |
| `shadow-dom` | **74** | 82 | 6 | 0 | 20 | 163 | 345 | 380 / 1,456 |
| `url` | **11** | 16 | 1 | 0 | 1 | 5 | 34 | 31 / 461 |
| **total** | **1799** | 2589 | 282 | 0 | 429 | 2593 | 7692 | 102,739 / 71,078 |

Subtests: **102,739 PASS, 71,078 FAIL, 908 NOTRUN, 139 TIMEOUT.**

**What the number says.** The five old suites measure what the last two
weeks worked on and stand at 72%; the new ones measure what nobody has
touched, and the whole is 35%. By what stands in front of the most tests:

- **The CSS value grammar** — the `parsing/` and `inheritance.html` files
  over 60 modules are 24,411 subtests, 8,460 PASS. By property, the failing
  subtests are `color` 5,271 (CSS Color 4/5: the modern syntax, `color()`,
  `color-mix()`, `lab`/`lch`/`oklab`/`oklch`, relative colours - the
  cascade's `properties/color.cpp` is a syntax check and `paint::parse_color`
  means a colour), `background-image` 2,160 (gradients), `font` 318, the
  grid track lists 209+203+112+89+81+68, `display` 186, `offset-path` 139,
  `content` 135, `filter` 116, `clip-path` 99, `mask` 87 ... 211 of the
  415 properties the sweep tests are not in the property table at all,
  and since `f08483ca` an unsupported name on `el.style` is an expando, so
  every one of their `test_valid_value`s fails honestly instead of by echo.
- **CSS Animations and CSS Transitions do not exist** - only Web Animations
  does. `css/support/interpolation-testcommon.js` drives every `animation/`
  file four ways and the "CSS Transitions" and "CSS Animations" rows all fail:
  `css-transforms` 3,396 failing subtests, `motion` 3,120, `masking` 2,821,
  `backgrounds` 2,876, `filter-effects` 2,267, `images` 2,472, `gaps` 2,477,
  `transitions` 610, `animations` 378, and `css-values/animations/
  calc-interpolation` 103 - the largest subtest cluster in the corpus, and
  one feature.
- **Container queries** - 171 of `css-conditional`'s 417 files are
  `assert_implements` HARNESS_ERRORs on `@container` (size 110, scroll-state
  41, style 20); `css-nesting` is 1 of 22 files; `css-cascade` 25 of 78
  (`@layer`, `@scope`, `revert-layer`).
- **`css/cssom-view`** 44 of 204: `getBoundingClientRect`, `scrollIntoView`,
  `elementFromPoint`, `scroll*` on the element and the window, `matchMedia`
  events.
- **`html/syntax`** 22 of 221, with **133 TIMEOUTs** - and the TIMEOUTs are
  not the parser: 126 of them are `speculative-parsing/`, which needs a WPT
  server (`stash.py`) and can never finish here; the next row of this file
  drops that directory from the checkout. The html5lib tree-construction
  fixtures (`parsing/html5lib_write.html` and its siblings, one `.dat` per
  `<meta name=variant>`) were SKIPPED by the runner at this row, which opened
  a file and had no query string to hand a variant - so the DOM's own tree
  builder was not measured here. The runner runs variants since `ctdrive
  --query` (the row above this one is the first with them).
- **`html/webappapis`** 47 of 309 (52 TIMEOUTs), **`html/semantics/forms`**
  58 of 331, **`dom/ranges`** 16 of 63 (9,294 failing subtests: `Range`
  mutations, `extractContents`/`cloneContents`/`deleteContents`),
  **`custom-elements`** 37 of 180, **`shadow-dom`** 74 of 182,
  **`selection`** 8 of 86 (46 HARNESS_ERRORs), **`encoding`** 12 of 54
  (TextDecoder for anything but UTF-8), **`url`** 11 of 29 (the WHATWG
  parser over `urltestdata.json`; `docs/plans/ada-url.md`).

Against `b346dc0b` on the five old suites: +10 files
(`line-break-ch-unit`, `viewport-units-invalidation`,
`viewport-units-scrollbars-mq-001`, `random-in-container-query`,
`random-item-in-container-query`, `computed-style-005`,
`cssstyledeclaration-csstext`, `getComputedStyle-pseudo`,
`getComputedStyle-pseudo-with-argument`, `Element-children`), -6:
`calc-size-parsing` and `signs-abs-computed` (the logical `*-block-size` /
`*-inline-size` and `image-resolution` / `grid-template-rows` were
unsupported names passing by echo - now expandos, and the honest answer
until the table has them), `viewport-units-gutter-003`/`-004`
(`width: 100vw` on a page whose root is `overflow-y: scroll`: the page's `vw`
now excludes the 15px scrollbar, `resolve_styles`, which is the rule - but
the test measures the scrollbar's width off an `overflow: scroll` element as
`offsetWidth - clientWidth`, which is 0 here because an element's scrollbar
reserves no space in this layout, so it expects the full 1024 and gets 1009:
element scrollbar gutters are the layout question), `getComputedStyle-detached-subtree` (a
`display: none` frame's document answers initial values instead of the empty
declaration since `getComputedStyle` routes to the frame's bindings - being
fixed), `serialize-values` (`baseline-shift: .5%` is an unsupported name
now: an expando keeps the author's `.5%`). `reflection-text.html` is still
the TIMEOUT named in the row below.

test262 at the same commit: unchanged from `b346dc0b`, **32,295 of 48,624**.

## The baseline — 2026-09-13, small hours

**784 of the 1,090 tests that ran, which is 71.9%**, and still not one crash.
Same instrument, engine at commit `b346dc0b` on `ctbrowser-wpt` - the merge of
Codex's `ctcompile-v1` `3e803401` into the evening's 88 audit commits (about
-8,000 lines) and agents J/V/D/M; five suites one after another on the devbox,
4 workers, `CTBROWSER_GL_DRIVER=deterministic`, 4 GB cap.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 252 | 45 | 10 | 0 | 2 | 53 | 362 |
| `dom/events` | 81 | 7 | 1 | 0 | 2 | 85 | 176 |
| `html/dom` | 149 | 67 | 3 | 0 | 8 | 138 | 365 |
| `css/cssom` | 145 | 37 | 0 | 0 | 10 | 29 | 221 |
| `css/css-values` | 157 | 97 | 8 | 0 | 9 | 237 | 508 |
| **total** | **784** | **253** | **22** | **0** | **31** | **542** | **1,632** |

Subtests: **69,851 PASS, 2,805 FAIL, 67 NOTRUN, 15 TIMEOUT.**

Against `00b5ab38` (the row below): **+32 files, 3 lost.** `css/css-values`
144 -> 157 (agent V: `attr-css-wide-keywords`, `attr-cycle`, the four
`calc-*-serialize` and `calc-nesting-002`, `calc-rounds-to-integer`,
`ident-function-computed`, `inherit-function-basic`, the three
`minmax-*-serialize`, `random-in-custom-function`, `random-in-if`),
`css/cssom` 140 -> 145 (agent M: `getComputedStyle-detached-subtree`,
`-resolved-colors`, `-sticky-pos-percent`, `mediaquery-sort-dedup`,
`ttwf-cssom-doc-ext-load-count`), `dom/nodes` 244 -> 252 (agent D: the
`node-realm-*` and `node-creation-realm` files, `Element-matches`,
`Node-isConnected`, `NodeList-live-mutations`), `html/dom` 146 -> 149
(`document.forms`, `nameditem-names`, `document.title-not-in-html-svg`,
`lang-attribute-document-element-replacement`).

**The three lost are all the audit's, and named so they are fixed rather
than re-baselined:** `html/dom/reflection-text.html` is a TIMEOUT - "the
page never yielded", an infinite loop during load in a file that completed
in under a second the evening before - which is the **-10,138 subtests** in
the row (79,714 -> 69,851 is that one file); `dom/nodes/Element-children.html`
reads `length`, `item` and `namedItem` as OWN enumerable properties of an
HTMLCollection (the `Object.keys` edge case) since the collection's methods
moved; `dom/nodes/MutationObserver-characterData.html` reports a processing
instruction's `oldValue` as its data without the `<?target ...?>` wrapper.

test262 at the same commit, and for the first time the WHOLE corpus:
**32,295 of 48,624 (66.4%)**; the ten areas the old table had are 28,397 of
32,927 (86.2%), from 81.5% - `docs/test262.md`, the `b346dc0b` row.

## The baseline — 2026-09-12, night

**755 of the 1,090 tests that ran, which is 69.3%**, and still not one crash.
Same instrument, engine at commit `00b5ab38` on `ctbrowser-wpt` — browser gate
540/541 at that commit (the one red is `ctcompile_lit`'s `global-maps.test`,
which Codex's `f57cab15` repairs and which is merged in as `aeb72dfb`); five
suites one after another on the devbox, 4 workers, `CTBROWSER_GL_DRIVER=deterministic`,
4 GB cap.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 244 | 53 | 10 | 0 | 2 | 53 | 362 |
| `dom/events` | 81 | 7 | 1 | 0 | 2 | 85 | 176 |
| `html/dom` | 146 | 71 | 2 | 0 | 8 | 138 | 365 |
| `css/cssom` | 140 | 42 | 0 | 0 | 10 | 29 | 221 |
| `css/css-values` | 144 | 110 | 8 | 0 | 9 | 237 | 508 |
| **total** | **755** | **283** | **21** | **0** | **31** | **542** | **1,632** |

Subtests: **79,714 PASS, 3,144 FAIL, 67 NOTRUN, 15 TIMEOUT.**

Against `15f47064` (the row below): **+40 files, 1 lost.** Per suite:
`css/css-values` 125 -> 144 (agent F: `calc-size-parsing`, `calc-mix-computed`,
`random-item-computed`, `random-serialize`, the three `url-request-modifiers-*`,
`sibling-function-invalidation`, `inherit-function-invalidation`, `if-conditionals`,
`calc-angle-values`, `attr-pseudo-elem-invalidation`, the eight
`viewport-units-gutter-00x`), `html/dom` 135 -> 146 (nine `the-lang-attribute-0xx`
files and `document-lastModified-01` from `offsetWidth` becoming an accessor
that flushes layout, `ef1f2464`; `name-content-attribute-and-property` back
with `common.js` in the corpus), `css/cssom` 133 -> 140 (`CSSStyleRule-set-
selectorText-namespace`, `computed-style-set-property`, the two
`cssstyledeclaration-*custom-properties`, `flex-serialization`,
`getComputedStyle-pseudo-checkmark`, `serialize-custom-props`), `dom/nodes`
242 -> 244 (`Document-` and `Element-getElementsByTagName`). **The one lost:**
`css/css-values/lh-unit-003.html` — `width: 10lh` on a ten-line box read
through the new synchronous `offsetWidth` flush is 50 by 250 where the frame's
layout, which the old copied number came from, was 250 by 250: the
synchronous flush and the frame disagree about that box, which is a layout
question rather than a units one (the second subtest, after the font loads,
still passes).

test262 at the same commit: **26,833 of 32,927 (81.5%)**, from 76.1% —
`docs/test262.md`, the `00b5ab38` row, including the 53 files it lost and why.

## The baseline — 2026-09-12, evening

**716 of the 1,090 tests that ran, which is 65.7%**, and still not one crash.
Same instrument, engine at commit `15f47064` on `ctbrowser-wpt` — browser gate
540/540 at that commit; five suites run one after another on the devbox, 4
workers, `CTBROWSER_GL_DRIVER=deterministic`, 4 GB cap, as every row below.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 242 | 55 | 10 | 0 | 2 | 53 | 362 |
| `dom/events` | 81 | 7 | 1 | 0 | 2 | 85 | 176 |
| `html/dom` | 135 | 81 | 2 | 0 | 9 | 138 | 365 |
| `css/cssom` | 133 | 49 | 0 | 0 | 10 | 29 | 221 |
| `css/css-values` | 125 | 126 | 11 | 0 | 9 | 237 | 508 |
| **total** | **716** | **318** | **24** | **0** | **32** | **542** | **1,632** |

Subtests: **78,570 PASS, 3,578 FAIL, 67 NOTRUN, 10 TIMEOUT.**

Against `d27d8f36` (the row below): **+129 files, 1 lost.** Per suite:
`dom/nodes` 175 -> 242 (agents D then E, 67 files: `Document-createElement`,
`Document-createElementNS`, `Document-importNode`, `Element-classlist`,
`Element-children`, `CharacterData-surrogates`, `DocumentFragment-getElementById`
among them), `dom/events` 72 -> 81 (`Event-dispatch-bubbles-*`,
`shadow-relatedTarget`, `mouse-event-retarget`, `Body-FrameSet-Event-Handlers`),
`html/dom` 114 -> 135 (agents H then E: the seven `reflection-*` files are
PASS now, `nameditem-*`, `document.title`, `document.body`, `document-dir`),
`css/cssom` 116 -> 133 (agent C: `shorthand-serialization`,
`cssstyledeclaration-csstext-*`, `CSSStyleRule-set-selectorText`,
`page-descriptors`, `variable-names`), `css/css-values` 111 -> 125 (agent S:
`attr-argument-grammar`, `if-*`, `random-item-*`, `round-mod-rem-computed`,
`rem-unit-root-element`). The one lost: `html/dom/elements/name-content-attribute-and-property.html`
is HARNESS_ERROR because `/html/resources/common.js` was not in the sparse
corpus — an instrument gap, `fetch-wpt.sh` carries the file since `4a21438a`
and the next row has it back. The engine side of the evening is mostly in the
script tier (`docs/test262.md`, the `15f47064` row) and shows here as subtests:
`dom/nodes` 11,889 PASS from 10,455.

test262 at the same commit: **25,051 of 32,927 (76.1%)**, from 58.3%.

## The baseline — 2026-09-12, late

**588 of the 1,090 tests that ran, which is 53.9%**, and still not one crash.
Same instrument, engine at commit `d27d8f36` on `ctbrowser-wpt` — browser gate
187/187 at that commit. The row below (`b570bd29`, three hours earlier) is
where the day's engine work was first measured; this one adds agent E's
html/dom + dom/events work and one line in the INSTRUMENT.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 175 | 117 | 14 | 0 | 3 | 53 | 362 |
| `dom/events` | 72 | 14 | 3 | 0 | 2 | 85 | 176 |
| `html/dom` | 114 | 102 | 3 | 0 | 8 | 138 | 365 |
| `css/cssom` | 116 | 66 | 0 | 0 | 10 | 29 | 221 |
| `css/css-values` | 111 | 132 | 10 | 0 | 18 | 237 | 508 |
| **total** | **588** | **431** | **30** | **0** | **41** | **542** | **1,632** |

Subtests: **75,010 PASS, 6,992 FAIL, 212 NOTRUN, 25 TIMEOUT.**

**THE REFLECTION TIMEOUTS WERE THE HARNESS.** `tools/wpt/testharnessreport.js`
built its result JSON with `json += piece` per subtest, which in this engine
copies the whole string each time: 8,000 subtests is 12 GB of copies, and the
driver's 4 GB cap killed the seven `html/dom/reflection-*.html` files before
they could report. Three sessions blamed the collector (the in-turn
collection of `05ece7bc` is still right, and still needed for other
files). It joins pieces now (`d27d8f36`); the seven files complete in under a
second each and are FAIL, not TIMEOUT — reflection-embedded 8,446/8,922,
forms 7,911/8,271, grouping 5,326/5,358, misc 4,781/4,877, sections
5,346/5,604, tabular 6,106/6,116, text 10,138/10,202 — which is **+48,000
passing subtests** and the whole of the html/dom subtest jump (8,658 ->
57,847). Every number in the table is comparable with the rows below except
that one column; the fix is in the instrument, not the engine, and is named
here so nobody reads it as either.

Against `b570bd29`: +3 files (`HTMLStyleElement-load-event`,
`Event-dispatch-click` — a TIMEOUT before — and `src-cancel`), no file lost;
`dom/nodes` +704 subtests and `dom/events` +3 from agent E's frames work (a
frame's `contentWindow` is a proxy over the page's globals, so
`windowFor(root).DOMException` IS the DOMException constructor and every
`assert_throws_dom` inside an iframe document passes: `ParentNode-querySelector-All`
1,671 -> 1,949, `Element-matches` 599 -> 668, `Document-createElementNS` 375 ->
595), plus `javascript:` links, `queueMicrotask`, `<input type=image>`
submitting, a sheet's `load` before the window's, and twenty ARIA attributes
as nullable enumerated reflections (`aria-attribute-reflection-enumerated`
562 -> 1,696 of 1,722).

test262 at the same commit: **19,190 of 32,927 (58.3%)** — the `b570bd29`
table in `docs/test262.md` plus `Function` +5 and `Array` +6 (the two
`fromAsync` crashes fixed, the `String(class)` span).
