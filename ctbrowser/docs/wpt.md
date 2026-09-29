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

## Round fifteen recovery — 2026-09-29

The previous Claude work is merged on `codex-ctbrowser`, through `c0034cb52`:
`401b2af11` merges `claude-ctbrowser` at `27ebe486b` (INPUT, focus and CSS6),
then `c0034cb52` merges `r15-html6` at `8e91ce46a`, including its final
disabled-focus correction. Together these recover 44 implementation commits
on integration base `6ffb9f353`. They are **not yet landed on `ctcompile-v1`**.

These are the preserved Claude measurements, recounted from their JSON files,
**not a new measurement of the combined branch**. Each compares against
`31c3d8ae0`, with WPT `3f6b09ae`, four workers, the default 4096 MiB process
limit and `CTBROWSER_GL_DRIVER=deterministic`. The selections overlap and
must not be summed.

| selection | files PASS before → after | subtests PASS before → after |
|---|---:|---:|
| INPUT/focus, seven directories | 971 → 1,135 | 64,885 → 65,250 |
| CSS6, 25 CSS directories | 1,329 → 1,388 | 40,024 → 40,182 |
| HTML6, seven directories before its root merge | 1,244 → 1,265 | 99,990 → 100,065 |
| HTML6, four surviving measurements after its root merge | 745 → 777 | 22,358 → 22,485 |

The INPUT selection is `dom/events`, `shadow-dom`, `css/selectors`,
`html/semantics/forms`, `css/cssom-view`, `custom-elements` and `selection`.
The four surviving HTML merged measurements cover forms, `dom/nodes`,
`custom-elements` and `domparsing`. Evidence is retained under
`/home/alex/.ctbrowser-auto/claude/ctbrowser/r15/{input,css6,html6}/`.
CSS's saved post-merge build passed 182 steps and its 31 selected CTests;
the INPUT journal records 41 focused CTests passing. No HTML CTest log
survived locally. Fresh combined build, CTests and WPT measurements remain
pending devbox SSH authentication. The local pinned
`tools/format.sh --check` passed: 1,190 C++ files, 155 Python and 116 web.

Next checks must cover the recovered input/focus, CSSOM, forms, frame and
DOM changes. Re-measure `css/selectors/focus-visible-028.html` after
`db5daf22d`, and investigate the remaining select-label focus loss in
`customizable-select/select-mouse-behavior.html`. HTML's File/Blob prototype
separation also exposes two `structured-clone.any.js` failures: deleting
the Blob global breaks cloning and incorrectly permits Blob transfer.
CSS's one recorded file loss is documented in `css-conformance.md`.

Two original worktrees still contain uncommitted recovery work and were
preserved untouched: `r15-vm3` (see `test262.md`) and `ctbrowser-wpt`
(DOM wrapper ephemerons, Range exception propagation and `dom_wrapper_gc`).
Neither is part of these merges. No expectation files changed in recovery;
full CTest, compiler, WPT and test262 suites have not been run.

## Round thirteen and the root's fixes — 2026-09-26

Round thirteen's four agents (CSS4, HTML4, DOM4, VM) were all cut off by the
account's session limit after about three hours and none reported; each
branch was merged whole at its last green gate (VM's gated work in progress
committed first), plus the root's `d96215f1`, `c1d6125c` and `5dc27425`.
Measured at `037fbaae` (combined gate 250/250) against `6011bf8d`, the same
instrument as the outage batch below.

| suite | files PASS `6011bf8d` → `037fbaae` | subtests PASS `6011bf8d` → `037fbaae` |
|---|---:|---:|
| `css` | 2,187 → **2,234** | 99,154 → **101,800** |
| `html` | 869 → **898** | 75,210 → **76,338** |
| `dom` | 436 → **451** | 52,725 → **54,914** |
| `shadow-dom` | 107 → **111** | 14,661 → **14,669** |
| `custom-elements` | 119 → **127** | 3,952 → **4,017** |
| `domparsing` | 24 → **24** | 704 → **704** |
| `selection` | 69 → **75** | 33,736 → **34,008** |
| `url` | 41 → **42** | 9,606 → **9,994** |
| **WPT total** | 3,852 → **3,962** (+110) | 289,748 → **296,444** |
| test262 (ran) | 48,110 → **48,233** (+123) | — |

