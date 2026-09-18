# web-platform-tests

**The suite every browser is measured against, pointed at this one.** WPT is the
shared conformance corpus for HTML, the DOM, CSS and the rest of the platform;
Chrome, Firefox and WebKit all run it, and their scores are public. This is what
it says about ctbrowser.

It is a MEASURING INSTRUMENT, not a pass/fail gate on the whole engine. The
engine is a long way from passing WPT and that is not news — what the instrument
is for is knowing *how far*, in numbers, per suite, and noticing the day one of
them moves.

    tools/wpt/fetch-wpt.sh                     the corpus, once
    tools/wpt/run-wpt.py --selftest            prove the harness works
    tools/wpt/run-wpt.py --dir dom/nodes       one directory, one table

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
translation) - was always there and is measured now. The wide corpus row
for `b722aa41` was still running when session 19 ended (results land in
`/tmp/w-b722aa41/` on the WSL box; tally with `wtally.py`). test262 at the
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

## The baseline — 2026-09-12, afternoon

**585 of the 1,090 tests that ran, which is 53.7%**, and still not one crash.
Same instrument (WPT `3f6b09ae`, four workers, 4 GB `ulimit -v`,
`CTBROWSER_GL_DRIVER=deterministic`), engine at commit `b570bd29` on
`ctbrowser-wpt` — browser gate 186/186 at that commit. The day started at
491/1,090 = 45.0%, re-measured at `0e5cfbef` (identical to the 09-10 row, so
the audit cuts moved nothing).

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 175 | 117 | 14 | 0 | 3 | 53 | 362 |
| `dom/events` | 71 | 14 | 4 | 0 | 2 | 85 | 176 |
| `html/dom` | 113 | 95 | 10 | 0 | 9 | 138 | 365 |
| `css/cssom` | 115 | 67 | 0 | 0 | 10 | 29 | 221 |
| `css/css-values` | 111 | 132 | 10 | 0 | 18 | 237 | 508 |
| **total** | **585** | **425** | **38** | **0** | **42** | **542** | **1,632** |

Subtests: **25,113 PASS, 7,537 FAIL, 212 NOTRUN, 26 TIMEOUT.**

(The run's JSON also holds 14 files under `html/dom/agentE-tmp/` that an agent
left in the corpus checkout on the devbox while iterating on the reflection
tests; they are excluded above and the corpus checkout was cleaned.)

Against `0e5cfbef`: **+94 files, +7,522 passing subtests**, FAIL subtests
8,455 -> 7,537. Per suite: `dom/nodes` +38, `css/cssom` +29, `css/css-values`
+21, `html/dom` +4, `dom/events` +3. What did it, from the PASS-set diff and
the agents' own measurements (each on its own branch, against this JSON):

- `dom/nodes` +38: every Node/Element/ParentNode/ChildNode operation now lives
  on its interface PROTOTYPE, one native per realm with a `length`
  (`c49750cc`: `Node-constants`, `Element-remove`, `Document-createComment`,
  `*-getElementsByTagNameNS`, +3,700 subtests in `ParentNode-querySelector-All`,
  `Element-classlist`, `Element-matches` alone); the three missing node kinds —
  DocumentType, ProcessingInstruction, CDATASection — with the Document node
  owning its child list and cross-document `adoptNode`/insert-adopts
  (`055be4c3`, `d42d36ab`: `Document-doctype`, `DocumentType-literal`,
  `Document-createProcessingInstruction`, `Node-nodeName`, `Node-nodeValue`,
  `Node-isEqualNode`, `Document-adoptNode`, `rootNode`, `Node-parentElement`);
  the Selectors engine — An+B in every spelling, `[ns|attr]`, `:has()`,
  `:scope`, `:root` as the tree root only, a scoped `querySelectorAll` that
  walks only its subtree (`87f64857`: `ParentNode-querySelector-scope`,
  `ParentNode-querySelectors-namespaces`, `Element-matches-namespaced-elements`);
  and the VM: a throw crossing a native is thrown ONCE at the native's call
  site (`0f2a3ca8` — `ParentNode-append`, `ParentNode-prepend`, and every
  assertion inside a `forEach` callback inside `test()`).
- `css/cssom` +29: `@import` expanded into the cascade with `CSSImportRule`,
  shadow-root `sheet`/`styleSheets`/`adoptedStyleSheets`,
  `HTMLLinkElement.disabled` (all seven files), preferred style-sheet sets,
  `@namespace`, `selectorText` against the sheet's namespaces, constructable
  sheets' `baseURL`/`replace` (`6c68232b`), and value serialisation
  (`serialize-values`, `dd255c38`).
- `css/css-values` +21: `progress()`/`hypot()`/`exp()`/`round()` families,
  signed zero, a percentage basis for used-value math, `lh`/`rlh`/`ic`/`rex`
  /`rch` and the viewport-variant units, computed `transform` as `matrix()`,
  the `border-radius` shorthand (`dd255c38`); the three `url-font-*-negative`
  files and `viewport-units-css2-001` moved with the VM's `null.x` TypeError
  and the promise-reaction fence.
- `html/dom` +4 and `dom/events` +3: `EventListener-handleEvent`, the two
  `scroll*` event files, `stream-append-*` and `src-buffered` — the VM changes
  (async functions rejecting, throws through natives), not shell work.

**PASS -> FAIL, three, all unmaskings:** `css/cssom/property-accessors` (a
setter's throw parked past the test body's own `try` — fixed in `849c7b50`,
after this measurement); `html/dom/historical` (`document.all` and
`HashChangeEvent` are absent and reading through them is now an honest
TypeError rather than a silent undefined); `css/css-values/animations/
line-height-lh-transition` (`20lh` resolves now, and with no transitions the
end value is read). **TIMEOUT 33 -> 38**: `MutationObserver-textContent`,
`MutationObserver-cross-realm-callback-report-exception` and the six
`*-invalidation` files for the new font-relative units (`cap`, `rcap`, `rch`,
`rex`, `ric`, `rlh`) — each a restyle that does not converge, named for the
next agent in `lib/Style`.

### What is standing in front of the most tests now

