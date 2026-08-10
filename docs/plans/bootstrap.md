# Bootstrap 5.3.8 at Chrome parity

**Where it is: S0 DONE, both halves. The Chrome comparison measures and its
numbers are recorded in `tools/check/css-parity.txt`; `bootstrap_layout` pins the
same table without a browser and is in the suite (84/84). S1, the CSS front end,
is next.**

`vendor/bootstrap/bootstrap.css` is the first real-world stylesheet this engine
has been pointed at. Every CSS test before it was a hand-written inline literal
of under ten lines, authored by somebody who already knew what the engine
supported — which is not evidence. This plan is the ladder from there to "a
Bootstrap page renders the way Chrome renders it", measured at every rung.

Read `vendor/README.md` for why a fourth corpus and why this one is not
JavaScript, and `docs/tools.md` for the two halves of the harness.

## What was measured on contact

Bootstrap **parses** without crashing, truncating or producing garbage: 2,965
selectors, ~6,289 (selector × declaration) entries, 5 `@keyframes`, 1 `@charset`.
Structurally ctcss holds. Then very little happens.

| | |
|---|---|
| `var()` uses / custom-property definitions | **1,370 / 1,185** — none substituted; there is no custom-property support anywhere |
| `:root, [data-bs-theme=light]` | **dropped whole.** `:root` is an unknown pseudo, so the alternative is `impossible` (`external/compile-time-css/include/ctcss/value.hpp:195`), and `[data-bs-theme=light]` is an attribute selector, which is not modelled. All 128 global `--bs-*` are unreachable |
| selectors that can never match | **303 of 2,965**, carrying ~650 declarations: 93 attribute, 123 pseudo-element, 119 functional, 48 `+`, 27 `~` |
| `@media` blocks | **108 of 109 flattened in unconditionally** — the prelude is substring-matched for `portrait`/`print` and nothing else (`value.hpp:364`). Every breakpoint applies at once and the last in source order wins |
| `calc()` | **134**, none evaluated |
| units | `rem` has a hardcoded 16px root (`layout/values.hpp:120`); `vh`/`vw`/`pt` fall through to `unit::none` and are treated as **px** |
| properties layout+paint consume | **22.** No `position`, `float`, `flex`, `box-sizing`, `min/max-width`, `line-height`, `text-align`, `z-index`, `opacity`, `border-radius`, `box-shadow`, `transform` |
| shorthands expanded | **`margin` and `padding` only** — so `border: 1px solid #dee2e6`, Bootstrap's commonest border form, produces neither `border-width` nor `border-color` and draws nothing |
| inheritance | **not in the cascade at all.** Five ad-hoc channels threaded as parameters through `box_builder` and `paint::recorder`. `inherit`/`initial`/`unset`/`revert` unimplemented |
| `margin: 0 auto` | does not centre — both autos resolve to 0 (`layout/values.hpp:121`) |

Of ~6,289 declarations roughly **3,344 (52%)** reach a consumer in a readable
form, and the missing half is not spread evenly: it is concentrated in the 5.3
component layer, which is built entirely on `--bs-*`. Buttons, cards, forms,
navs, modals, tables, alerts and badges render essentially unstyled while Reboot
and the plain-literal utilities work.

## What the harness says now

`tools/check/css-parity.py` on `bootstrap-box.html`, the smallest fixture — 40
elements, 48 compared columns:

```
BY CAUSE
  27 elements share a @x offset of +44px      first at html>body:1>div:0>h1:0
  @h: 35 of 40 differ                         line-height: 34 of 40 differ
  height: 35 of 40 differ                     text-align:  34 of 40 differ

ELEMENT                PROPERTY          ctbrowser                   chrome
html                   @w                1009                        1024
body                   line-height       var(--bs-body-line-height)  24
body                   color             var(--bs-body-color)        rgb(33, 37, 41)
div.container.probe    @x                0                           32
div.container.probe    max-width         1320                        960
div.container.probe    margin-left       auto                        32
div.container.probe    padding-right     auto                        12

40 elements, 48 properties: 343 differ / 1920, substituted=1354
```

Every line of that is a predicted failure arriving on schedule, which is the
first evidence that the harness measures what it claims to:

- **`var(--bs-body-line-height)` as a literal value** — the headline gap. Note
  that the inheritance walk *worked*: those are `body`'s declarations, found by
  `getComputedStyle` walking ancestors.
- **`max-width: 1320` where Chrome says `960`** — `@media` flattening, with the
  `xxl` breakpoint winning at a 1024 viewport.
- **`margin-left: auto` and `@x: 0` where Chrome centres at 32** — no
  auto-margin resolution.
- **`padding-right: auto`** — `calc(var(--bs-gutter-x) * .5)` fails
  `parse_length`, which returns a default-constructed `length` whose unit is
  `auto_`.
- **`substituted=1354` of 1920** — 70% of compared values ctbrowser did not
  answer at all. This is the number that stops "no difference" being mistaken
  for "implemented", and it is ratcheted alongside `differ`.

### Two findings that were NOT predicted

1. **`clientWidth` disagrees with the width layout actually used.**
   `browser::run_layout` re-runs layout at `width - scrollbar_width` when a page
   overflows (`src/shell/browser.cpp`), but `documentElement.clientWidth` still
   reports the full viewport (`src/shell/bindings/element.cpp`). So the harness's
   viewport cross-check passes — both engines say 1024 — while every `@x` and
   `@w` carries a 15px error. One of the two is wrong and they cannot both stay.
   Fold into **S7**, where the box model is already being touched.

2. **`tools/remote-build.sh` deleted `tools/.venv`.** The rsync is `--delete`
   with no protect filter for it, so every remote build destroyed the Playwright
   venv and the failure surfaced later as "playwright not installed". Fixed by
   adding the same exclude/protect pair `third_party/angle/` already had.
   `tools/check/compare.py` also still looked for `build/src/examples/ctdrive`,
   a path the 2026-08-09 reorg moved; it now tries both.

## The decisions this plan rests on

1. **The CSS front end gets rewritten inside ctbrowser** as `src/style/css/` — a
   real CSS Syntax Level 3 tokenizer, full selector grammar, component-value
   model, at-rules with real conditions — and `external/compile-time-css` is
   retired from the tree. Exactly the precedent of 2026-07-27, when cthtml
   stopped being a submodule because the DOM needed a real WHATWG tokenizer.
   ctcss's model has no room for attribute selectors, component values, at-rule
   conditions or nesting, and its constexpr-first contract is what made it a
   brace-and-semicolon splitter. compile-time-css survives as its own repository.
2. **All the way up a measured ladder**, one rung at a time, each QA'd against
   Chrome before the next. A halt can be called at any rung and everything below
   it is finished and gated.
3. **The machine gate is a computed-style and box-geometry diff, not a pixel
   diff.** Pixel-identical to Chrome is not reachable — different text
   rasteriser. Property-identical is.
4. **`bootstrap.bundle.js` is vendored and out of scope.** Pinned beside the CSS
   so the pair travels together; nothing measures it yet.

## The harness

Two artefacts, and the split is the design.

**Local, read by a person: `tools/check/css-parity.py`.** Needs Chrome. Drives
ctbrowser and Chromium through `compare.py`'s daemon, runs one dump script in
both, normalises both sides identically and diffs. Deliberately **not a ctest** —
`docs/build.md`: *"a browser-versus-browser diff should be read, not silently
failed."* Two numbers per fixture, both ratcheted, and `--advance` is the only
thing that writes `tools/check/css-parity.txt`, because a test that edits its own
expectations cannot fail.

- `differ` falls as layout gets right.
- `substituted` falls as properties get modelled.

**Everywhere, gated automatically: `tests/unit/bootstrap_layout.cpp`** (S0's one
remaining piece). Builds each fixture through `shell::browser` headless with
`font8x8_metrics`, emits ctbrowser's own side of the same table from C++, and
byte-compares `tests/baseline/bootstrap-*.txt`. No Chrome, so it runs on the
devbox. The relationship between the halves is the point: the baseline needs no
Chrome, and what the Chrome comparison gives you is the knowledge that the
baseline is *right*. A text baseline also names the element and the property that
moved, which a `.ppm` diff cannot.

