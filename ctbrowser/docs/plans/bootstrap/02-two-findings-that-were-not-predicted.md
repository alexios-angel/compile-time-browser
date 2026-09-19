[Back to bootstrap.md](../bootstrap.md)

### Two findings that were NOT predicted

1. **`clientWidth` disagrees with the width layout actually used.**
   `browser::run_layout` re-runs layout at `width - scrollbar_width` when a page
   overflows (`ctbrowser/lib/Shell/browser/frame.cpp`), but `documentElement.clientWidth` still
   reports the full viewport (`ctbrowser/lib/Shell/bindings/element/`). So the harness's
   viewport cross-check passes — both engines say 1024 — while every `@x` and
   `@w` carries a 15px error. One of the two is wrong and they cannot both stay.
   Fold into **S7**, where the box model is already being touched.

2. **`tools/remote-build.sh` deleted `tools/.venv`.** The rsync is `--delete`
   with no protect filter for it, so every remote build destroyed the Playwright
   venv and the failure surfaced later as "playwright not installed". Fixed by
   adding the same exclude/protect pair `third_party/angle/` already had.
   `tools/check/compare.py` also still looked for `build/src/examples/ctdrive`,
   a path the 2026-08-09 reorg moved; it now tries both.

### The forms overhaul, and where a stated width stops

A user looked at the rendered page and said the controls were wrong. They were,
in four independent ways, and none of them was where the symptom pointed.

**A control's chrome was two constants, not CSS.** Layout reserved
`control_text_inset` around a field's text and the painter reserved the same
number a second time, and no stylesheet could reach either. So Bootstrap's
`.form-control` drew its text 6px in where it asks for 12, and stood 24px tall
where Chrome makes it 38. Both are gone: the UA sheet now says
`input, textarea, select { padding: 1px 5px; border: 1px solid #8f8f9d }`, which
reserves exactly what those constants did, and an author sheet can now change it.
Two exemptions came with it, because a blanket rule is wrong for the controls
that are GLYPHS rather than fields - a checkbox with a text field's padding is
25px wide, not 13.

**The painter drew frames the cascade had already drawn.** `replaced_painter`
took one rect and inferred everything else, so it filled a field's background
over the CSS background and outlined a border on top of the CSS border. It now
takes TWO - the border box and the content box - and `content_box_of()` in
`browser.hpp` is the single place a `node_id` becomes the second. The painter no
longer owns a control's background or its frame at all; it owns the tick, the
dot, the thumb and the caret, which is the part no stylesheet describes.

**A stated size on a replaced element was the CONTENT box.** Everywhere else in
this engine a stated width names the border box - `outer_width_of` has returned
it unchanged since it was written - and the replaced branch of `layout_box` was
the one place that disagreed, adding the padding and the border around it. With
`box-sizing: border-box` on `*`, which is the first thing Bootstrap's Reboot
does, that counted the 24px of padding and 2px of border twice: every
`.form-control` came out 346px wide against Chrome's 320. The fix is one
conditional - an INTRINSIC size is a content size and grows, a STATED one does
not - and it moved five controls onto Chrome's number exactly.

**An inline-level box's margins were not part of its line.** `inline_flow`
placed each child at the pen and advanced by its border box, so
`.form-label`'s `margin-bottom: .5rem` - the thing that separates every
Bootstrap label from its field - did nothing at all, and two inline-blocks side
by side touched. The line's extent is the MARGIN box now.

Together: `bootstrap-components.html` went from **48 screen cells different to
3**, which is the largest single move the cell metric has recorded, and its
property diff fell 489 to 444. Kitchen went 610 to 590.

Worth recording about the ORDER of that: the property diff barely moved on the
one that mattered, because a control reporting `@w=346` when Chrome says `320`
is one property on one element, and a label with no gap under it is zero
properties on none. The screenshot is what found both.

### Margin collapsing, and the rung the screen-cell metric valued far more

Block flow used to add `margin-bottom` and `margin-top` as two independent pieces
of space. That is not an approximation of CSS margin collapsing: it makes every
ordinary Bootstrap section too tall, and it cannot represent the chain through
an empty block at all. A collapsed group is now an associative `margin_strut`
holding its largest positive and most-negative members. Reducing a group to one
scalar early is wrong: `10, -20, 15` must retain both extrema and resolve to -5,
not collapse pairwise to -10 and then become +5.