| what | where | counted |
|---|---|---:|
| reading an UNRESOLVABLE name is `undefined`, not a ReferenceError — `get_global`'s row says may_throw 0, so it is an ABI change made together with Codex (proposed in the journal) | `bytecode_opcodes.def`, `run_loop.cpp` | 1,211 test262 files; every WPT `assert_throws_js(ReferenceError, ...)` |
| no strict mode at all: writes to non-writable properties, `this` in a plain call, `arguments` — the `-s.js` tests | the compiler (directive prologue) and `store_property` | ~1,000 test262 files across `-s` and `gs` |
| `for await` / the sync `for-of` do not close the iterator on `break`/throw; `.return()` runs no `finally` | `compile_for_await`, `generator_resume` | ~100 test262 files |
| the six `*-invalidation` TIMEOUTs above — a font-relative unit change on the root does not settle | `lib/Style` restyle | 6 files |
| `document.all`, `HashChangeEvent`, `MutationObserver` on `textContent` | `bindings/document`, `events/`, `mutation.cpp` | `historical`, 2 TIMEOUTs |
| the wrapper of an adopted node is not the wrapper that was adopted (identity across `adoptNode`) | `document/tree_ops.cpp` `node_from` | named in `unit/second_document` |
| `:target`; null-namespace elements (`|div`); `#eof\` tokenisation | `lib/Shell/page` -> engine hook; the DOM/bindings; `css/token.cpp` | 6 + 16 + 2 subtests |
| shorthand reconstruction in `el.style`/`cssText`; `@property` registration (`typed_arithmetic_cycle`) | `element/declarations.cpp`; `css/parser.cpp` | the `shorthand-*` files, 1 subtest |
| the seven `reflection-*.html` files now report (see the late row: it was the harness); what is left is URL reflection (`link.href`, `input.formAction`: the document has no URL under ctdrive — `browser::set_location` from the driver turns 160 subtests) and the per-row table in `element/reflection.cpp` | `element/reflection.cpp`, `browser.hpp`, `tools/ctdrive` | ~1,900 subtests |

## The baseline — 2026-09-10, late

**491 of the 1,090 tests that ran, which is 45.0%**, and still not one crash.
Same instrument (WPT `3f6b09ae`, four workers, 4 GB `ulimit -v`,
`CTBROWSER_GL_DRIVER=deterministic`), engine at commit `62945aeb` on
`ctbrowser-wpt` — browser gate 160/160 at that commit. The day started at
369/1,090 = 33.9%.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 137 | 149 | 12 | 0 | 11 | 53 | 362 |
| `dom/events` | 68 | 17 | 4 | 0 | 2 | 85 | 176 |
| `html/dom` | 109 | 99 | 10 | 0 | 9 | 138 | 365 |
| `css/cssom` | 86 | 90 | 3 | 0 | 13 | 29 | 221 |
| `css/css-values` | 91 | 164 | 4 | 0 | 12 | 237 | 508 |
| **total** | **491** | **519** | **33** | **0** | **47** | **542** | **1,632** |

Subtests: **17,591 PASS, 8,455 FAIL, 72 NOTRUN, 34 TIMEOUT.**

Against `d99ddf7b` (the night row below): +46 files, +1,078 passing
subtests, two files PASS -> FAIL. Named from the PASS-set diff:

- `dom/nodes` +5: `ChildNode-after`, `ChildNode-replaceWith`,
  `Document-createTreeWalker`, `Node-cloneNode-svg`, `Node-lookupPrefix` —
  the Node method surface following DOM 4.2 (`51aac4de`), the Document
  binding's constructor/traversal/clone work (`d049b7d4`).
- `dom/events` +2: `Event-dispatch-detached-input-and-change`,
  `Event-dispatch-single-activation-behavior` — activation behaviour moved
  into dispatch (`690e0036`).
- `html/dom` +17: the nine `translate` files, `dataset-delete` and
  `dataset-binding` (proxy `deleteProperty`/`getOwnPropertyDescriptor`,
  `3e096c88`), `dynamic-getter`, `innertext-whitespace-pre-line`,
  `multiple-text-nodes`, `getter-first-letter-marker-multicol` (innerText,
  `fce07d5d`), `document.title-06`, `sanitize-regular`.
- `css/cssom` +11: `CSS-namespace-object-class-string`, `CSSContainerRule`,
  `CSSKeyframesRule`, `CSSStyleDeclaration-iterator`,
  `cssom-fontfacerule-constructors`, `cssom-pagerule`,
  `cssstyledeclaration-cssfontrule`, `escape`, `insertRule-charset-no-index`,
  `invalid-pseudo-elements`, `rule-restrictions` — the CSSOM interface work
  (`25d8a284`) and the selector parser refusing undefined pseudos
  (`e161aa19`).
- `css/css-values` +13: `position-computed`, `round-function`,
  `clamp-partial-serialize`, `calc-numbers`, `clamp-length-computed`,
  `exp-log-compute`, `sin-cos-tan-computed`, `acos-asin-atan-atan2-computed`,
  `getComputedStyle-calc-mixed-units-001`, `calc-complex-unresolved-serialize`,
  `attr-invalidation`, `attr-length-specified`, `attr-serialization` — the
  math and `attr()` work (`2214ce24`) and number serialisation (`d611d333`).
- **`css/css-values` −2, both one subtest and both from the calc() type
  algebra in `2214ce24`**: `progress-invalid.html` — `progress(10px * 10px,
  …)` on `opacity` must be refused and now computes to `calc(0)` (a
  `<length>²` result is being accepted where the whole must be a number);
  `typed_arithmetic_cycle.html` — a custom-property cycle through typed
  arithmetic reads `20px` where `228px` is expected. Both are the next
  agent's first two items in `lib/Style/css/calc/`.

### What is standing in front of the most tests now

From the agents' own reports over this run, with the file each names.

| what | where | counted |
|---|---|---:|
| Node methods live on each WRAPPER, not on `Node.prototype` (`Node.prototype.insertBefore` is undefined), and the natives carry no `length` | `element/node_methods.cpp` `install_node_methods` / `method` lambda | 16 + 3 subtests in `dom/nodes`, and every `X.prototype.method.call` idiom |
| no `DocumentType` / `ProcessingInstruction` / `CDATASection` node kinds | `include/ctbrowser/dom/node.hpp`, the tree builder | ~60 subtests + 3 whole-file HARNESS_ERRORs (`Node-contains`, `Node-compareDocumentPosition`, `Node-properties` die in `dom/common.js`) |
| collection inside a turn — the seven `reflection-*.html` files die at the 4 GB cap because `collect_if_due` runs once per tick | `include/ctbrowser/script/vm.hpp` `safepoint()` — landed as `d0345272` and REVERTED: it turns `unit/p5_api` red (`loadBlob`'s result stops being `instanceof Blob`), i.e. the fetch -> `Response.blob()` path holds a heap value the root inventory cannot see | 7 TIMEOUTs, thousands of subtests |
| `createElement("f:oo")` splits at the colon | `element/wrapper.cpp:171` | 15 subtests of `Document-createElement` |
| a native cannot see that a throw crossed its `cx.call` — the TreeWalker filter re-entry case, and any binding that loops over callbacks | the VM; the events fence in `events/dispatch.cpp` is the pattern | `TreeWalker-acceptNode-filter` and its kin |
| `@import` is consumed and dropped; a `<style>` appended to a shadow root has no `sheet` | `style/css/parser.cpp:187`, `stylesheets/` `sync_style_sheets` | 2 HARNESS_ERRORs + 3 files in `css/cssom` |
| `font-family` unquoted serialisation, `counter()` canonicalisation, shorthand reconstruction in `cssText` | `lib/Style/css/properties` | the rest of `serialize-values` and the shorthand-* files |
| transforms computing to `matrix()`; `lh`/`cap`/`cqw` units; `composite: add/accumulate` for animations | `lib/Style`, `bindings/animations.cpp` | 14 + ~20 + 2 files in `css/css-values` |
| `javascript:` hrefs; `hashchange`; `<input type=image>` as a submit | `browser/actions.cpp` | the `<a>` rows of the activation tests |
| Web Animations render nothing (the overlay is for the object model only); no `finish`/`cancel` events | `bindings/animations.cpp` | — |

### And the two that were NOT measured

`test262` was not re-run this session, although the VM changed in ways it
scores: top-level block `let`/`const` scoping (`d99ddf7b`),
`Object.prototype.__proto__` and the proxy traps (`552a4ba0`, `3e096c88`),
`for-in` over a proxy (`e32599bf`), label chains (`65bb22ed`). The numbers in
`docs/test262.md` are therefore older than the engine; the next session should
run `ct262` before touching the VM again.

## The baseline — 2026-09-10, night

**445 of the 1,090 tests that ran, which is 40.8%**, and still not one crash.
Same instrument, engine at commit `d99ddf7b` on `ctbrowser-wpt` — browser gate
152/152 at that commit.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 132 | 153 | 13 | 0 | 11 | 53 | 362 |
| `dom/events` | 66 | 18 | 5 | 0 | 2 | 85 | 176 |
| `html/dom` | 92 | 116 | 10 | 0 | 9 | 138 | 365 |
| `css/cssom` | 75 | 101 | 3 | 0 | 13 | 29 | 221 |
| `css/css-values` | 80 | 173 | 2 | 0 | 16 | 237 | 508 |
| **total** | **445** | **561** | **33** | **0** | **51** | **542** | **1,632** |

Subtests: **16,513 PASS, 9,113 FAIL, 72 NOTRUN, 40 TIMEOUT.**

Against `8ca744a1` (the evening row): +13 files, +182 passing subtests, and
TIMEOUT 43 -> 33. Named from the PASS-set diff: `html/dom` +10 is the whole
of `render-blocking/` bar the IDL file that already passed —
`parser-blocking-script`, `parser-inserted-{async,defer,module}-script`,
`parser-inserted-{style-element,stylesheet-link}`,
`script-inserted-{script,module-script,style-element,stylesheet-link}` —
which is Paint Timing plus `load`/`error` at a sheet or script element
(`1abf9798`), the `?pipe=` query dropped before the filesystem probe
(`4e3f124c`), and the `blocking` token list (`cb6fa020`). `dom/nodes` +3 is
`ChildNode-before`, `Text-wholeText` and `insert-adjacent` — the fragment
serialisation and `textContent` on a text node (`74d61725`). Nothing went
PASS -> FAIL. **FAIL subtests rose 8,218 -> 9,113 and NOTRUN fell 727 -> 72
in the same run**: eight `dom/nodes` files that used to time out — the two
`Document-characterSet-normalization` files among them — now run to the end
under the top-level block-scoping fix (`d99ddf7b`) and report their
failures instead of NOTRUN, which is the honest number.

## The baseline — 2026-09-10, evening

**432 of the 1,090 tests that ran, which is 39.6%**, and still not one crash.
Same instrument (WPT `3f6b09ae`, four workers, 4 GB `ulimit -v`,
`CTBROWSER_GL_DRIVER=deterministic`), engine at commit `8ca744a1` on
`ctbrowser-wpt` — browser gate 151/151 at that commit.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 129 | 148 | 21 | 0 | 11 | 53 | 362 |
| `dom/events` | 66 | 18 | 5 | 0 | 2 | 85 | 176 |
| `html/dom` | 82 | 126 | 10 | 0 | 9 | 138 | 365 |
| `css/cssom` | 75 | 99 | 5 | 0 | 13 | 29 | 221 |
| `css/css-values` | 80 | 173 | 2 | 0 | 16 | 237 | 508 |
| **total** | **432** | **564** | **43** | **0** | **51** | **542** | **1,632** |

Subtests: **16,331 PASS, 8,218 FAIL, 727 NOTRUN, 48 TIMEOUT.**

Against `f830fbd3` (the morning row below): +10 files, +206 subtests. Named,
because a PASS-set diff of the two JSON files says exactly which:
`dom/events` +10 — `Event-dispatch-listener-order`, `Event-dispatch-throwing`,
`EventTarget-add-listener-platform-object`, `EventTarget-dispatchEvent`,
`synthetic-events-cancelable`, the four `webkit-*-event` files and
`window-event-restored-after-throwing-onerror` — which is the events port
(`4f3fdc50`), the listener fence actually compiling (`c282145e`), the
shadow-aware dispatch path (`8ca744a1`) and `customElements` (`1e9a8275`,
the platform-object test defines one). `dom/nodes` +1 `slotchange-events`
(customElements), `html/dom` +1 `blocking-idl-attr` (`cb6fa020`).
**`css/css-values` −2**: `interpolate-size-{max,min}-height-composition`
went PASS → FAIL because `element.animate` now exists (`8606ed50`), so their
Web Animations leg runs instead of being skipped as unsupported — they were
passing by not testing, and composition (`add`/`accumulate`) is not
implemented. The subtest count went up 95 in that suite all the same.

## The baseline — 2026-09-10, morning

**422 of the 1,090 tests that ran, which is 38.7%**, and still not one crash.
Measured on the devbox against WPT `3f6b09ae`, four workers, a 4 GB `ulimit -v`
per driver, `CTBROWSER_GL_DRIVER=deterministic`, engine at commit `f830fbd3` on
`ctbrowser-wpt` — the tip after the five red gate tests were fixed and the
CSSOM-to-cascade hook (`3fad3a99`) was installed. The browser gate at that
commit is 144 of 145: the one failure, `cssom_sheets`, pinned the trailing
space the style attribute no longer has and was corrected in `77afb315`.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 128 | 146 | 23 | 0 | 12 | 53 | 362 |
| `dom/events` | 56 | 29 | 5 | 0 | 1 | 85 | 176 |
| `html/dom` | 81 | 125 | 10 | 0 | 11 | 138 | 365 |
| `css/cssom` | 75 | 99 | 5 | 0 | 13 | 29 | 221 |
| `css/css-values` | 82 | 170 | 2 | 0 | 17 | 237 | 508 |
| **total** | **422** | **569** | **45** | **0** | **54** | **542** | **1,632** |

Subtests: **16,125 PASS, 8,405 FAIL, 729 NOTRUN, 48 TIMEOUT.**

Against the `f7e0912` row below: +53 files (369 -> 422), +2,572 subtests
(13,553 -> 16,125), FAIL 599 -> 569, TIMEOUT 59 -> 45, HARNESS_ERROR 63 -> 54.
Per suite: `dom/nodes` 114 -> 128, `dom/events` 56 -> 56, `html/dom` 76 -> 81,
`css/cssom` 54 -> 75, `css/css-values` 69 -> 82. What sits between the two
commits: the 2026-09-08 file splits (no behaviour), the four gate fixes
(`querySelector` on a detached subtree, `:lang()` wildcards and the
Content-Language pragma, `for-in` over a proxy, the style attribute's
serialisation, an iframe's `parent`/`top`), and the CSSOM reaching the cascade —
which is most of `css/cssom`'s +21.

### What is standing in front of the most tests now

Counted from this run's JSON. "Near-pass" is a FAIL file with one or two
failing subtests — the cheapest file to turn.

| what | where | counted |
|---|---|---:|
| near-pass files | — | `html/dom` 98, `css/css-values` 75, `dom/nodes` 70, `css/cssom` 67, `dom/events` 13 |
| Web Animations (`element.animate`, `getAnimations`) | new binding | 246 subtests say "Web Animations should be supported" in `css/css-values` |
| `customElements.define` | new binding | 6 HARNESS_ERRORs across `dom/nodes`, `html/dom`, `css/css-values` |
| reflection: `IDL get expected _ but got _` | `element/reflection.cpp` | 1,097 subtests in `html/dom`, plus 234 `expected null but got string` and 283 `innerText` |
| `render-blocking` (`blocking=`) | `element/reflection.cpp` | 11 files in `html/dom`, several as 10 s TIMEOUTs |
| `hasOwnProperty` undefined on a wrapper | the wrapper prototype chain | 6 HARNESS_ERRORs in `html/dom`, 11 subtests in `css/cssom` |
| `on{animation,transition}*` handlers | `events/` — landed after this run in `4f3fdc50` | ~36 subtests in `dom/events` |
| `HTMLLinkElement-disabled-*` | `stylesheets/` | 4 TIMEOUTs in `css/cssom` |
| `document.characterSet` normalisation | `document/` | 2 × 60 s TIMEOUTs in `dom/nodes` |
| `FontFace` constructor, `CSS.registerProperty`, `showModal` | new | 3 + 2 + 1 HARNESS_ERRORs in `css/css-values` |

## The previous baseline — 2026-09-07, night

**369 of the 1,090 tests that ran, which is 33.9%**, and still not one crash.
Measured on the devbox against WPT `3f6b09ae`, four workers, a 4 GB `ulimit -v`
per driver, `CTBROWSER_GL_DRIVER=deterministic`, engine at commit `f7e0912` on
`ctbrowser-wpt`.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 114 | 148 | 32 | 0 | 15 | 53 | 362 |
| `dom/events` | 56 | 24 | 8 | 0 | 3 | 85 | 176 |
| `html/dom` | 76 | 130 | 10 | 0 | 11 | 138 | 365 |
| `css/cssom` | 54 | 116 | 6 | 0 | 16 | 29 | 221 |
| `css/css-values` | 69 | 181 | 3 | 0 | 18 | 237 | 508 |
| **total** | **369** | **599** | **59** | **0** | **63** | **542** | **1,632** |

Subtests: **13,553 PASS, 10,868 FAIL, 726 NOTRUN, 111 TIMEOUT.**

`f7e0912` is the tip of the branch after the previous session's four subagent
merges landed — the XML front end, the live collections, the CSS value grammar,
the CSSOM object model, the reflection work, `attachShadow`, `document.fonts`
and the ECMAScript early errors. Those merges are the +34 files over the
`636f1b3` row below; the table under it is what each of them was for.

### And what the same commit says about the SUITE

**The engine's own CTest gate was RED at `f7e0912`, 112 of 119**, and it had
been red since those merges landed: `page_scripts`, `selectors`,
`widgets_basics`, `shadow_dom`, `vm_basics` and `promise_combinators` failed and
`early_errors` **hung** — killed at 1,500 s. Every one of them predates this
measurement.

That is worth writing down beside a number that went up, because the two facts
are the same fact: four branches were merged without the suite being run over
them, so the WPT score moved and six unit tests and one hang moved with it. The
instrument is not the gate. `tools/remote-build.sh` is the gate, and a WPT
measurement taken without it is a measurement of an engine nobody has checked.

## The previous baseline — 2026-09-07, evening

**335 of the 1,090 tests that ran, which is 30.7%**, and still not one crash.
Measured on the devbox against WPT `3f6b09ae`, four workers,
`CTBROWSER_GL_DRIVER=deterministic`, engine at commit `636f1b3` on
`ctbrowser-wpt`.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 106 | 157 | 31 | 0 | 15 | 53 | 362 |
| `dom/events` | 56 | 24 | 8 | 0 | 3 | 85 | 176 |
| `html/dom` | 71 | 135 | 10 | 0 | 11 | 138 | 365 |
| `css/cssom` | 54 | 115 | 6 | 0 | 17 | 29 | 221 |
| `css/css-values` | 48 | 199 | 3 | 0 | 21 | 237 | 508 |
| **total** | **335** | **630** | **58** | **0** | **67** | **542** | **1,632** |

Subtests: **13,168 PASS, 11,165 FAIL, 726 NOTRUN, 109 TIMEOUT.**

**Test by test against the run below it: 74 files gained and NOT ONE lost.**
That is the number worth reading twice, because the previous session's table has
a column of six regressions in it and this one does not. HARNESS_ERROR fell
90 -> 67 and TIMEOUT 60 -> 58, both of which mean tests that could not start now
start.

Two measurements were taken this session and both are here, because the first
one is what makes the second attributable:

| date | commit | PASS | of 1,090 | what it measures |
|---|---|---:|---:|---|
| 2026-09-07 (day) | — | 197 | 18.1% | the session in the table further down |
| 2026-09-07 (eve) | `a790941` | 261 | 23.9% | three DOM commits carried over from an interrupted session: a namespaced `attribute`, the document answering as a Node, and the computed passive default |
| 2026-09-07 (eve) | `636f1b3` | **335** | **30.7%** | the session below |

### What moved, and what it cost

| change | measured effect |
|---|---|
| **an XML front end** — `dom/xml.hpp` and `lib/DOM/xml.cpp`, a real XML 1.0 parser, because an `.xhtml` file handed to the HTML tree builder gets a `<script>` whose body still has `<![CDATA[` on the front | twelve `dom/nodes` files reported `parse error: expression - at 1:1` and ran no assertion at all. All 80 `.xhtml`/`.xht`/`.xml` files in the corpus were parsed with it and 76 are well-formed by it; the four that are not are each correct, and named in the header |
| **the interface table is built before it is adopted** — `interface_prototypes_` is built lazily on the first `wrap()`, and `createHTMLDocument`, `make_live_collection` and `documentElement` all read it before anything had wrapped a node | four unit tests, and the shape of the bug is worth naming: `instanceof HTMLDivElement` was FALSE for a page whose first statement made a document and TRUE for one that had touched an element first |
| **a collection is live, an HTMLCollection, and read-only by index** — `getElementsByTagName` built an array-shaped plain object and `Element.children` a plain Array | `dom/nodes` 75 -> 106 with the two below |
| **`instanceof` follows a proxy to its target** — 7.3.21 calls `[[GetPrototypeOf]]`, which a proxy forwards; this engine read a `prototype` field a `proxy_object` does not have | every live collection and `window` answered false. It is also what `document.links`, `document.scripts` and `getElementsByName` assert first |
| **`el.style` follows the `style` attribute** — the declaration store was seeded once at wrapper construction and `setAttribute("style", …)` never touched the proxy | `serialize-values.html` is 697 subtests of exactly that shape, and `css-style-attr-decl-block.html` names it in a subtest title |
| **an undeclared name asks the embedder** — HTML 7.3.3 makes an element with an `id` a named property of the global object, and a bare identifier resolves against that object | six `css/cssom` files address their subject as `getComputedStyle(target1)` with `target1` written nowhere but in the markup, and every read off it was `undefined` |
| **CharacterData**, `Text.splitText`, `new Text()`/`new Comment()`/`new DocumentFragment()`, `isEqualNode`/`isSameNode`, and a detached `Attr` that keeps the value it was removed with | `dom/nodes` again; offsets are UTF-16 code units over UTF-8 bytes, which is the part a shortcut gets wrong |
| **the CSS value grammar and canonical serialization** — §10.13 sum ordering, the calculation context a property imposes, `progress()`, `ident()`, `inherit()`, `random-item()`, and a percentage with no context being a syntax error | `css/css-values` 29 -> 48 |
| **CSSOM's `getComputedStyle`** — a pseudo-element argument is not the element, the automatic minimum size for a grid item and for an `aspect-ratio`, and a computed `font-family` that keeps its case | `css/cssom` 39 -> 54 |
| **the CSSOM object model** — constructable sheets, `insertRule` on a grouping rule, a MediaList that is a view of its text, adopted sheets reaching the author CSS | `css/cssom`, with the row above |
| **reflection, `dataset`, ARIA, `insertAdjacent*`, the namespaced attribute API** | `html/dom` 62 -> 71 |

### And one regression, caused and fixed inside the session

Making `getElementsByTagName` and `Element.children` live turned them into
PROXIES, and `context::iterable_values` had no proxy case — so
`for (const x of el.children)` and `[...collection]` silently read an **empty
list**. Not an error: a wrong answer. `unittests/unit/node_methods` caught it
on the devbox before the measurement above was taken.

`getElementsByClassName` and the eight document collections were already proxies
and already iterated as nothing, so the defect predates the change that exposed
it — which is the argument for a suite that runs on every build rather than a
number that only moves forward.

## The previous baseline — 2026-09-07, day

**1,632 tests, 232.1 s, four workers, and still not one crash.** Measured on the
devbox against WPT `3f6b09ae`, engine at this branch.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 65 | 181 | 31 | 0 | 32 | 53 | 362 |
| `dom/events` | 52 | 21 | 8 | 0 | 10 | 85 | 176 |
| `html/dom` | 43 | 150 | 17 | 0 | 17 | 138 | 365 |
| `css/cssom` | 21 | 140 | 7 | 0 | 24 | 29 | 221 |
| `css/css-values` | 16 | 215 | 1 | 0 | 39 | 237 | 508 |
| **total** | **197** | **707** | **64** | **0** | **122** | **542** | **1,632** |

Subtests: **3,622 PASS, 18,478 FAIL, 736 NOTRUN, 101 TIMEOUT.**

**197 of 1,090 tests that ran, which is 18.1%.** Four days earlier it was 119 —
10.9% — and the two measurements in between are worth keeping because they say
which half of the movement was aimed at:

| date | PASS | of 1,090 | what happened |
|---|---:|---:|---|
| 2026-09-02 | 52 | 4.8% | the first measurement |
| 2026-09-03 | 119 | 10.9% | six DOM changes, each measured |
| 2026-09-07 (before) | 138 | 12.7% | **nothing aimed at WPT**: the GC, array and error-prototype work since |
| 2026-09-07 (after) | 197 | 18.1% | the session below |

Test by test against the 2026-09-07 "before" run: **65 files gained, 6 lost**.
HARNESS_ERROR fell 162 -> 122 and TIMEOUT 81 -> 64, both of which mean tests that
could not start now start.

**Zero crashes** again, over a still larger executed surface.

### What moved, and what it cost

Every row is one commit with its own before/after, and every number was measured
rather than projected. The engine work is described where it lives; this is what
the instrument said about it.

| change | measured effect |
|---|---|
| **DOMException**, and `context::throw_value` so a native can throw one | `assert_throws_dom` checks `code`, `name` AND `e.constructor === DOMException`; an Error passes none. 246 `dom/nodes` subtests reported exactly that |
| **`getComputedStyle` answers** — a script mutation marks `dirty::styles` rather than `dirty::paint`, the read flushes the pending restyle, and the object publishes 125 longhands under both spellings with their initial values | `css/cssom` 12 -> 21, and it is the precondition for most of `css/css-values` |
| **the value grammar** — 143 properties with a syntax, `el.style` validating and canonicalising, `CSS.supports`, `CSS.escape` | `test_invalid_value` can be answered at all for the first time |
| **the element-only tree** — `firstElementChild` and its six siblings, `moveBefore`, `matches`/`closest`, and pre-insertion validity throwing the DOMException the specification names | `dom/nodes` 46 -> 65 |
| **eleven event interfaces**, a listener that may be an object with `handleEvent`, `this` bound to the current target, `passive` enforced, and a throwing listener reported to the page | `dom/events` 37 -> 52, subtests 180 -> 320 |
| **a handler property on the window fires at all** — `fire_handler_property` tested `is_object()` and the window is a PROXY, so `window.onerror`, `window.onload` and `window.onclick` had never once run | found by a unit test written for the throwing-listener work, not by the corpus |
| **the document's own throws**, three separate name rules verified against every row of the corpus's own tables, `defaultView`, `document.write`, `compatMode` from the doctype | `html/dom` 27 -> 43 |

### And six that went PASS -> FAIL, every one of them diagnosed

The gate fails in both directions and so does this table. All six are in
`css/css-values`, and **all six were passing by not testing anything**:

| what | why it passed before | why it fails now |
|---|---|---|
| `attr-all-types`, `attr-argument-grammar`, `random-alias-property`, `calc-time-values` | each guards its assertions on `CSS.supports`, which did not exist | it does now, and `attr()` / `random()` / `type()` are not implemented. `CSS.supports` was fixed the same day to refuse a value calling a function this engine cannot evaluate — these four now RUN and fail on the feature itself |
| `random-item-serialize` | `el.style` stored any value it was given, so a `random-item()` on a modelled property round-tripped | the value grammar refuses it, which is what a browser without `random-item()` does. `assert_not_equals(readValue, "")` is the test noticing |
| `lh-rlh-on-root-001` | `getComputedStyle` answered `undefined` and two of its four subtests never got far enough to compare | it answers now, and answers 16 where 20 belongs: `lh` must be the line box's height and this engine resolves it to the font size |

Two of the seven in the first measurement were a real defect of this session's own
— `el.style` re-serialising a value whose grammar it does not model, so
`random-item(auto ,serif)` came back with the spacing changed — and that one is
fixed; `random-item-valid.html` passes again.

### The previous baseline, 2026-09-03

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP |
|---|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 42 | 199 | 32 | 0 | 36 | 53 |
| `dom/events` | 24 | 45 | 11 | 0 | 11 | 85 |
| `html/dom` | 27 | 126 | 19 | 0 | 55 | 138 |
| `css/cssom` | 10 | 145 | 17 | 0 | 20 | 29 |
| `css/css-values` | 16 | 213 | 3 | 0 | 39 | 237 |
| **total** | **119** | **728** | **82** | **0** | **161** | **542** |

Subtests: **2,262 PASS, 16,331 FAIL, 746 NOTRUN, 92 TIMEOUT.**

### The previous baseline, 2026-09-02, for comparison

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP |
|---|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 14 | 215 | 29 | 0 | 51 | 53 |
| `dom/events` | 6 | 62 | 10 | 0 | 13 | 85 |
| `html/dom` | 11 | 135 | 7 | 0 | 74 | 138 |
| `css/cssom` | 8 | 148 | 15 | 0 | 21 | 29 |
| `css/css-values` | 13 | 128 | 2 | 0 | 128 | 237 |
| **total** | **52** | **688** | **63** | **0** | **287** | **542** |

**Two columns are not comparable across the 09-02 and 09-03 tables, and saying
which is the point of keeping both.** `css/support/` was not in the sparse
checkout on 2026-09-02, so 94 of `css/css-values`' 128 harness errors were one
missing helper file each; fetching it converted 84 into real failures and three
into passes with the engine UNCHANGED. FAIL going up by 84 is the instrument
working. HARNESS_ERROR falling and TIMEOUT rising is mostly the same story: a
test that used to die on the first missing function now runs.

**And once more between 09-03 and 09-07**, for the same reason and recorded the
same way: `/dom/constants.js` and `/dom/common.js` joined the sparse list.
`dom/events/Event-constants.html` reported HARNESS_ERROR about a helper that was
never checked out. One file, and it is named here so nobody attributes it to the
engine.

## What is standing in front of the most tests now

Every entry is a count from the **2026-09-07 evening** run at `636f1b3` and
names a piece of work rather than a symptom. This is the handoff.

| what | where | what it is worth, counted |
|---|---|---:|
| **`attachShadow` and the shadow root** | `lib/Shell/bindings/element/shadow.cpp`, and it is a BINDINGS change: `node_kind` already has `document_fragment`, which is the shape of a shadow root | **34 files**, across all four suites, and most of them are not ABOUT shadow DOM - they attach a shadow tree as scaffolding and assert something else. Several are whole-file HARNESS_ERRORs, so they report nothing at all today |
| **`customElements.define`** | new, `lib/Shell/bindings/` | 11 files |
| **`document.fonts`**, even as a `FontFaceSet` whose `ready` is already resolved | `lib/Shell/bindings/document/install.cpp` | 10 files die on `` `then` is undefined `` before their first assertion |
| **`assert_implements`** — the feature-detection helper reports `undefined` | mostly `html/dom/render-blocking/`, which needs `blocking=` on `<link>`/`<script>` | 11 files |
| **Web Animations** — `element.animate`, `getAnimations`, `KeyframeEffect` | new | 6 files in `css/css-values`, plus the interpolation ones behind them |
| **`<script type="module">`** | `lib/Shell` script loading | the seven `css/cssom/getComputedStyle-insets-*` files import their fixture as a module |
| **the reflection tables** | `lib/Shell/bindings/element/reflection.cpp` | the largest SUBTEST cluster left: `html/dom` has 2,468 failing subtests and the eight `reflection-*.html` files are most of them. Note the file yield is low - they time out regardless - and the subtest yield is enormous |
| **`css/css-values` has 4,541 failing subtests** and `dom/nodes` 4,203 | — | the two suites where the remaining work is deepest rather than widest |

### The earlier handoff, from the 2026-09-07 day run

Kept because several of its rows are now done and the ones that are not have not
moved. `querySelector` on the Selectors engine, reflection as a table, a second
Document, MutationObserver, the CSSOM object model, interface objects for node
types and attribute namespaces have all landed since it was written; shadow DOM
and named access on the Window are the two that had not, and named access landed
in the evening run above.

| what | where | what it is worth |
|---|---|---|
| **`querySelector` runs a hand-rolled matcher, not the Selectors engine** | `dom_bindings::query`, `lib/Shell/bindings/document/tree_ops.cpp` | it gives up on ANY selector containing a space or a `>` — its own comment says "a combinator: not supported, matches nothing" — while `lib/Style/css/selector.cpp` and `style::engine::matches` implement combinators, attribute selectors, `:not`/`:is` and the sibling forms. Two matchers, and the weaker one is the one script reaches. `matches`/`closest` are deliberately defined in terms of it so the two cannot disagree, which means all three move together. The rung: give `style::engine` a public `select(txn, root, selector_list)` that runs the same DFS `resolve_subtree` does but tests each element instead of resolving it — the cursor it needs (`levels_`, `path_`, `ancestor_filter`) is already maintained there — and give `dom_bindings` a way to reach the engine, which today observes only the resolved `style_map`. It is also the precondition for `querySelector` throwing `SyntaxError`, which it must NOT do before then: it cannot parse plenty of VALID selectors, so it would fire on correct input |
| **reflection: an IDL attribute does not reflect its content attribute** | `lib/Shell/bindings/element/reflection.cpp` | ~6,400 failing subtests in `html/dom`, in four shapes: `getAttribute() expected X but got Y` (2,453), `IDL get expected (string) X but got undefined` (2,411), the same for boolean (665) and for number (576). It wants a TABLE — interface, IDL name, content attribute, type, default — not a method each. Note the file yield is low and the subtest yield is enormous: the eight `reflection-*.html` files each run thousands of subtests and time out regardless |
| **a second Document** | `dom_bindings`, the whole handle model | `createHTMLDocument` (27 files) and `createDocument` (17). `node_id` is a slot+generation into one slab and `wrappers_`, `namespaces_`, `mirrors_` and `webgl_objects_` are ALL keyed on `pack(node_id)`, so two documents give two different nodes the same key and `getElementById` on one returns the other's wrapper. Doing it properly means a document handle beside the node handle in every one of those and in `receiver()`/`handle_of()`/`wrap()`, and `doc_` becoming "the document this call is about" rather than a member, across ~90 uses in six files |
| **MutationObserver** | `dom_bindings`, `mutated()` | 18 files in `dom/nodes`, 5 more as harness errors in `css/cssom` and `html/dom`. `mutated()` is already the funnel — 18 call sites under `element/`, 5 in `document/` — so a per-target snapshot diff there is the shape, and it needs members on `dom_bindings` to hold the observer list, its queue and the snapshot. **All or nothing**: the validation half alone turns five `MutationObserver-*` files from a fast FAIL into a 10-second TIMEOUT, because their last subtest is an `async_test` waiting for a record |
| **the CSSOM object model** | new, `lib/Shell/bindings/` | `document.styleSheets`, `CSSStyleSheet`, `CSSRuleList`, `CSSStyleRule.selectorText`, `insertRule`/`deleteRule`, `new CSSStyleSheet()`. It is the precondition for most of what is left in `css/cssom` — 22 subtests fail on `new CSSStyleSheet` by name and four `dom/events` harness errors are `insertRule`. `style::css::stylesheet` already retains the rules, the selectors and the declarations, so the model is there; what is missing is the binding and a way for `dom_bindings` to reach the engine's sheets |
| **interface objects for node types** | `lib/Shell/bindings/element/interfaces.cpp` | `HTMLBodyElement`, `Window`, `Document`, `NodeList`, `HTMLCollection` as globals with prototypes the wrappers chain to. It is what `assert_class_string`, `e instanceof HTMLBodyElement` and `eventTarget.constructor.name` all ask, and one defect stands behind `Body-FrameSet-Event-Handlers.html`, `passive-by-default.html`, `document.links` and `document.scripts` |
| **shadow DOM** | the tree model | `attachShadow`: 14 files plus five harness errors across three suites. Not a bindings change |
| **attribute namespaces** | `include/ctbrowser/dom/node.hpp` | `setAttributeNS`/`getAttributeNS`: `struct attribute` is `(atom name, std::string value)` with nowhere to put a namespace, so this is a DOM-layer change rather than a binding. 80 subtests and two harness errors in `dom/nodes` |
| **named access on the Window** | `lib/Shell/bindings/window/window.cpp` | `window[id]` for an element with an `id`. One harness error in `dom/events` found it; it is a documented HTML feature that pages use widely |

## Skips, all 542 of them

| count | reason |
|---:|---|
| 331 | reftest: needs a reference render |
| 120 | not a testharness test |
| 76 | testdriver: needs WebDriver input injection |
| 8 | variant: the driver opens a file and has no query string |
| 6 | https: needs a TLS origin |
| 1 | `global=dedicatedworker`: no Worker |

Every one names a feature. **No skip in this list exists to improve a number.**


## The corpus

Fetched by `tools/wpt/fetch-wpt.sh` into `~/.cache/wpt` (`$WPT_DIR` overrides),
**sparse and shallow, at a pinned commit**, and never committed here.

| | |
|---|---|
| pin | `3f6b09ae3ed55280074645ce38e9002f52fc60a8` |
| where | `~/.cache/wpt`, outside the source tree |
| size | ~71 MB, 10,601 files on disk out of WPT's 162,834 (2,465 and 13 MB before the widening of 2026-09-13) |
| verify | `tools/wpt/fetch-wpt.sh --verify` — checks the SHA *and* that the sparse patterns matched something |

Vendoring it was never on the table: WPT is ~1.5 million files, it changes every
day, and what has to be reproducible is the **commit**, not a copy of the bytes.
`~/.cache` rather than `build/` because a reconfigure wipes the build tree and
re-downloading a corpus because somebody deleted a `CMakeCache.txt` is a bad
trade.

**THE WIDENING OF 2026-09-13.** Five suites at 69% with a long tail of features
said less and less about the engine, so the list grew in two ways. Every CSS
module's `parsing/` directory, its `inheritance.html` and its `animation/`
tests are in - the sparse patterns `/css/*/parsing/`, `/css/*/inheritance.html`,
`/css/*/animation/` are what `--no-cone` is for - which is 1,747 testharness
files over 60 modules that all go through `css/support/`: the CSS front end
measured property by property (`test_valid_value`, `test_invalid_value`,
`test_computed_value` over 415 properties), and every interpolation test that
drives CSS Transitions, CSS Animations and Web Animations. And twenty more
whole suites that are mostly testharness and mostly implemented: `css-syntax`,
`css-variables`, `css-cascade`, `css-conditional`, `css-nesting`, `css-color`,
`cssom-view`, `selectors`, `mediaqueries`, `html/syntax` (the html5lib
tree-construction fixtures against the DOM's own tree builder),
`html/semantics/forms`, `html/webappapis`, `dom/ranges`, `dom/traversal`,
`dom/lists`, `dom/collections`, `dom/abort`, `shadow-dom`, `custom-elements`,
`domparsing`, `selection`, `url`, `encoding`. `run-wpt.py`'s own planner
counts 5,099 runnable tests in the widened checkout against 1,090 before,
2,593 skipped (reftests, mostly). A module directory taken whole would be
reftests; a suite for a feature the engine has never heard of would be NOTRUN
noise - both are still the rule for what is NOT in the list.

**THE CHECKOUT IS SHARED, on the devbox.** One `~/.cache/wpt` serves every agent
and every worktree on that machine, and it is not covered by the per-directory
isolation that keeps two builds apart. On 2026-09-03 one agent added
`/css/support/` to it and another agent's next measurement moved by 87 files in
a suite its change could not have touched. **If a number moves in a suite the
change cannot explain, check `.git/info/sparse-checkout` and its mtime before
believing the engine did it** — that is what identified this one. The fetch list
in `fetch-wpt.sh` and the expectations file are kept in step deliberately so a
fresh checkout and a shared one agree.

`--verify` checks two things and the second is the one that matters: a sparse
checkout whose patterns matched nothing leaves a directory that exists, has the
right `HEAD`, and cannot run a single test. That is a green verify followed by a
suite of identical harness errors, so `resources/testharness.js` and `dom/nodes`
are checked **by name**.

### Which suites, and why

The sparse list is in `fetch-wpt.sh`. It is chosen by what this engine
implements, because a suite for a feature the engine has never heard of reports
the same thing for every file in it and that is noise in a table rather than a
finding.

| path | why it is fetched |
|---|---|
| `resources/` | testharness.js itself. Not optional |
| `common/` | the fixtures the suites import |
| `dom/nodes/` | the tree: `Node`, `Element`, `Attr`, `Document`. The engine has its own WHATWG tokenizer and tree builder, so this is the closest thing to a direct measurement of it |
| `dom/events/` | `EventTarget`, dispatch, capture/bubble, listener options — all of which `lib/Shell/bindings/events/` implements |
| `html/dom/` | reflection: does `el.id = "x"` change the attribute, and back |
| `css/cssom/` | `getComputedStyle` and the style declaration objects, which `lib/Shell/bindings/computed_style/` answers and `tools/check/css-parity.py` already measures against Chrome a different way |
| `css/css-values/` | value parsing and computation — `calc()`, lengths, units — against the CSS Syntax Level 3 front end in `lib/Style/css/` |
| `css/support/` | the helpers the two `css/` suites import. **Not optional either**: `test_valid_value`, `test_computed_value`, `test_math_used`, `test_interpolation` and `test_specified_serialization` all live here, and a test that cannot load one reports HARNESS_ERROR without running a subtest. Adding it on 2026-09-03 converted 87 harness errors into real measurements |

**Left out on purpose**, and each for a reason rather than a score: everything
needing a network origin (`fetch/`, `xhr/`, `service-workers/` — the runner has
no server and `CTBROWSER_NETWORK=0`), everything needing a second browsing
context (`html/browsers/`, `webmessaging/` — no iframes, no `window.open`, no
workers), and the reftest suites (`css/css-flexbox/` and friends are render
comparisons, which is `tools/check/check-render.cmake`'s instrument, not this
one).

## The results hook

`tools/wpt/testharnessreport.js` is **the slot WPT leaves for the
implementation**. Every test loads two scripts: `testharness.js`, which is the
harness and is the same for everybody, and `testharnessreport.js`, whose copy in
the checkout does nothing at all. `run-wpt.py` copies ours over it on **every**
run, so a re-fetch can never leave the corpus silently reporting nothing. This
is exactly what every browser vendor does.

**Where the results go**: onto `window.__wpt_state`, as a string that is already
JSON, with `window.__wpt_done` set after it. The runner reads it back through
`ctdrive`'s `eval`, which returns whatever the snippet *logged* — so one
`console.log` of one string is the whole channel, and it needs nothing from the
engine a page could not do.

Three decisions in that file are worth stating:

- **The JSON is built by hand.** `JSON.stringify` would make every result in the
  suite depend on this engine's `JSON.stringify` being right about nested
  objects, string escaping and lone surrogates — and when it was not, the
  failure would arrive as a hundred harness errors that look like the DOM is
  broken. The one thing that must not be under test is the instrument. Non-ASCII
  is escaped too, so the payload crossing a socket, a `std::string` and a Python
  decode is pure ASCII and none of the three has to agree about an encoding.
- **`setup({output: false})`**, as wptrunner's own hook does. The results table
  testharness builds into `#log` is for a human with a browser window; building
  it puts `createElementNS`, `appendChild` and `textContent` between every test
  and its result, so a defect in any of the three would arrive as a corpus-wide
  failure saying nothing about the test that found it.
- **A missing harness is reported, not silent.** If `add_completion_callback` is
  not a function the hook publishes a `HARNESS_ERROR` saying so. Without that,
  a page that failed to load the harness reports zero subtests — and zero
  failing subtests is exactly what a naive runner scores as a pass.

### How console output and errors reach the terminal today

They already did, and nothing new was needed for the channel itself:

- `console.log` accumulates in `browser::bindings().console_output()`, and
  `ctdrive`'s `eval` returns everything logged during the snippet. That is the
  mechanism `tools/check/compare.py` has used all along.
- an uncaught throw in a `<script>` lands in `browser::script_error()`, which
  `ctdrive`'s `info` command returns and which `ctbrowse` prints. The runner
  reads it when a page dies without publishing, so a timeout can say *why*.

What was missing was the **page's** half of that — see the next section.

## What WPT found in the engine

**Seven gaps**, in the order they blocked things. Each is a genuine engine
defect rather than harness scaffolding, each is fixed on this branch, and the
full suite (118 tests, goldens included) still passes with all seven fixed.

Two of them were found by the negative proofs rather than by the corpus, which
is the argument for having them: `--selftest` reported the wrong outcome for
three of its five fixtures the first time it ran, and `window.parent` and the
stuck VM fault were what those misses turned out to be. The regex defect came
the other way round - the corpus ran, the numbers looked plausible, and the
FAILURE MESSAGES were gibberish.

| | what was wrong | what it looked like |
|---|---|---|
| `self` was not defined | `window` and `globalThis` were, `self` was not | testharness.js is `(function (global_scope) { … }(self))`, so `global_scope` was `undefined` — and this engine treats a property **store** on undefined as a no-op rather than a TypeError, so the harness ran to completion, reported no error, and defined not one of its globals. The page then failed with "`test` is undefined", forty lines from the cause. **100% of WPT was unrunnable for one missing alias.** |
| a leading `/` had no document root | `<script src="/resources/testharness.js">` was read off the root of the disk | every test in the corpus loaded with no harness at all, and every one of them reported the same error — none of which was about the engine. `shell::asset_registry::set_document_root`, set from `CTBROWSER_DOC_ROOT`, is what a server would have resolved it against |
| `addEventListener` was not a bare global | it was a property of `window` only, and the window proxy's fallback goes `window.x` → global, never the other way | testharness installs its uncaught-exception handler as a receiverless `addEventListener("error", …)`. That threw a TypeError at the very end of the harness's own initialisation — after the asserts were exposed and before `on_tests_ready()`, which is the worst possible place: the file half-ran and said nothing |
| there was no `load` event | the engine sets `document.readyState` to `"complete"` from the start, so every library that *asks* before it listens — p5, Phaser, Babylon — takes its already-loaded branch and never needed one | testharness does the opposite: it listens unconditionally and sets `all_loaded` in the handler, and `Tests.all_done()` requires that flag. **Every test ran its subtests, passed them, and then sat until the harness's own 10-second timeout and reported TIMEOUT.** `browser::tick` now fires `DOMContentLoaded` and then `load` at the window, once per document |
| an uncaught throw was invisible to the page | it reached `browser::script_error()` — an *embedder* channel — and no `error` event was ever dispatched | a test that threw during load could not be told from one that never finished. `dom_bindings::dispatch_error` gives the page `window.onerror` and `addEventListener("error", …)`, which is what turns that case into a `HARNESS_ERROR` |
| `window.parent` was undefined | there are no iframes, so nothing had ever needed the browsing-context chain | testharness walks `[self … top, opener]` to broadcast state. With `parent` undefined the walk ran one step past the end and called `postMessage` on `undefined` — **inside a completion callback**, and `notify_complete` runs those in a bare `forEach` with no try/catch, so the throw killed every later callback including the one that reports results. Every test that ran perfectly reported nothing and was recorded as a TIMEOUT. `parent`/`top` are now this window and `opener` is null, which is what the spec says a top-level browsing context reports |
| a thrown script left the VM refusing to run | `run()` leaves `failed_` set, and every C++ entry into JavaScript declines while it is | so dispatching the new `error` event ran **no listener at all**, reported the original error a second time as a "callback fault", and left the page believing nothing had gone wrong. The harness then finished normally and **a page that threw during load was reported as a PASS** — the single worst answer this instrument can give. The fault is now taken before the event is dispatched: `script_error_` already holds the text, and the page cannot be handed anything while the VM is refusing to run its code |
| `\uXXXX` and `\xHH` in a regex were not decoded | `rx_escape_char` fell through to "the char itself", so `\u` was the letter `u` and the four hex digits became four more class members. `[\udc00-\udfff]` parsed as `{u,d,c,0,f}` plus the **range `'0'`–`'u'`** | which matches most of ASCII, silently. testharness sanitises every test **name** and every assertion **message** through `str.replace(/([\ud800-\udbff]+)…/g, …)`, so that bogus range hit nearly every character of every string the harness had to say: 2,343 failing subtests in `dom/nodes` came back with their names rewritten into runs of `U+61U+73U+73…`. A corpus-wide corruption of the **results**, from one escape. Now decoded; above `0xFF` the byte-based matcher refuses the pattern rather than approximating it, which makes the sanitiser the no-op it should always have been |

### And six more, found in the second pass

The gaps above stopped the corpus running at all. These were found by reading
what it then said, one API at a time — each is measured in the table further up,
and each has a commit of its own.

| | what was wrong | what it looked like |
|---|---|---|
| `getElementsByClassName` did not exist | neither did `getElementsByName`, `removeAttribute`, `hasAttribute`, `nodeName`, `nodeType` or `localName` | 39 test files, and it was the largest single cause in the first baseline. The hard half was not the search but the COLLECTION: `getElementsBy*` returns a live view, and five of the suite's own tests take one, mutate the document and read it again |
| the document and the window were ONE listener bucket | `listener::target` empty meant "one of the two, we cannot tell" | invisible until an event carried `currentTarget`, and then both reported the same object. `removeEventListener` on one could take the other's listener away |
| dispatch had no path, no phase and no flags | it fired the global bucket, walked the ancestors, and fired the global bucket again | `currentTarget` and `eventPhase` read `undefined`, `stopPropagation` was a no-op, and nothing a page CONSTRUCTED could be dispatched at all — `document.createEvent` and `dispatchEvent` did not exist |
| a `once` listener was reaped during a NESTED dispatch | the compaction ran at the end of every dispatch, including one started inside a listener | it shifts the vector the outer dispatch is indexing, so the listener after the removed one is skipped. Reproduced by `AddEventListenerOptions-once.any.js`, whose second case dispatches from inside a `once` listener that re-registers itself |
| a listener registered twice was registered twice | the DOM says (type, callback, capture) on one target is a listener's IDENTITY | a page that registers defensively in a function it calls twice got two calls per event, and a `once` listener registered twice fired twice |
| `document.implementation` did not exist | so `hasFeature` did not, and the test's `.apply(...)` on `undefined` did not either | 136 assertions in one file, reported as "`apply` is not a function" — a message about a method nobody was missing, forty lines from the cause |

### And the two that were NOT fixed then, both of which are now

`docs/wpt.md` named these on 2026-09-03 as standing in front of many tests. Both
have since been closed, and how each turned out is worth a line:

- **`"length" in []` was false.** `context::has_property` parsed an array key as
  an index and never answered `"length"`, so `assert_array_equals` — which opens
  with exactly that test — could not compare against any array a page built.
  Closed by the property-attribute work: `own_property` is now the shared
  [[GetOwnProperty]] over all four tables and `in` walks the whole chain through
  it, which also fixed `'toString' in {}` and an accessor being invisible to the
  operator whose entire job is to see one.
- **A native could not throw a DOMException.** Closed by
  `context::throw_value(value)` and `lib/Shell/bindings/exceptions.cpp`. What it
  was worth is in the 2026-09-07 table; what it turned out to ALSO be worth is
  not in any table: `Document-createElementNS.html` passes
  `doc.defaultView.DOMException` as `assert_throws_dom`'s constructor argument,
  so its 110 throwing assertions needed `document.defaultView` as much as they
  needed the exception.

## Running it

**One directory at a time is the interface.** A batch that runs everything and
prints one number hides exactly what you need.

```bash
tools/wpt/fetch-wpt.sh                            # once; ~40 MB
tools/wpt/run-wpt.py --selftest                   # FIRST: does the harness fail?
tools/wpt/run-wpt.py --dir dom/nodes              # a table
tools/wpt/run-wpt.py --dir dom/events --filter Event-dispatch
tools/wpt/run-wpt.py --dir css/cssom --json /tmp/cssom.json --tsv /tmp/cssom.tsv
```

Useful flags: `--jobs N` (4 by default — **the devbox has 8 vCPUs and is
shared**), `--memory-mb N` (4096, applied as `ulimit -v` to each driver),
`--limit N`, `--driver PATH`.

Each test is **one `ctdrive` process**, which is what makes a crash a crash: the
process dies, the runner sees the signal and names it, and the next test is
unaffected. The environment is pinned the way the render checks pin theirs —
`CTBROWSER_GL_DRIVER=deterministic`, `SDL_VIDEODRIVER=offscreen`,
`CTBROWSER_FONTS=font8x8`, `CTBROWSER_NETWORK=0` — so a run says the same thing
twice and a box with no GPU is not a variable.

### Timeouts and the memory cap

WPT's own metadata is the source of truth: `<meta name="timeout" content="long">`
means 60 s and everything else means 10 s. The runner adds a 5 s margin on top,
and the margin is the point — the harness has its **own** 10-second timeout and
must be given the chance to report it, because a page killed at exactly its
deadline loses the subtest detail the harness was about to publish.

`ulimit -v` is applied by a one-line `/bin/sh` wrapper rather than by Python's
`preexec_fn`, which the standard library documents as unsafe from a thread pool
— and this runner has four workers, so that is not a theoretical objection.
`ulimit -c 0` goes with it: hundreds of multi-GB cores is a build machine down,
not a finding.

### `.any.js` and the wrappers

A `.any.js` test has **no HTML on disk at all** — wptrunner synthesises
`<test>.any.html` when the browser asks for it. The runner builds the same page,
reading the `// META:` lines the way the manifest does (`global=`, `script=`,
`timeout=`), and writes it *beside* the script so its relative `<script src>`
resolves. Wrappers are named `*.ctwpt.html`, deleted at the end of a run, and
swept at the start of the next one so an interrupted run cannot leave the corpus
with tests nobody wrote.

## What is skipped, and why

Every skip names a **feature this engine does not implement**, never a number.
The counts are in the baseline table above; the reasons are these and only these:

| reason | what it means |
|---|---|
| `reftest: needs a reference render, not a harness result` | `<link rel=match>`. A different instrument — `tools/check/check-render.cmake` |
| `not a testharness test` | the file never loads `testharness.js`; usually a fixture or a helper page |
| `testdriver: needs WebDriver input injection` | `test_driver.click()` and friends. `ctdrive` can synthesise input, but not through testdriver's protocol |
| ~~`variant: the driver opens a file and has no query string`~~ | gone on 2026-09-13: a `<meta name="variant" content="?file=x">` is one run per variant, `ctdrive --query` giving the page its `location.search`, keyed `path?query` in the expectations as WPT's manifest keys it |
| `global=…: no Worker/ServiceWorker in this engine` | a `.any.js` whose declared scopes exclude `window` |
| `Worker:` / `SharedWorker:` / `ServiceWorker: not implemented` | the `.worker.` / `.sharedworker.` / `.serviceworker.` filename spellings |
| `https: needs a TLS origin` / `h2: needs an HTTP/2 server` | WPT encodes its server requirements in the filename, and there is no server here |

A test that this engine simply fails is **not** skipped. It is run, it fails, and
the failure is recorded — which is the difference between a measurement and a
score.

## Expectations, and the gate

`tools/wpt/expectations.txt` is **every deviation from "it passes"**, one line
each, sorted, keyed on the test path:

    dom/nodes/Node-cloneNode.html	FAIL
    dom/nodes/Node-cloneNode.html	SUBTEST	FAIL	"Node.cloneNode() on a DocumentType"
    dom/nodes/Node-baseURI.html	SKIP	not a testharness test

A test with **no** line is expected to pass with every subtest passing, so the
file only ever grows when the engine is wrong and only shrinks when it is fixed.
Subtest names are JSON-quoted because WPT has names containing tabs and
newlines. Assertion *messages* are deliberately absent: they carry values that
differ run to run, and a file that churns is a file nobody re-reads.

**The gate fails in both directions.**

```bash
tools/wpt/run-wpt.py --dir dom/nodes --check              # against expectations
tools/wpt/run-wpt.py --dir dom/nodes --update-expectations
```

A new failure is a regression. **A line that has started passing is a failure
too**, and that is the half that keeps the file honest: an expectations file
that only ever grows is a file that tells lies about the engine, and a gate that
checks one direction would keep them there forever. `--update-expectations`
records a change deliberately, and **merges** — re-measuring one directory
rewrites that directory's lines and leaves every other suite exactly as it was.

Both `--check` and `--gate` scope the comparison to the tests the run actually
ran, so one file serves the two-minute gate and a full-suite sweep.

## The ctest entries

Off by default, because the corpus is not in this repository and a test that
silently passes when the corpus is absent is worse than one that is not
registered.

```bash
cmake --preset default -DCTBROWSER_WPT=ON
ctest --preset default -L wpt          # both, ~2 minutes
ctest --preset default -LE wpt         # everything else
```

| test | what it does |
|---|---|
| `wpt-selftest` | the **negative proofs** — five fixtures whose outcomes are asserted. Seconds |
| `wpt-gate` | a fixed subset against `expectations.txt`, budgeted at two minutes |

The **full** run is a documented command and not a build step: it takes tens of
minutes and belongs to a person.

## The negative proofs

**A harness that has only ever been seen to report passes is not evidence of
anything.** `tools/wpt/run-wpt.py --selftest` runs five pages from
`tools/wpt/selftest/` and **asserts** each outcome and each subtest count —
never reads them off a table.

    fixture                              want            got   subtests
    must-pass.html                       PASS           PASS   2 {PASS: 2}      ok
    must-fail.html                       FAIL           FAIL   2 {FAIL:1,PASS:1} ok
    never-done.html                   TIMEOUT        TIMEOUT   1 {PASS: 1}      ok
    throws-on-load.html         HARNESS_ERROR  HARNESS_ERROR   0                ok
    no-harness.html             HARNESS_ERROR  HARNESS_ERROR   0                ok

**All five reported the outcome they must**, measured 2026-09-02 and again on
2026-09-03 after six DOM changes. Three of them did not, the first time they
were run, and each miss was a real engine defect — see the table above.

### And the gate itself, proved both ways

A gate nobody has watched fail is not a gate. Re-proved on 2026-09-03 against
the current expectations file, on the `--gate` subset:

| what was done to `expectations.txt` | gate says | exit |
|---|---|---:|
| nothing | `matches expectations.txt exactly` | 0 |
| deleted one SUBTEST line of a test that fails | `+ dom/events/Event-dispatch-click.html SUBTEST FAIL "basic with click()"` | 1 |
| added a `FAIL` line for a test that passes | `1 UNEXPECTED PASS(ES): - dom/events/Event-type.html FAIL` | 1 |
| restored | `matches expectations.txt exactly` | 0 |

**A line that names a test outside the `--gate` subset proves nothing**, which
the first attempt at this found: deleting
`dom/events/AddEventListenerOptions-passive.any.js FAIL` left the gate green,
because `--check` and `--gate` scope the comparison to the tests the run
actually ran. That is the documented behaviour and it is what lets one file
serve both the two-minute gate and a full-suite sweep — but it means a
falsification has to pick a line the gate will actually visit, or it silently
asserts nothing.


`tools/wpt/selftest/README.md` says what each one is for. The two that matter
most: `must-fail.html` contains one assertion that is false on purpose beside one
that passes, so a runner that called the whole file bad would be caught as
surely as one that called it good; and `no-harness.html` loads the report hook
*without* `testharness.js`, which is exactly the shape every test in the corpus
had before the document root existed — zero subtests, no error, and a naive
runner scores it green.

## Moving the pin

Deliberate, with a re-baseline attached. WPT adds and renames tests every day and
`expectations.txt` is keyed on test paths.

```bash
# 1. edit WPT_COMMIT in tools/wpt/fetch-wpt.sh
tools/wpt/fetch-wpt.sh
# 2. re-measure every suite in the baseline table, one at a time
tools/wpt/run-wpt.py --dir dom/nodes --update-expectations
# 3. update the table in this file, with the date and the real numbers
```

`run-wpt.py` refuses to run if the checkout's `HEAD` is not the commit
`fetch-wpt.sh` names. A table read against the wrong corpus is worse than no
table, so the two are compared rather than assumed.