The compared property set lives in exactly one place — `PROPS` in
`css-parity.py`, with a `PROPS_VERSION` the record file pins. `--emit-props`
writes `css-parity-props.txt` for the C++ side so there is no second list to
drift. Longhands only: Chrome reconstructs shorthands with rules that differ
between engines and between its own versions.

**Geometry epsilon is 1/64 px and it is not a knob** — Chrome's `LayoutUnit`
quantum, the smallest difference Chrome can represent. The ratchet is on the
*count*, never on the epsilon, so a 0.5px difference is a recorded difference
rather than "within tolerance".

**Fixtures** are six pages in `examples/pages/`, small on purpose: a
1,000-element page produces a report nobody reads, and when a rung lands you want
to know which component moved. They link the stylesheet with `<link
rel="stylesheet">` rather than inlining it, so **both engines parse the same
document** — inlining for ctbrowser only would destroy the comparison. Viewport
1024×768: Bootstrap's breakpoints are 576/768/992/1200, and 1024 sits 32px inside
`lg`, clear of a boundary.

## The ladder

| # | Rung | Gate |
|---|---|---|
| **S0** | **Harness.** `getComputedStyle`; `<link rel=stylesheet>` + a `style_error` channel; `css-dump.js`; `css-parity.py` + ratchet; six fixtures; `bootstrap_layout.cpp` + `tests/baseline/`; the `ctdrive` `reply()` fix; `compare.py`'s `request()` extraction; `box_of`/`find_id` → `tests/support/dom_probe.hpp` | **DONE.** 84/84 green, formatting clean, numbers recorded. Both halves verified able to FAIL: the ratchet exits 1 when a count rises, and `bootstrap_layout` exits 1 naming the element and property that moved |
| **S1** | **Tokenizer + component values + grammar**, feeding the existing cascade unchanged. ctcss leaves `engine.hpp`. Plus the perf fixes everything depends on: one rule per declaration *block*, O(1) `put()`, values as views, `specificity` in `rule`, the ancestor-facts stack | All 15 `style_basics` tests pass **unchanged**; rule count 6,289 → 2,965. A pure front-end substitution, so a regression is unambiguous |
| **S2** | **Selector engine.** Attributes (6 ops + `i`/`s`), `+`/`~`, `:not`/`:is`/`:where`, `:root`, structural pseudos, `nth-child(An+B)`, sibling-facts stack, bucketing, packed specificity, drop-the-rule for unknown pseudo-elements | Bootstrap selectors that can match: ~89% → **~100%** |
| **S3** | **Computed values.** `style::value`, the property table, **real inheritance**, `inherit`/`initial`/`unset`/`revert`, split interning. `layout/values.hpp` and `paint/values.hpp` lose their parsers; the five inheritance channels collapse into one — and `computed_style.cpp`'s ancestor walk becomes a straight read | Sharing rate per half; **goldens must not move** — that is the test. Riskiest rung: run both paths with an equality assertion for its duration |
| **S4** | **`var()` + `calc()` + units.** Custom-property cascade, substitution, IACVT, cycles, the two-pass order; `em`/`rem`/`vh`/`vw`/`pt` folding against a real root font-size; shorthand expansion moved to cascade time | All 1,370 `var()` resolve; the hardcoded 16 is gone; `test_shorthands_expand` passes verbatim |
| **S5** | **`@media` with a real environment** + resize re-evaluation; `@supports`, `@layer`, `@charset`, `@import` | The page at 375px and at 1400px differs the way Chrome's does; `dirty::styles` marked on resize **only** when a query flipped |
| **S6** | **The rest of the shorthand table**; **`::before`/`::after` + `content`**; **retire ctcss** | Shorthand coverage count; form-check marks and dropdown carets appear; `check-package.sh` green |
| **S7** | **Box model.** `box-sizing`, borders in `resolved_edges`, `min/max-width`, `min/max-height`, auto-margin centring — and the `clientWidth`/layout-width inconsistency above | `bootstrap-box.html` → near zero; the amended `layout_basics` **pair**; **zero golden movement expected — verify** |
| **S8** | **Text metrics.** `line-height` (replacing the hardcoded `line_height_factor = 1.25f`), `text-align`, `vertical-align` | `bootstrap-type.html`; **moves every text golden** — taken early on purpose |
| **S9** | **Flex**, including the `run_parallel` independence guard | `bootstrap-grid.html` → near zero; ~20 tests in a new `flex_basics.cpp`; parallel-equals-sequential extended with a flex case |
| **S10** | **De-replace `<button>`** — out of `is_replaced_tag`, its intrinsic sizing moved into the UA sheet as real `padding`/`border` so the cascade can override it | `bootstrap-components.html`; **moves `widgets`, `elements`** |
| **S11** | **Position and stacking**, in two sub-rungs: in-layer `z-index`, then fixed/sticky as their own layers | `bootstrap-position.html`; new `position_basics.cpp` |
| **S12** | **Paint decoration.** Per-side `border-{width,style,color}`, `border-radius`, `box-shadow`, `opacity`, `visibility`, `outline` | Screenshots **by eye**; `bootstrap.ppm`; new `raster_basics` coverage cases. **Moves `widgets`, `elements`, `page.ppm`** |
| **S13** | **`float`/`clear`/`overflow`**, then **`transform`/`transition`/animation** last — a transition makes the frame time-dependent, which no byte-compared golden can hold | Fixtures pin `transition: none` until this rung, with a comment saying why |