Each independently arranged fragment returns its before/after struts and whether
its empty border box collapses through. The existing sequential block assembly
then merges those results while choosing sibling `y` positions. That distinction
keeps the parallel claim honest: local subtree layout remains independent, and
sequential and parallel use the SAME assembly rather than two implementations.
A definite-height split-point test caught the parallel driver discarding the
parent's percentage-height basis; it now carries the used content height down the
dominant path before workers run.

The edge rules are explicit. First/last-child margins can escape only through an
open parent edge; inline-blocks, flex items, table cells, absolute/fixed boxes,
roots and independent overflow formatting contexts stop them. Empty descendants
join both sides, while a real line box stops collapse-through. Non-zero
`min-height` has the narrow CSS exception rather than blocking every last-child
collapse, and block flow now applies both `min-height` and `max-height` to its used
border-box height. An absolutely positioned box's static marker uses the margin
position it WOULD have occupied in flow without consuming that pending group.
The effective-line fact is carried separately from geometry: `line-height: 0`
still makes a line box, while an empty `<span>` does not, and `<br>` must not be
mistaken for the latter. An indefinite percentage `max-height` is `none`, not a
zero clamp; the parallel and sequential paths share that used-height helper.

Overflow exposed a cascade prerequisite. Keeping `overflow`, `overflow-x` and
`overflow-y` as unrelated computed declarations loses source order, so the
shorthand now expands to its two axes in the cascade. Only hidden/scroll/auto
(and legacy overlay) create the formatting boundary; `clip` deliberately does
not. Paint reads the same final axes, so `overflow:hidden` did not regress when
the shorthand representation changed. A shorthand that becomes invalid only
after `var()` substitution actively unsets BOTH axes; merely removing a raw
`overflow` entry would incorrectly uncover an earlier longhand.

One ratchet initially rose for the right layout reason and the wrong API reason.
The dropdown divider's wrapper became a real zero-height fragment; the DOM
geometry walk used an empty rectangle as its "not found" sentinel, discarded its
real x/y/width, and invented zeros. The walk now carries `optional<rect>`, so a
zero-height element remains found and `getBoundingClientRect()` reports its real
position. A binding test pins that distinction.

Measured against fresh local Chromium at 1024x768:

```
                         properties     screen cells
bootstrap-type             127 -> 123       5 -> 5
bootstrap-components       444 -> 438       3 -> 3
bootstrap-position         113 -> 111       4 -> 4
bootstrap-kitchen          590 -> 587      55 -> 16
all six fixtures         1,458 -> 1,443    68 -> 29
```

Box and grid are byte-for-byte unchanged in the text baseline. Type, components,
position and kitchen moved as predicted; their diffs were read. The current
kitchen screenshots were stacked with ctbrowser above Chrome and opened: the
heading/divider/cards/table rhythm in the first viewport now tracks Chrome, and
the 39-cell fall is visible rather than a number accepted unseen.

Five image goldens moved for the same reason: `page`, `elements`, `widgets`,
`svg`, and the final eight rows of `angle/babylonorbit`. Old/new stacks were
opened before accepting them; every change is tighter adjoining block spacing,
with no missing content or paint movement. The complete 90-test CTest set passes
after giving `net_basics` its loopback socket and pointing `gl_basics` at the
bundled SwiftShader ICD, as `docs/platform.md` requires.

Two adjacent model limits remain recorded rather than smuggled into this claim.
The new height clamp is the ordinary block-flow path; flex CONTAINERS and
replaced elements still do not apply their own `min/max-height`. Also, overflow
propagation from `html`/`body` to the viewport and `display: flow-root` are not
represented, so those two formatting-context boundaries remain future work.

## The decisions this plan rests on

1. **The CSS front end gets rewritten inside ctbrowser** as `ctbrowser/lib/Style/css/` — a
   real CSS Syntax Level 3 tokenizer, full selector grammar, component-value
   model, at-rules with real conditions — and `third-party/compile-time-css` is
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