**Five files lost**, each assigned: `form-double-submit-multiple-targets.html`
by design - HTML's "plan to navigate" keeps ONE planned navigation per form,
and the test's own header says what it checks is unspecified;
`shadow-dom/.../html-forms/test-003.html` (a form in a second document
targeting the main document's iframe no longer loads it twice) and three
css transform/viewport files are round fourteen's first items.

What moved it: `url/failure.html` passes (1,175 subtests - a real Location
object and `window.open()`); `Document-characterSet-normalization-1/2`
(654, ctdrive now answers `encoding.py` as wptserve does);
`Range-insertNode.html` runs (1,537 of 1,840) since the about:blank load
fires during insertion; calc-size() interpolation and a long CSS tail
(`css-conformance.md`); the eight `event-handler-attributes-*` files
(WindowEventHandlers forwarded on `<body>`/`<frameset>`, and WebIDL2's parse
of html.idl no longer quadratic - a failed sticky regex exec searched the
rest of its input); custom elements built by `createElementNS`; and the VM
round (`test262.md`). `Range-surroundContents.html` became a CRASH: it runs
far enough now to reach the 4 GB cap - every Range was a GC root (fixed
after this measurement, below).

**A second instrument: `html/canvas`.** Not in the shared corpus; a sparse
checkout at the same pin in `~/.cache/wpt-canvas` (`/resources/`, `/common/`,
`/html/canvas/element/`, `/html/canvas/resources/`, `/images/`, `/fonts/`),
run as `run-wpt.py --wpt-dir ~/.cache/wpt-canvas --dir html/canvas/element`.
Baseline at `c1d6125c`: **371 of 1,220 runnable files PASS, 987 subtests**
- whole APIs missing (gradients, patterns, `roundRect`, `reset`, `arcTo`,
the context's own curves, `isPointInPath`, line dashes) before any pixel.

## The outage batch: GC pins, UTF-16 strings, testdriver — 2026-09-26

Six pieces of work built and gated on the devbox while `/mnt/c` was
returning EIO, then committed as `30be59df`..`6011bf8d`. Each row was
measured at the tree of the commit named in its column heading, against
the one before it. Same instrument as round eleven: WPT `3f6b09ae`, devbox,
four workers, 4 GB cap, deterministic GL; test262 four workers, 10 s, 2 GB.
The corpus was re-fetched between `5ca8e8a1` and `acc14107` ("The
instrument, widened" below), so that column's subtests include files that
ran for the first time.

| suite | `5ca8e8a1` | GC + WebIDL `acc14107` | UTF-16 `1a6285ad` | `::before` `6f4d39b8` | DOM `1ba73740` | testdriver `db57406e` | activation `6011bf8d` |
|---|---:|---:|---:|---:|---:|---:|---:|
| `css` files | 2,168 | 2,169 | 2,169 | 2,170 | 2,170 | 2,187 | **2,187** |
| `html` files | 835 | 837 | 839 | 839 | 840 | 861 | **869** |
| `dom` files | 409 | 415 | 422 | 422 | 424 | 436 | **436** |
| `shadow-dom` files | 100 | 100 | 100 | 100 | 100 | 107 | **107** |
| `custom-elements` files | 119 | 119 | 119 | 119 | 119 | 119 | **119** |
| `domparsing` files | 24 | 24 | 24 | 24 | 24 | 24 | **24** |
| `selection` files | 57 | 57 | 57 | 57 | 57 | 69 | **69** |
| `url` files | 40 | 41 | 41 | 41 | 41 | 41 | **41** |
| **WPT files PASS** | 3,752 | 3,762 | 3,771 | 3,772 | 3,775 | 3,844 | **3,852** |
| **WPT subtests PASS** | 281,702 | 282,861 | 283,232 | 283,295 | 283,308 | 289,613 | **289,748** |
| runnable files | 5,155 | 5,155 | 5,155 | 5,155 | 5,155 | 5,609 | **5,609** |
| test262 (ran) | 48,057 | 48,062 | 48,110 | 48,110 | 48,110 | — | — |

**Zero WPT files lost at any step, and no test262 file.** (test262 was not
re-run for the last two: they touch no script semantics.) Per step:

- **The GC pin** (`a516032e`). `native_scope` pinned everything allocated
  since the OUTERMOST native began; testharness.js runs each test body
  through `Function.prototype.apply`, so a body's garbage lived as long as
  the body - the six `dom/nodes/NodeList-static-length-getter-tampered-*`
  (4 CRASH, 2 TIMEOUT) reached the 4 GB cap. A native now pins what it
  allocated itself and what its script callees returned to it
  (`reference/header-contracts/script-vm-gc.md`); all six pass, and so do
  `document-write/032.html` and `css/css-values/viewport-units-keyframes`.
- **Every suite under ASan.** Narrowing the pin exposed C++ holding values
  across calls into script with nothing rooting them. All eight WPT suites
  and all of test262 ran on an ASan+UBSan build (`cmake --preset asan`, the
  ASan `ctdrive` as `run-wpt.py --driver`, `ASAN_OPTIONS=log_path=`). Fixed:
  `make_generator`'s eager prologue, `iterable_values`' proxy and
  array-like branches, a frame's CSS animation records (marked nowhere),
  `apply_canvas_font`'s view of a temporary, the tree builder's
  `declarative_shadow_root` pointer into the open-element stack (52 pages)
  and `lone_value` reading past `eof` (257 pages). Afterwards css, html,
  dom, shadow-dom and custom-elements ran with zero sanitizer reports and
  test262 48,060 with none. The full ASan tree is 24 GB and filled the
  devbox once: build only `ctbrowser-tool-ctdrive ctbrowser-tool-ct262`.
- **WebIDL shapes and the globals** (`1f182f77`, `acc14107`).
  `url/idlharness.any.js` 77 of 77 (41): operations enumerable with
  `length` = required arguments and a TypeError for too few
  (`set_operation`), setters' `length` 1, interface objects' `length`,
  `webkitURL`; and an interface object or intrinsic is no longer an
  enumerable global (`realm_record::builtin`). Of this column's +1,159
  subtests, 540 are the six idlharness files the re-fetch made runnable,
  496 the six `event-handler-attributes-*` that read
  `/interfaces/html.idl`.
- **Strings are UTF-16** (`2b710a0d`, `1a6285ad`). Length and every index
  a script sees are code units over the same WTF-8 storage
  (`script/utf16.hpp`). It moved the four `dom/ranges/Range-mutations-*Data`
  files, `Range-selectNode`, `Node-properties`, `document.title-05.html`
  and `atob/base64.any.js` (380 of 380: btoa and atob are isomorphic, and
  throw a DOMException). With the indices right, p5's Friendly Error
  System walks its 4.5 MB source through `charCodeAt` 5.7 million times
  (0.42 million before) and each call built two `current_stack()`
  strings: `p5_ratchet` went 110 s → 338 s. `context::stack_mark` (frame
  depth + innermost `ip`) replaced them: `p5_ratchet` 72.7 s, `p5_api`
  1.1 s (6.7), `babylon_ratchet` 5.3 s (11.3), serially on the devbox.
- **`::before` and `::after` boxes** (`6f4d39b8`): css +1 file, +63
  subtests; six subtests PASS → FAIL inside files that already failed, each
  an accidental pass (a `0px` → `auto` transition read 0 when the pseudo
  box did not exist). Details in `css-conformance.md`.
- **DOM** (`1ba73740`): submit/reset stop at their form
  (`Event-dispatch-single-activation-behavior` 132/132), checkbox
  pre-activation, a spec UTF-8 decoder, `HTMLCollection-as-prototype`.
- **testdriver** (`db57406e`). `run-wpt.py` skipped every file that loads
  testdriver.js; they run now, and runnable went 5,155 → 5,609.
  `tools/wpt/testdriver-vendor.js`, installed over the corpus's at every
  run, implements `click`, `send_keys` and `action_sequence` by queueing
  ctdrive input steps that the run loop replays; the rest of testdriver
  still rejects. Files PASS +69, subtests +6,305 (shadow-dom's alone
  +6,019); `selftest/testdriver.html` proves the input arrives trusted. **Runnable is 5,609 from here on: compare files
  PASS across this line, not pass rates.**
- **User activation** (`6011bf8d`): HTML 6.4.3's transient and sticky
  activation from trusted mousedown/keydown, `navigator.userActivation`,
  and `showPicker()` on `<input>`/`<select>` - html +8 files
  (`show-picker-*`), +135 subtests.

## Round eleven and the root's VM work — 2026-09-26

Round eleven (CSS3, HTML3, DOM3, T262-3; the CSS and DOM agents were cut
off by a usage limit after their last green gate and their branches merged
whole) plus the root's commits since `b9c6662e`, measured at `5ca8e8a1`.
Same instrument: WPT `3f6b09ae`, devbox, four workers, 4 GB cap,
deterministic GL; test262 four workers, 10 s, 2 GB.

| suite | files PASS `b9c6662e` → `5ca8e8a1` | subtests PASS `b9c6662e` → `5ca8e8a1` |
|---|---:|---:|
| `css` | 2,041 → **2,168** | 90,003 → **98,450** |
| `html` | 814 → **835** | 74,214 → **74,387** |
| `dom` | 408 → **409** | 52,396 → **52,407** |
| `shadow-dom` | 98 → **100** | 8,637 → **8,642** |
| `custom-elements` | 88 → **119** | 3,584 → **3,945** |
| `domparsing` | 24 → **24** | 704 → **704** |
| `selection` | 57 → **57** | 33,638 → **33,638** |
| `url` | 39 → **40** | 9,269 → **9,529** |
| **WPT total** | 3,569 → **3,752** (+183) | 272,445 → **281,702** |
| test262 (ran) | 47,425 → **48,057** (+632) | — |

**Zero files lost** in any suite, WPT or test262. What moved it: CSS3's 53
commits (a frame's style sheets are the frame's, `shape()` and path
interpolation, 3D transforms computed as `matrix3d`, grid tracks
interpolated track by track, shorthands split into their longhands, and a
long tail of grammars - `css-conformance.md`); HTML3's form submission through
the form algorithm, Fetch's Headers/Request/Response, XHR decoding and
`<area>`; DOM3's scoped custom element registries and the parser
constructing custom elements; T262-3's immutable bindings, `super` and
`new.target` in direct eval, computed class fields and the rest
(`test262.md`); and the root's event-loop ordering (`promise-rejection-
events.html` 32 → 40 of 43), `import()` rejecting, module namespaces,
a script's `var` non-configurable, and strings counting toward collection
(`document-write/032.html` no longer aborts at the 4 GB cap).

**The instrument, widened (2026-09-26).** `fetch-wpt.sh` now checks out
`/interfaces/` and `/fetch/data-urls/resources/` and makes wptserve's
`/resources/WebIDLParser.js` alias. Seven idlharness files that were
HARNESS_ERROR ran for the first time at `5ca8e8a1`: `css/cssom` 263/497
subtests, `css/cssom-view` 167/417, `css/css-conditional` 32/45,
`container-queries` 14/28, `css/css-cascade` 23/34, `url` 41/77 (all
FAIL), and `selection/idlharness.window.js` TIMEOUT; `html/webappapis/atob/
base64.any.js` went 285/286 → 317/380. The table above predates the
re-fetch; the next measurement includes them.

## Rounds eight to ten, Temporal and direct eval — 2026-09-25

Eight suites and the whole of test262, measured at the session's start
(`c246c9b5`) and at the round-ten merge (`b9c6662e`). Same instrument
throughout: WPT `3f6b09ae`, the devbox, four workers, a 4 GB cap per
driver, `CTBROWSER_GL_DRIVER=deterministic`; test262 four workers, 10 s,
2 GB.

| suite | files PASS `c246c9b5` → `b9c6662e` | of runnable | subtests PASS `c246c9b5` → `b9c6662e` |
|---|---:|---:|---:|
| `css` | 1,648 → **2,041** | 2,926 | 71,216 → **90,003** |
| `html` | 706 → **814** | 1,149 | 73,567 → **74,214** |
| `dom` | 387 → **408** | 504 | 51,827 → **52,396** |
| `shadow-dom` | 66 → **98** | 182 | 8,545 → **8,637** |
| `custom-elements` | 70 → **88** | 180 | 3,471 → **3,584** |
| `domparsing` | 17 → **24** | 69 | 327 → **704** |
| `selection` | 41 → **57** | 96 | 33,332 → **33,638** |
| `url` | 39 → **39** | 49 | 9,269 → **9,269** |
| **WPT total** | 2,974 → **3,569** (+595) | 5,155 | 251,554 → **272,445** |
| test262 (ran) | 42,009 → **47,425** (+5,416) | 49,377 | — |

Between them, the round-nine merge (`178a634f`) and its fixes (`42839a4d`)
measured css 1,882 files / 88,957 subtests, html 777, dom 401, shadow-dom
95, custom-elements 76, domparsing 21, selection 52 (the `m-m4` run), and
test262 42,416 at `178a634f`.

What moved it: rounds eight to ten (four worktree agents each: CSS, HTML
forms, DOM/custom elements/shadow DOM, test262 built-ins and language),
merged and gated by the root; **Temporal** (0 → 4,603 of 4,603 runnable
`built-ins/Temporal` files: ISO calendar, UTC and fixed-offset zones,
exact `__int128` arithmetic, RFC 9557 parsing, relative rounding - see
`test262.md`); and in the root's own areas a frame realm's `Function` and
`eval`, `reportError` at the calling window, timer IDL conversions,
`queueMicrotask` exceptions reported at the callback's window, and
immutable `NaN`/`Infinity`/`undefined` on every global.

**Files lost, `c246c9b5` → `b9c6662e`, each named:**
`css/css-values/random-item-in-container-query.tentative.html` (handed to
round eleven's CSS agent) and `css/cssom/serialize-values.html` (open),
`dom/events/EventListener-handleEvent-cross-realm.html` and
`html/.../promise-rejection-events-iframe.html` (frame realms: the
listener's and the rejection's global), and four test262 files - Array.from
closing its iterator (fixed at `2f073d06`), two `instanceof` with a
primitive left side whose right side is `Function.prototype` (it is not
callable in this engine), and `superclass-static-method-override.js`.

Direct eval (`e764b92c`) took test262 to **47,431**, and the fixes on top
of it to **47,525 at `2123355a`** (+94, zero lost) - see `test262.md`.

## Background shorthand and strict String conversion — 2026-09-18

Full `css/` replay at `fd54d8b1`: **1,647/2,926 runnable files PASS,
71,205 subtests PASS**. Against `bd840fed`: **+7 files and
+70 passing subtests, zero passing files or subtests lost**. Same WPT
`3f6b09ae`, devbox, four workers, 4 GB cap and deterministic GL driver;
213.9 seconds.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `fd54d8b1` | 1,647 | 1,063 | 25 | 0 | 191 | 1,488 | 71,205 / 31,888 |

`b8adba54` expands background layers through the existing longhand grammars,
resets omitted components and serializes them in canonical order. The cascade
uses the same owned expansion. Clip/origin combinations, slash adjacency,
final-layer colors and calculated sizes survive variable substitution and
inheritance. Position validation now checks the shape of typed math rather
than accepting arbitrary functions; image components use the scalar image grammar.
`c381c199` resolves custom-property keywords produced by substitution
through their own cascade before dependent variables read them. Invalid
shorthands reset every governed longhand. Unsupported image functions remain
unsupported during background expansion; `image-set()` still needs its own
implementation and attribute-taint tracking. The first full replay exposed
two passing-subtest losses, and the next replay exposed an internal sentinel
leaking through computed custom properties. `fd54d8b1` returns the
required empty string. All three losses were corrected before this measurement.

`45c943e0` changes JS/VM semantics: strict String conversion calls
ToPrimitive before rejecting Symbols, including boxed Symbols and Symbols
returned by callbacks. String methods stop after failed conversions and
preserve internally caught exceptions. The VM's pending-exception query also
recognizes an exception at a native-call fence, preventing later coercions
from replacing it through `Function.call`. Symbol property keys and explicit
`String(Symbol())` remain supported. `e76272e6` also makes existing VM and AOT call-stack limits raise catchable
`RangeError`s. No limit, bytecode or public signature changed.

Fresh backgrounds measured **91 -> 94 files PASS** and
**5,837 -> 5,892 subtests PASS**, with no losses. Fresh String test262 measured
**1,174 -> 1,181 files PASS**, zero losses.
The CSS unit fixture now correctly rejects a non-final layer color and keeps
a valid layered control. Final focused CTests passed **11/11** (0.26 seconds), and
the browser gate passed **216/216** (93.67 seconds), excluding
compiler tests with `CTCOMPILE_MLIR=OFF`. Formatting passed; all 18
source/test hashes matched the devbox and merged tree. Regressions failed
before their fixes; the runtime controls include forced GC during conversion.

Evidence: `/tmp/ctbrowser29/` contains `browser29-css.json`,
`css-comparison.json`, `measure-css-final.log`, focused backgrounds/String JSON,
`root-comparison.json`, `string-comparison.json`, `root-red.log`,
`root-check.log`, `string-red.log`, `final-gate.log`, `substitution-green.log`,
`final-gate-sentinel-complete.log`, `format-sentinel.log` and `source.sha256`.
No WPT or test262 expectation files changed. Other WPT directories, the rest of test262 and
compiler suites were not replayed.

The final run completed remotely before the SSH interruption. Recovery in
`/tmp/ctbrowser30/recovery.log` verified all source hashes and the output's
completion time before the next devbox build; `recovered-css.json` and
`recovered-css-comparison.json` preserve that result and comparison.

Next: computed background positions, container-relative lengths and relative
image URLs. The background-valid file still demands both `transparent` and
`rgba(0, 0, 0, 0)` for omitted color; these inconsistent assertions remain
visible. Runtime follow-ups include abrupt conversion handling in parseInt
and JSON.rawJSON, String.fromCharCode wrapping and UTF-16/case folding.

## Computed images and String coercion errors — 2026-09-18

Full `css/` replay at `bd840fed`: **1,640/2,926 runnable files PASS,
71,135 subtests PASS**. Against `ad6f9147`: **+3 files and
+31 passing subtests, zero passing files or subtests lost**. Same WPT
`3f6b09ae`, devbox, four workers, 4 GB cap and deterministic GL driver;
201.4 seconds.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `bd840fed` | 1,640 | 1,070 | 25 | 0 | 191 | 1,488 | 71,135 / 31,958 |

`4182a623` clamps radial-gradient radii after math folding and validates
substituted image syntax before calculation results lose their origin.
`9b13ac4c` selects light/dark images using the element's color scheme,
represents a selected `none` as a transparent image, and passes the scheme
to nested colors. Color functions remain gradient stops; quoted URLs use
the existing URL serializer.

`54fb7228` changes JS/VM semantics: `String.fromCodePoint` preserves numeric
conversion errors before range checks, and `String.raw` stops after failed
getters/conversions and rejects direct Symbols at its numeric/string
conversion sites. Internally caught exceptions still allow coercion to
succeed. The raw getter result remains rooted during subsequent calls.
No bytecode or public interfaces changed.

Fresh backgrounds measured **90 -> 91 files PASS** and
**5,833 -> 5,837 subtests PASS**, with no losses. Fresh String test262
measured **1,169 -> 1,174 files PASS**, zero losses; details in `test262.md`.
Added regressions failed before their fixes. Final focused CTests passed
**4/4** (0.13 seconds), the browser gate **216/216** (70.14 seconds), with
compiler tests excluded and `CTCOMPILE_MLIR=OFF`; formatting passed.
All six source/test hashes matched the devbox and merged tree.

Evidence: `/tmp/ctbrowser28/` contains `browser28-css.json`,
`css-comparison.json`, `measure-css.log`, focused backgrounds and String
before/after JSON, `focused-comparison.json`, `string-comparison.json`,
`red.log`, `string-red.log`, `image-red.log`, `final-gate-retry.log`,
`format-final-retry.log` and `source.sha256`. No expectations changed.
Other WPT directories, the rest of test262 and compiler suites were not replayed.

Next: background shorthand expansion leaves longhands empty for
`background: none`. Relative image URLs, container layout, strict ToString
for object conversions and UTF-16 strings remain open.

## Computed layer sizes, cross-fade grammar and URL escapes — 2026-09-18

Full `css/` replay at `ad6f9147`: **1,637/2,926 runnable files PASS,
71,104 subtests PASS**. Against `c7332149`: **+4 files and +38
passing subtests, zero passing files or subtests lost**. Same WPT `3f6b09ae`,
devbox, four workers, 4 GB cap and deterministic GL driver; 200.7 seconds.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `ad6f9147` | 1,637 | 1,073 | 25 | 0 | 191 | 1,488 | 71,104 / 31,989 |

`814baa2c` computes and clamps background/mask size components before
inheritance while retaining the shared math evaluator's random ordinals and
finite bounds. `1ab0f6fb` validates substituted sizes even when mixed
length/percentage math remains unresolved; literal negative components stay
invalid. `b0956d17` validates cross-fade items without rejecting percentages
inside nested images, and handles URL tokens as images. `a6d44a88` consumes
complete URL escapes, including a hexadecimal escape's terminating space.

Fresh focused backgrounds measured **87 -> 90 files PASS** and
**5,815 -> 5,833 subtests PASS**, with no losses. The added regressions
failed before their fixes. Final focused CTests and the browser gate passed;
the browser gate was **216/216** (67.07 seconds), excluding compiler
tests with `CTCOMPILE_MLIR=OFF`. Formatting passed, and source/test hashes
matched the devbox and committed tree.

Evidence: `/tmp/ctbrowser27/` contains `browser27-css.json`,
`css-comparison.json`, `measure-css.log`, focused before/after JSON and
comparisons, `red.log`, `context-red.log`, `mixed-variable-red.log`,
`image-red.log`, `url-red.log`, `gate-complete.log`, `format-complete.log`
and `source.sha256`. No expectations, public interfaces or JS/VM behavior
changed. Other WPT directories, test262 and compiler suites were not replayed.

The `css-values/ch-empty-pseudo-recalc-on-font-load` file also changed
three subtests from TIMEOUT to FAIL; its passing count did not change.

Next: computed radial-gradient sizes still lose their nonnegative range after
math folding. Relative image URL resolution, gradient number precision and
container-relative lengths remain open.

## Border-image sides, layer repetition and size ranges — 2026-09-18

Full `css/` replay at `c7332149`: **1,633/2,926 runnable files PASS,
71,066 subtests PASS**. Against `a7d76d12`: **+8 files and +254
passing subtests, zero passing files or subtests lost**. Same WPT `3f6b09ae`,
devbox, four workers, 4 GB cap and deterministic GL driver; 204.3 seconds.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `c7332149` | 1,633 | 1,077 | 25 | 0 | 191 | 1,488 | 71,066 / 32,024 |

`1119a3c4` expands border-image side lists before animation arithmetic and
uses CSSOM serialization to compress the results. `0b70c015` repeats layers
using each list's original length; the growing vector size had made every
appended item repeat the first layer. `c7332149` marks background and mask
sizes nonnegative so the existing animation clamp handles lengths and pure
percentages, while preserving unresolved mixed calculations.

Fresh focused backgrounds runs measured **80 -> 87 files PASS** and
**5,589 -> 5,815 subtests PASS**, with no losses. Each regression failed
before its fix. The final browser gate passed **216/216** (66.72 seconds),
excluding compiler tests with `CTCOMPILE_MLIR=OFF`; formatting passed.
All four final source/test hashes matched the devbox and committed tree.

Evidence: `/tmp/ctbrowser26/` contains `browser26-final-css.json`,
`final-css-comparison.json`, `measure-final-css.log`, focused JSON/comparisons,
`red.log`, `repetition-red.log`, `size-red.log`, `final-gate.log`,
`format-final.log` and `source.sha256`. The first source commit's full replay
is retained in `browser26-css.json` and `css-comparison.json`.
No expectations, public interfaces or JS/VM behavior changed. Other WPT
directories, test262 and compiler suites were not replayed.

Next: static computed background sizes can still retain negative components
after early math folding; animation ranges are fixed here. Gradient angle
precision uses the shared number serializer and needs a consistent policy.
Relative image URLs and container-relative lengths remain open.

## Animation ranges, addition and repeating gradients — 2026-09-18

Full `css/` replay at `a7d76d12`: **1,625/2,926 runnable files PASS, 70,812 subtests PASS**.
Against `22ff781c`: **+4 files and +185 passing subtests, zero
passing files or subtests lost**. Same WPT `3f6b09ae`, devbox, four workers,
4 GB cap and deterministic GL driver; 203.3 seconds.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `a7d76d12` | 1,625 | 1,085 | 25 | 0 | 191 | 1,488 | 70,812 / 32,278 |

`0e70832e` clamps pure percentage animation results for nonnegative properties,
while retaining mixed length/percentage calculations until their basis exists.
`8bca06f4` adds equal numeric list components during additive composition;
identical keywords still retain their value. `544e8d31` recognizes the existing
repeating-gradient parsers in `CSS.supports()` (merged as `a7d76d12`).

Fresh focused backgrounds runs measured **77 -> 80 files PASS** and
**5,480 -> 5,589 subtests PASS**, with no losses. Each regression failed before
its fix. Final focused CTests passed **2/2** (0.02 seconds), and the browser
gate passed **216/216** (66.47 seconds), excluding compiler tests with
`CTCOMPILE_MLIR=OFF`; formatting passed. All four source/test hashes matched
the devbox and committed tree.

Evidence: `/tmp/ctbrowser25/` contains `browser25-css.json`,
`css-comparison.json`, `measure-css.log`, the focused before/intermediate/after
JSON and comparisons, `red.log`, `addition-red.log`, `gradient-red.log`,
`gate-final.log`, `format-final.log` and `source.sha256`. No expectations,
public interfaces or JS/VM behavior changed. Other WPT directories, test262
and compiler suites were not replayed.

Next: border-image composition still needs list expansion and compression.
Container-relative lengths need actual layout content boxes and size containment:
the browser never installs the style engine's container-size callback.
The investigated flow is in `/tmp/ctbrowser25/container-diagnosis.md`;
no container workaround was added.

## Border-image grammar and computed ranges — 2026-09-18

Full `css/` replay at `22ff781c`: **1,621/2,926 runnable files PASS,
70,627 subtests PASS**. Against `47d281a0`: **+14 files and +209 passing
subtests, zero passing files or subtests lost**. Same WPT `3f6b09ae`,
devbox, four workers, 4 GB cap and deterministic GL driver; 203.0 seconds.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `22ff781c` | 1,621 | 1,089 | 25 | 0 | 191 | 1,488 | 70,627 / 32,463 |

`ca830c3b` validates the border-image components, expands its five longhands,
serializes the slash groups and preserves omitted-value resets. Its numeric
range metadata also applies to animation results. `22ff781c` clamps border
widths after shorthand expansion and computes each image component before
inheritance. Substituted calculations retain their range semantics; literal
negative values remain invalid. The same final clamp also fixes gaps,
padding, flex components and font sizes; the module deltas are in
`css-conformance.md`.

Fresh focused CSS backgrounds runs measured **66 -> 77 files PASS** and
**5,319 -> 5,480 subtests PASS**, with no losses. The added browser regression
failed before the fix, and review exposed a further variable-substitution
case that also failed before its correction. Final focused CTests passed
**2/2** (0.06 seconds), the browser gate **216/216** (69.42 seconds), with
compiler tests excluded and `CTCOMPILE_MLIR=OFF`; formatting passed.
All seven source/test hashes matched the devbox and committed tree. One
intermediate sync failed its hash check before testing and was discarded.

Evidence: `/tmp/ctbrowser24/` contains `browser24-css.json`,
`css-comparison.json`, `measure-css.log`, focused before/after JSON,
`backgrounds-comparison.json`, `red.log`, `image-variable-red.log`,
`green-verified.log`, `gate-final.log`, `format-final.log` and `source.sha256`.
No expectations or JS/VM behavior changed. Other WPT directories, test262
and compiler suites were not replayed.

Next: negative percentage animation results still escape the range clamp;
border-image slice and width interpolation retain failing subtests. Additive
image composition and container-relative units also remain open.

## Border-radius grammar and computed values — 2026-09-18

Full `css/` replay at `47d281a0`: **1,607/2,926 runnable files PASS,
70,418 subtests PASS**. Against `92fda299`: **+5 files and +37 passing
subtests, zero passing files or subtests lost**. Same WPT `3f6b09ae`,
devbox, four workers, 4 GB cap and deterministic GL driver.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `47d281a0` | 1,607 | 1,103 | 25 | 0 | 191 | 1,488 | 70,418 / 32,672 |

`7de3cd0c` validates radius components, expands both axes into corner
longhands, compresses repeated values and shares storage with the WebKit
aliases. Radius range metadata also clamps negative animation results.
`47d281a0` removes the cascade's lossy horizontal-only expansion and computes
each corner component before inheritance, including relative lengths and
negative calculations. The borrowed expansion helper now leaves radius to
the existing owned CSSOM path; its signature is unchanged.

Only `css/css-backgrounds` moved: **61 -> 66 files PASS**, **5,282 -> 5,319
subtests PASS**. The gained files are `border-radius-interpolation`,
`border-radius-computed`, `border-radius-invalid`, `border-radius-valid`
and `webkit-border-radius-valid`. A fresh focused before/after run matched
the full replay. The added cascade regression failed before the fix;
the three focused CTests passed afterward. The browser gate passed
**216/216** (68.82 seconds), excluding compiler tests with
`CTCOMPILE_MLIR=OFF`; formatting passed. All ten source/test hashes matched
the devbox and committed tree.

Evidence: `/tmp/ctbrowser23/` contains `browser23-css.json`,
`css-comparison.json`, `measure-css.log`, `browser23-backgrounds-{before,after}.json`,
`backgrounds-comparison.json`, `red-retry.log`, `green.log`, `gate.log`,
`format-combined.log` and `source.sha256`. No expectations changed.
Other WPT directories and test262 were not replayed; JS/VM behavior is unchanged.

Next: border-image accepts invalid values and lacks shorthand expansion;
negative computed widths also remain. `border-width-computed` still reports
`-10px` for `border-bottom-width: calc(-0.5em + 10px)` where it expects `0px`.

## Column-rule widths and layout coordinates — 2026-09-18

Full `css/` replay at `92fda299`: **1,602/2,926 runnable files PASS,
70,381 subtests PASS**. Against `7cfe95b5`: **+3 files and +6 passing
subtests, zero passing files or subtests lost**. Same WPT `3f6b09ae`,
devbox, four workers, 4 GB cap and deterministic GL driver; 201.2 seconds.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `92fda299` | 1,602 | 1,108 | 25 | 0 | 191 | 1,488 | 70,381 / 32,709 |

`1084fb74` records accumulated translations in the shared coordinate lookup,
so image `x`/`y` and `offsetLeft`/`offsetTop` discard transforms on the element
and its ancestors. `HTMLImageElement-x-and-y-ignore-transforms` now passes.
`7848b325` resolves column-rule width keywords before shorthand serialization,
gaining multicol `inheritance` and `column-rule-computed`, plus one css-gaps
subtest. Rules retain their computed width for `none` and `hidden` styles.

Both added regression tests failed on the unchanged engine and passed with
the fixes. The browser CTest gate passed **216/216** (66.98 seconds), excluding
compiler tests with `CTCOMPILE_MLIR=OFF`; formatting passed. All four changed
source/test hashes matched the devbox and the committed tree.

Evidence: `/tmp/ctbrowser22/` contains `browser22-css.json`,
`css-comparison.json`, `measure-css.log`, `red-retry.log`, `green.log`,
`gate.log`, `format-green.log` and `source.sha256`, plus the focused image
and multicol results. No expectations changed. Other WPT directories and
test262 were not replayed; JS/VM behavior is unchanged.

Next: border-radius accepts invalid values and loses the vertical radii in
computed elliptical shorthands. Multicol's remaining failures concern
pseudo-element selectors; image zoom/inline placement also remains open.

## Column-wrap and transformed geometry — 2026-09-18

Full `css/` replay at `7cfe95b5`: **1,599/2,926 runnable files PASS,
70,375 subtests PASS**. Against `39f651a3`: **+3 files and +47 passing
subtests, zero passing files or subtests lost**. Same WPT `3f6b09ae`,
devbox, four workers, 4 GB cap and deterministic GL driver.

| css | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | subtests PASS / FAIL |
|---|---:|---:|---:|---:|---:|---:|---:|
| `7cfe95b5` | 1,599 | 1,111 | 25 | 0 | 191 | 1,488 | 70,375 / 32,715 |

`7cbc05fa` adds `column-wrap` and its reset through `columns`, gaining
`column-wrap-reset-interpolation` and `discrete-no-interpolation`.
`4d93744f` applies composed 2D transforms to both client-rectangle APIs,
fixing `viewport-relative-lengths-scaled-viewport`. `7cfe95b5` shares that
geometry with quads and coordinate conversions, retaining rotated corners.
The shared transform parser still models 2D transforms with px lengths;
3D transforms and relative transform lengths remain open.

The first full replay exposed seven GeometryUtils subtest losses; the
shared geometry correction restores all seven and gains two more. A
separate regression preserves infinite coordinates in `DOMQuad.fromRect`.
No expectations changed. The final browser CTest gate passed **216/216**
(70.52 seconds), excluding compiler tests with `CTCOMPILE_MLIR=OFF`;
formatting passed and all nine changed source/test hashes matched the devbox.

Evidence: `/tmp/ctbrowser21/` contains `browser21-corrected-css.json`,
`corrected-css-comparison.json`, `measure-corrected-css.log`,
`final-browser-gate.log`, `last-format.log` and `corrected-source.sha256`.
The initial replay and its seven losses remain in `browser21-final-css.json`
and `css-comparison.json`. Other WPT directories and test262 were not replayed;
the preceding runtime measurements remain unchanged.

## Detailed sections

The following sections retain the complete original text and measurements.

<a id="columns-and-runtime-recovery--2026-09-18"></a>
- [Columns and runtime recovery — 2026-09-18](wpt/01-columns-and-runtime-recovery--2026-09-18.md#columns-and-runtime-recovery--2026-09-18)
<a id="positive-integer-animation-follow-up--2026-09-18"></a>
- [Positive integer animation follow-up — 2026-09-18](wpt/01-columns-and-runtime-recovery--2026-09-18.md#positive-integer-animation-follow-up--2026-09-18)
<a id="css-recovery--2026-09-18"></a>
- [CSS recovery — 2026-09-18](wpt/01-columns-and-runtime-recovery--2026-09-18.md#css-recovery--2026-09-18)
<a id="wide-baseline--2026-09-18-round-seven-recovered"></a>
- [Wide baseline — 2026-09-18: round seven recovered](wpt/01-columns-and-runtime-recovery--2026-09-18.md#wide-baseline--2026-09-18-round-seven-recovered)
<a id="the-baseline--2026-09-18-round-seven-merged-five-suites--test262"></a>
- [The baseline — 2026-09-18: round seven merged (five suites + test262)](wpt/01-columns-and-runtime-recovery--2026-09-18.md#the-baseline--2026-09-18-round-seven-merged-five-suites--test262)
<a id="the-baseline--2026-09-17-midday-round-six-merged"></a>
- [The baseline — 2026-09-17, midday: round six merged](wpt/01-columns-and-runtime-recovery--2026-09-18.md#the-baseline--2026-09-17-midday-round-six-merged)
<a id="the-baseline--2026-09-16-night-round-five-merged"></a>
- [The baseline — 2026-09-16, night: round five merged](wpt/01-columns-and-runtime-recovery--2026-09-18.md#the-baseline--2026-09-16-night-round-five-merged)
<a id="the-baseline--2026-09-16-evening-round-four-merged"></a>
- [The baseline — 2026-09-16, evening: round four merged](wpt/01-columns-and-runtime-recovery--2026-09-18.md#the-baseline--2026-09-16-evening-round-four-merged)
<a id="and-the-wide-corpus-at-the-same-sha"></a>
- [And the wide corpus at the same SHA](wpt/01-columns-and-runtime-recovery--2026-09-18.md#and-the-wide-corpus-at-the-same-sha)
<a id="the-baseline--2026-09-16-afternoon-rounds-two-and-three-merged"></a>
- [The baseline — 2026-09-16, afternoon: rounds two and three merged](wpt/01-columns-and-runtime-recovery--2026-09-18.md#the-baseline--2026-09-16-afternoon-rounds-two-and-three-merged)
<a id="and-the-wide-corpus-at-the-same-sha-1"></a>
- [And the wide corpus at the same SHA](wpt/01-columns-and-runtime-recovery--2026-09-18.md#and-the-wide-corpus-at-the-same-sha-1)
<a id="the-baseline--2026-09-16-the-five-suites-after-the-round-one-merges"></a>
- [The baseline — 2026-09-16, the five suites after the round-one merges](wpt/01-columns-and-runtime-recovery--2026-09-18.md#the-baseline--2026-09-16-the-five-suites-after-the-round-one-merges)
<a id="the-baseline--2026-09-13-the-widened-corpus"></a>
- [The baseline — 2026-09-13, the widened corpus](wpt/01-columns-and-runtime-recovery--2026-09-18.md#the-baseline--2026-09-13-the-widened-corpus)
<a id="the-baseline--2026-09-13-small-hours"></a>
- [The baseline — 2026-09-13, small hours](wpt/01-columns-and-runtime-recovery--2026-09-18.md#the-baseline--2026-09-13-small-hours)
<a id="the-baseline--2026-09-12-night"></a>
- [The baseline — 2026-09-12, night](wpt/01-columns-and-runtime-recovery--2026-09-18.md#the-baseline--2026-09-12-night)
<a id="the-baseline--2026-09-12-evening"></a>
- [The baseline — 2026-09-12, evening](wpt/01-columns-and-runtime-recovery--2026-09-18.md#the-baseline--2026-09-12-evening)
<a id="the-baseline--2026-09-12-late"></a>
- [The baseline — 2026-09-12, late](wpt/01-columns-and-runtime-recovery--2026-09-18.md#the-baseline--2026-09-12-late)
<a id="the-baseline--2026-09-12-afternoon"></a>
- [The baseline — 2026-09-12, afternoon](wpt/02-the-baseline--2026-09-12-afternoon.md#the-baseline--2026-09-12-afternoon)
<a id="what-is-standing-in-front-of-the-most-tests-now"></a>
- [What is standing in front of the most tests now](wpt/02-the-baseline--2026-09-12-afternoon.md#what-is-standing-in-front-of-the-most-tests-now)
<a id="the-baseline--2026-09-10-late"></a>
- [The baseline — 2026-09-10, late](wpt/02-the-baseline--2026-09-12-afternoon.md#the-baseline--2026-09-10-late)
<a id="what-is-standing-in-front-of-the-most-tests-now-1"></a>
- [What is standing in front of the most tests now](wpt/02-the-baseline--2026-09-12-afternoon.md#what-is-standing-in-front-of-the-most-tests-now-1)
<a id="and-the-two-that-were-not-measured"></a>
- [And the two that were NOT measured](wpt/02-the-baseline--2026-09-12-afternoon.md#and-the-two-that-were-not-measured)
<a id="the-baseline--2026-09-10-night"></a>
- [The baseline — 2026-09-10, night](wpt/02-the-baseline--2026-09-12-afternoon.md#the-baseline--2026-09-10-night)
<a id="the-baseline--2026-09-10-evening"></a>
- [The baseline — 2026-09-10, evening](wpt/02-the-baseline--2026-09-12-afternoon.md#the-baseline--2026-09-10-evening)
<a id="the-baseline--2026-09-10-morning"></a>
- [The baseline — 2026-09-10, morning](wpt/02-the-baseline--2026-09-12-afternoon.md#the-baseline--2026-09-10-morning)
<a id="what-is-standing-in-front-of-the-most-tests-now-2"></a>
- [What is standing in front of the most tests now](wpt/02-the-baseline--2026-09-12-afternoon.md#what-is-standing-in-front-of-the-most-tests-now-2)
<a id="the-previous-baseline--2026-09-07-night"></a>
- [The previous baseline — 2026-09-07, night](wpt/02-the-baseline--2026-09-12-afternoon.md#the-previous-baseline--2026-09-07-night)
<a id="and-what-the-same-commit-says-about-the-suite"></a>
- [And what the same commit says about the SUITE](wpt/02-the-baseline--2026-09-12-afternoon.md#and-what-the-same-commit-says-about-the-suite)
<a id="the-previous-baseline--2026-09-07-evening"></a>
- [The previous baseline — 2026-09-07, evening](wpt/02-the-baseline--2026-09-12-afternoon.md#the-previous-baseline--2026-09-07-evening)
<a id="what-moved-and-what-it-cost"></a>
- [What moved, and what it cost](wpt/02-the-baseline--2026-09-12-afternoon.md#what-moved-and-what-it-cost)
<a id="and-one-regression-caused-and-fixed-inside-the-session"></a>
- [And one regression, caused and fixed inside the session](wpt/02-the-baseline--2026-09-12-afternoon.md#and-one-regression-caused-and-fixed-inside-the-session)
<a id="the-previous-baseline--2026-09-07-day"></a>
- [The previous baseline — 2026-09-07, day](wpt/02-the-baseline--2026-09-12-afternoon.md#the-previous-baseline--2026-09-07-day)
<a id="what-moved-and-what-it-cost-1"></a>
- [What moved, and what it cost](wpt/02-the-baseline--2026-09-12-afternoon.md#what-moved-and-what-it-cost-1)
<a id="and-six-that-went-pass---fail-every-one-of-them-diagnosed"></a>
- [And six that went PASS -> FAIL, every one of them diagnosed](wpt/02-the-baseline--2026-09-12-afternoon.md#and-six-that-went-pass---fail-every-one-of-them-diagnosed)
<a id="the-previous-baseline-2026-09-03"></a>
- [The previous baseline, 2026-09-03](wpt/02-the-baseline--2026-09-12-afternoon.md#the-previous-baseline-2026-09-03)
<a id="the-previous-baseline-2026-09-02-for-comparison"></a>
- [The previous baseline, 2026-09-02, for comparison](wpt/02-the-baseline--2026-09-12-afternoon.md#the-previous-baseline-2026-09-02-for-comparison)
<a id="what-is-standing-in-front-of-the-most-tests-now-3"></a>
- [What is standing in front of the most tests now](wpt/02-the-baseline--2026-09-12-afternoon.md#what-is-standing-in-front-of-the-most-tests-now-3)
<a id="the-earlier-handoff-from-the-2026-09-07-day-run"></a>
- [The earlier handoff, from the 2026-09-07 day run](wpt/02-the-baseline--2026-09-12-afternoon.md#the-earlier-handoff-from-the-2026-09-07-day-run)
<a id="skips-all-542-of-them"></a>
- [Skips, all 542 of them](wpt/02-the-baseline--2026-09-12-afternoon.md#skips-all-542-of-them)
<a id="the-corpus"></a>
- [The corpus](wpt/02-the-baseline--2026-09-12-afternoon.md#the-corpus)
<a id="which-suites-and-why"></a>
- [Which suites, and why](wpt/02-the-baseline--2026-09-12-afternoon.md#which-suites-and-why)
<a id="the-results-hook"></a>
- [The results hook](wpt/02-the-baseline--2026-09-12-afternoon.md#the-results-hook)
<a id="how-console-output-and-errors-reach-the-terminal-today"></a>
- [How console output and errors reach the terminal today](wpt/02-the-baseline--2026-09-12-afternoon.md#how-console-output-and-errors-reach-the-terminal-today)
<a id="what-wpt-found-in-the-engine"></a>
- [What WPT found in the engine](wpt/02-the-baseline--2026-09-12-afternoon.md#what-wpt-found-in-the-engine)
<a id="and-six-more-found-in-the-second-pass"></a>
- [And six more, found in the second pass](wpt/02-the-baseline--2026-09-12-afternoon.md#and-six-more-found-in-the-second-pass)
<a id="and-the-two-that-were-not-fixed-then-both-of-which-are-now"></a>
- [And the two that were NOT fixed then, both of which are now](wpt/02-the-baseline--2026-09-12-afternoon.md#and-the-two-that-were-not-fixed-then-both-of-which-are-now)
<a id="running-it"></a>
- [Running it](wpt/02-the-baseline--2026-09-12-afternoon.md#running-it)
<a id="timeouts-and-the-memory-cap"></a>
- [Timeouts and the memory cap](wpt/02-the-baseline--2026-09-12-afternoon.md#timeouts-and-the-memory-cap)
<a id="anyjs-and-the-wrappers"></a>
- [`.any.js` and the wrappers](wpt/02-the-baseline--2026-09-12-afternoon.md#anyjs-and-the-wrappers)
<a id="what-is-skipped-and-why"></a>
- [What is skipped, and why](wpt/02-the-baseline--2026-09-12-afternoon.md#what-is-skipped-and-why)
<a id="expectations-and-the-gate"></a>
- [Expectations, and the gate](wpt/02-the-baseline--2026-09-12-afternoon.md#expectations-and-the-gate)
<a id="the-ctest-entries"></a>
- [The ctest entries](wpt/02-the-baseline--2026-09-12-afternoon.md#the-ctest-entries)
<a id="the-negative-proofs"></a>
- [The negative proofs](wpt/03-the-negative-proofs.md#the-negative-proofs)
<a id="and-the-gate-itself-proved-both-ways"></a>
- [And the gate itself, proved both ways](wpt/03-the-negative-proofs.md#and-the-gate-itself-proved-both-ways)
<a id="moving-the-pin"></a>
- [Moving the pin](wpt/03-the-negative-proofs.md#moving-the-pin)