Three golden-moving events — **S8, S10, S12** — and they are three commits.
Taking the line-height hit at S8 rather than after flex is why it sits there: if
it lands later, `widgets.ppm` moves once for reasons you cannot separate, and
"did flex break the heading spacing or did line-height?" stops being answerable
from the image. Canvas-drawing goldens are unaffected by all three — a canvas is
a replaced element whose box does not depend on line height.

**Predict first, regolden second.** The commit message names which goldens will
move *before* the run; an unpredicted golden moving stops the commit. That is the
only way an image golden catches a second, unintended change riding along with an
intended one. And the text baseline is the primary review artefact —
`git diff tests/baseline/` names the element and the property.

## Per rung

```bash
tools/check/css-parity.py --all examples/pages/bootstrap-grid.html   # is it right?
tools/check/compare.py shot grid-after                              # does it LOOK right?
tools/check/css-parity.py --advance                                 # record it
REGOLDEN=1 ctest --preset default -R bootstrap_layout               # if predicted
git diff tests/baseline/                                            # and READ it
tools/format.sh --check && ./tools/remote-build.sh                  # GCC 13, no SDL
```

Measure with **callgrind, not wall clock** — `docs/performance.md` is explicit
that wall clock here varies ±10% and that a change once looked like 10% and was
0.3%.

### Performance targets, stated so they can fail

| target | value |
|---|---|
| `add_sheet(bootstrap)` parse + compile | **< 15 ms**, **< 3 MB** retained, single-threaded |
| `resolve_all` on a 2,000-element page | **< 8 ms** (< 4 µs/element) |
| distinct computed styles | **< 15%** of element count; distinct **inherited** halves **< 60** |
| one hover | **< 0.2 ms** — a subtree re-resolve, not a document one |
| declaration bag | **≤ 20 bytes/entry** (36 + a heap allocation today) |

A hover currently re-resolves the whole document
(`src/shell/browser.cpp`). Record per sheet whether any selector has a state
requirement on a *non-subject* compound; for Bootstrap that is false, so a hover
can re-resolve only that element and its descendants.

## The assertions that must change, and why they were wrong

- **`tests/unit/style_basics.cpp` `test_unmatched_element_gets_empty_style`**
  becomes wrong at S3: with real inheritance an `<em>` inherits `color` from the
  UA sheet's `body`. That assertion encoded *the absence of inheritance* as
  though it were a rule. Rewrite it to: an empty **own** half, and an inherited
  half whose `color` is the UA's.
- **The sentinel values `color: tag` / `class` / `id` / `author` / `ua`.** At S3
  an invalid value for a known property is dropped at parse time. Change them to
  real colours **at S1**, so the change is separated from the behaviour it would
  otherwise be confused with. Those tests are about cascade *ordering*, and using
  an invalid value as a sentinel is exactly what a real property table makes
  impossible.
- **`test_shorthands_expand`** and **`test_identical_styles_are_shared`** must
  pass **verbatim throughout** — they are the acceptance tests for the two
  hardest decisions, cascade-time expansion and split interning.