**Everywhere, gated automatically: `ctbrowser/unittests/unit/bootstrap_layout.cpp`** (S0's one
remaining piece). Builds each fixture through `shell::browser` headless with
`font8x8_metrics`, emits ctbrowser's own side of the same table from C++, and
byte-compares `ctbrowser/test/baseline/bootstrap-*.txt`. No Chrome, so it runs on the
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
| **S0** | **Harness.** `getComputedStyle`; `<link rel=stylesheet>` + a `style_error` channel; `css-dump.js`; `css-parity.py` + ratchet; six fixtures; `bootstrap_layout.cpp` + `ctbrowser/test/baseline/`; the `ctdrive` `reply()` fix; `compare.py`'s `request()` extraction; `box_of`/`find_id` → `ctbrowser/test/support/dom_probe.hpp` | **DONE.** 84/84 green, formatting clean, numbers recorded. Both halves verified able to FAIL: the ratchet exits 1 when a count rises, and `bootstrap_layout` exits 1 naming the element and property that moved |
| **S1** | **Tokenizer + component values + grammar**, feeding the existing cascade unchanged. ctcss out of `engine.hpp`, out of style's public interface, and out of the install | **DONE.** All 15 `style_basics` tests pass **verbatim**, the `bootstrap_layout` baselines and `ctbrowser/test/golden/page.ppm` are **byte-unchanged**, `check-package.sh` green. Retained compiled selectors 6,289 → **2,550**, dead ones 650 → **0**. `add_sheet` on 297 KB: **3.5 ms** against a 15 ms target. The remaining perf items - O(1) `put()`, values as views, packed specificity in `rule`, the ancestor-facts stack - are deferred to the rungs that need them, so this one stayed a pure substitution |
| **S2a** | Attribute selectors (all 6 operators + `i`/`s`), `:root`, packed (a,b,c) specificity | **DONE.** Bootstrap's matchable selectors 2,550 → **2,603 of 2,950 (86.4% → 88.2%)**, and the 128 global `--bs-*` are reachable at last. 85/85 |
| **S2b** | `+` and `~`, on an ancestor/sibling FACTS stack rather than re-deriving facts per candidate | **DONE.** Matchable 2,603 → **2,651 (89.9%)**; `facts_of` is no longer called during matching at all. 85/85 |
| **S2c** | `:not`/`:is`/`:where`, structural pseudos, `nth-child(An+B)`, and `:disabled`/`:checked`/`:link` as facts | **DONE.** Matchable 2,651 → **2,767 (93.8%)**, and the Chrome diff falls 7,820 → **7,703** - the first rung it has moved at all. 85/85 |
| **S3a** | **Real inheritance in the cascade**: computed_style splits into two independently interned halves, custom properties inherit, `inherit`/`initial`/`unset`/`revert` | **DONE. No golden moved** - that was the gate. `getComputedStyle`'s ancestor walk deleted. Sharing holds: 9 distinct inherited halves for 111 elements on the grid fixture, 4 for 40 on the box one |
| **S3b** | `style::value` and the property table with closed keyword sets; `em`/`rem`/`vh`/`vw`/`pt` folded to px at computed-value time; `layout/values.hpp` and `paint/values.hpp` lose their parsers; box_builder's remaining inheritance parameters go | Goldens must not move; the hardcoded 16px `rem` basis and `vh`-as-px both die |
| **S4** | **`var()` + `calc()` + units.** Custom-property cascade, substitution, IACVT, cycles, the two-pass order; `em`/`rem`/`vh`/`vw`/`pt` folding against a real root font-size; shorthand expansion moved to cascade time | All 1,370 `var()` resolve; the hardcoded 16 is gone; `test_shorthands_expand` passes verbatim |
| **S5** | **`@media` with a real environment** + resize re-evaluation; `@supports`, `@layer`, `@charset`, `@import` | The page at 375px and at 1400px differs the way Chrome's does; `dirty::styles` marked on resize **only** when a query flipped |
| **S6** | **The rest of the shorthand table**; **`::before`/`::after` + `content`**; **retire ctcss** | Shorthand coverage count; form-check marks and dropdown carets appear; `check-package.sh` green |
| **S7a** | `min/max-width` clamping and auto-margin centring | **DONE and TESTED, but LATENT** - it buys 6 differences, because `.container` gets `max-width: 1320px` from the flattened `@media` and 1320 does not clamp 1009. It cannot pay until S5 |
| **S5** | **`@media` with a real environment** — now the BOTTLENECK, see below | `.container` takes the breakpoint's max-width, which then clamps, which then centres |
| **S7b** | `box-sizing`, borders in `resolved_edges`, `min/max-height`, and the `clientWidth`/layout-width inconsistency | **DONE for Bootstrap's border-box path.** Block `min/max-height` landed with margin collapsing; general author `content-box` sizing remains a wider model change |
| **S8** | **Text metrics.** `line-height` (replacing the hardcoded `line_height_factor = 1.25f`), `text-align`, `vertical-align` | `bootstrap-type.html`; **moves every text golden** — taken early on purpose |
| **S9** | **Flex**, including the `run_parallel` independence guard | **DONE.** `bootstrap-grid.html` **490 → 173**, the whole Chrome diff 3,140 → **2,458**, and what is left on the grid fixture is not flex. 30 tests in a new `flex_basics.cpp`, two flex cases in parallel-equals-sequential, and six spec bugs found by an adversarial review that the 25 original tests could not see. **No image golden moved** |
| **S10** | **De-replace `<button>`** — out of `is_replaced_tag`, its intrinsic sizing moved into the UA sheet as real `padding`/`border` so the cascade can override it | `bootstrap-components.html`; **moves `widgets`, `elements`** |
| **CSS2 flow** | Associative vertical-margin groups; sibling, empty-through and parent/child collapse; BFC/flex/table/position boundaries; parallel-result merge | **DONE.** 1,458 → 1,443 properties and **68 → 29 cells**; kitchen alone 55 → 16 |
| **S11** | **Position and stacking.** S11a placement and S11b in-layer contexts are **DONE**; fixed/sticky compositor layers remain | `bootstrap-position.html`; `position_basics.cpp` + Appendix E cases in `paint_basics.cpp` |
| **S12** | **Paint decoration.** Per-side `border-{width,style,color}`, `border-radius`, `box-shadow`, `opacity`, `visibility`, `outline` | Screenshots **by eye**; `bootstrap.ppm`; new `raster_basics` coverage cases. **Moves `widgets`, `elements`, `page.ppm`** |
| **S13** | **`float`/`clear`/`overflow`**, then **`transform`/`transition`/animation** last — a transition makes the frame time-dependent, which no byte-compared golden can hold | Fixtures pin `transition: none` until this rung, with a comment saying why |

Four golden-moving events — **S8, S10, S12, and CSS2 flow** — and they are
separate commits.
Taking the line-height hit at S8 rather than after flex is why it sits there: if
it lands later, `widgets.ppm` moves once for reasons you cannot separate, and
"did flex break the heading spacing or did line-height?" stops being answerable
from the image. Canvas-drawing pixels were unaffected: a canvas is a replaced
element whose contents do not depend on line height or block margins. CSS2 flow
moved the `babylonorbit` page golden only in its final eight rows of page chrome,
not in the canvas scene.

**Predict first, regolden second.** The commit message names which goldens will
move *before* the run; an unpredicted golden moving stops the commit. That is the
only way an image golden catches a second, unintended change riding along with an
intended one. And the text baseline is the primary review artefact —
`git diff ctbrowser/test/baseline/` names the element and the property.

## Per rung

```bash
tools/check/css-parity.py --all ctbrowser/examples/pages/bootstrap-grid.html   # is it right?
tools/check/compare.py shot grid-after                              # does it LOOK right?
tools/check/css-parity.py --advance                                 # record it
(cd ctbrowser && REGOLDEN=1 ctest --preset default -R bootstrap_layout)  # if predicted
git diff ctbrowser/test/baseline/                                    # and READ it
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
(`ctbrowser/lib/Shell/browser/chrome.cpp`, `set_state`). Record per sheet whether any selector has a state
requirement on a *non-subject* compound; for Bootstrap that is false, so a hover
can re-resolve only that element and its descendants.

## The assertions that must change, and why they were wrong

- **`ctbrowser/unittests/unit/style_selectors.cpp` `test_unmatched_element_gets_empty_style`**
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
