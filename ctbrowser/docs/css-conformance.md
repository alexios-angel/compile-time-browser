# The CSS suites, measured

`ctbrowser/docs/wpt.md` is the instrument and the whole-corpus baseline. This
file is the **two CSS suites**, in more detail than that table has room for:
what the numbers are, what moved them, and — the part that matters most here —
**which subsystem each remaining failure actually belongs to.**

    tools/wpt/run-wpt.py --dir css/cssom     --jobs 4
    tools/wpt/run-wpt.py --dir css/css-values --jobs 4

Everything below was measured on the devbox against WPT `3f6b09ae`, four
workers, a 4 GB `ulimit -v` per driver, `CTBROWSER_GL_DRIVER=deterministic`.

## 1. The measurement was wrong before it was low

**94 of `css/css-values`' 128 harness errors were a missing file, not a
finding.** Almost every test in that suite is four lines long and calls
`test_valid_value`, `test_computed_value` or `test_math_used`, all of which live
in `css/support/*.js` — a directory `tools/wpt/fetch-wpt.sh` did not check out.
Those files could not have passed whatever the engine did, and no engine fix
could ever have moved them.

`css/support/` is now in the sparse list, and `--verify` names
`css/support/parsing-testcommon.js` by name so a checkout made before that line
existed is caught rather than quietly measured. It is a `SKIP_DIR_PARTS`
directory in `run-wpt.py`, so nothing in it is ever collected as a test: it is
imported, never run.

**This is a measurement fix and not engine progress.** Here is exactly what it
was worth, engine unchanged at `9ee803a`, measured **2026-09-03**:

| css/css-values | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| before — no `css/support/` (`docs/wpt.md`, 2026-09-02) | 13 | 128 | 2 | 0 | 128 | 237 | 508 |
| after — helpers fetched, **same engine** | 16 | 211 | 2 | 0 | 42 | 237 | 508 |

86 harness errors became real measurements: **+3 PASS and +83 FAIL**. The
suite's honest score went *down* in the sense that matters — 83 files that were
being counted as "the corpus is broken" are now counted as "the engine is
wrong", which is what they always were.

`css/cssom` does not use those helpers and reproduced the published baseline
exactly (8 / 148 / 15 / 0 / 21 / 29), which is the check that the instrument
itself did not move underneath the comparison.

## 2. Where the two suites stand — 2026-09-07, night

Measured on the devbox against WPT `3f6b09ae`, four workers, a 4 GB `ulimit -v`
per driver, `CTBROWSER_GL_DRIVER=deterministic`, engine at commit `f7e0912` on
`ctbrowser-wpt`.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `css/cssom` | **54** | 116 | 6 | 0 | 16 | 29 | 221 |
| `css/css-values` | **69** | 181 | 3 | 0 | 18 | 237 | 508 |

Subtests: `css/cssom` **1,052 PASS** / 593 FAIL / 1 NOTRUN / 8 TIMEOUT;
`css/css-values` **2,515 PASS** / 4,429 FAIL / 15 TIMEOUT.

Zero crashes in either suite, as in every run since the first.

| | 2026-09-03 | 09-07 day | 09-07 eve `a790941` | 09-07 eve `636f1b3` | 09-07 night `f7e0912` |
|---|---:|---:|---:|---:|---:|
| `css/cssom` files | 8 | 21 | 39 | 54 | **54** |
| `css/cssom` subtests | 113 | 220 | 346 | 1,050 | **1,052** |
| `css/css-values` files | 16 | 16 | 29 | 48 | **69** |
| `css/css-values` subtests | 549 | 695 | 2,038 | 2,332 | **2,515** |

`css/css-values` gained 21 files with no change aimed at it in this run: they
are the previous session's four subagent merges arriving, and §2b below is what
each of them was.

### THE TWO BLOCKS IN FRONT OF BOTH SUITES, and neither is a CSS bug

Both were found by reading the failure log rather than by writing CSS, both are
one line, and between them they hold more subtests than everything measured
above.

**1. `el.style` is a WRITABLE data property.** CSSOM declares it
`[PutForwards=cssText] readonly attribute CSSStyleDeclaration style`, so
`el.style = ""` must forward to `el.style.cssText = ""`. In this engine
`lib/Shell/bindings/element/views.cpp` installs it with `obj.set(...)`, so that
assignment REPLACES the declaration object with the string. Every later
`el.style[prop] = v` writes to a primitive and vanishes, and `getComputedStyle`
reports the initial value forever.

`css/support/numeric-testcommon.js` — `test_math_used`, `test_math_computed`,
`test_math_specified` — opens every case with exactly that assignment.
**Nineteen `css/css-values` files fail 100% because of it, 1,339 failing
subtests:** `round-mod-rem-computed` (243), `signs-abs-computed` (233),
`round-function` (191), `signed-zero` (162), `minmax-length-computed` (80),
`hypot-pow-sqrt-computed` (53), `calc-mix-computed` (53),
`acos-asin-atan-atan2-computed` (52), `minmax-length-percent-computed` (50),
`typed_arithmetic` (39), `progress-computed` (36), `minmax-angle-computed` (32),
`sin-cos-tan-computed` (32), `minmax-time-computed` (24), `exp-log-compute`
(21), `minmax-number-computed` (14), `minmax-percentage-computed` (14),
`minmax-integer-computed` (10), and `sin-cos-tan-serialize`. The control is
clean: `computed-testcommon.js` never writes `el.style = …` and its 15 files are
an ordinary mix of pass and fail.

`lib/Shell/bindings/stylesheets/prototypes.cpp` already installs `rule.style` as an
accessor with a forwarding setter and is the template. **How many of the 1,339
then PASS is not measured** — behind the block is the math serialization, which
is a separate question.

**2. `set_author_styles_hook` is never installed.** `browser/styles.cpp`'s
`refresh_author_styles` rebuilds the cascade from the DOM's text rather than
from `dom_bindings::author_style_text()`, because CSSOM's selector
serialization is lossy — and so no `insertRule`, no `selectorText =`, no
`replaceSync` and no `adoptedStyleSheets` reordering ever reaches the cascade.
That is what "expected `rgb(255, 0, 0)`, got `rgb(0, 0, 0)`" means in
`adoptedstylesheets-cascade-order` (10 subtests),
`CSSStyleSheet-constructable-invalidation`, `-replace-cssRules`, `-cssRules`,
`-duplicate`, `adoptedstylesheets-modify-array-and-sheet` (3),
`selectorText-modification-restyle-002`, and 19 of
`CSSStyleRule-set-selectorText`'s 43 — roughly 40 subtests over eight files,
gated on one wire.

What makes the serialization lossy is in `lib/Style/css/selector.cpp`:
`representable()` falls back to the author's bytes when a compound sets
`never_matches`, but a pseudo-class the compiler silently DROPS sets nothing, so
it serializes as `*`. Fixing that in the compiler unblocks the wire.

## 2a. Where the two suites stood — 2026-09-07, evening

Measured on the devbox against WPT `3f6b09ae`, four workers, engine at commit
`636f1b3` on `ctbrowser-wpt`. §6 is the 2026-09-07 daytime re-measurement and
§2b is the 2026-09-03 baseline; all four are kept because the comparison is the
instrument.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `css/cssom` | **54** | 115 | 6 | 0 | 17 | 29 | 221 |
| `css/css-values` | **48** | 199 | 3 | 0 | 21 | 237 | 508 |

Subtests: `css/cssom` **1,050 PASS** / 591 FAIL / 1 NOTRUN / 8 TIMEOUT;
`css/css-values` **2,332 PASS** / 4,526 FAIL / 15 TIMEOUT.

Zero crashes in either suite, as in every run since the first.

| | 2026-09-03 | 2026-09-07 day | 2026-09-07 eve, `a790941` | 2026-09-07 eve, `636f1b3` |
|---|---:|---:|---:|---:|
| `css/cssom` files | 8 | 21 | 39 | **54** |
| `css/cssom` subtests | 113 | 220 | 346 | **1,050** |
| `css/css-values` files | 16 | 16 | 29 | **48** |
| `css/css-values` subtests | 549 | 695 | 2,038 | **2,332** |

**`css/cssom`'s passing subtests tripled in one step**, 346 to 1,050, and the
single change behind most of it is not a CSS one: `el.style`'s declaration store
was seeded once at wrapper construction, so `setAttribute("style", …)` — which
writes the attribute directly and never touches the proxy — left every read
answering with what the element had when it was wrapped.
`cssom/serialize-values.html` is 697 subtests of exactly that shape.

### What moved `css/cssom`, 39 -> 54 files

* **`el.style` follows the attribute**, above, and a supported property that is
  not set reads `""` rather than `undefined` — CSSOM 6.7.2's generated getters.
* **A pseudo-element argument is not the element.** `getComputedStyle(el, "::x")`
  took the second argument and threw it away. CSSOM splits it three ways and only
  the first is "ignore it"; a colon-prefixed argument this engine does not style
  reports an EMPTY declaration, which is 17 of `getComputedStyle-pseudo-with-
  argument.html`'s 22 subtests and 6 of `-picker.html`'s 9. It costs one:
  `::picker(select)` on a non-select expected `rgba(0, 0, 0, 0)` and now gets
  `""`, which is the honest answer from an engine with no `::picker`.
* **The automatic minimum size**, `min-width`/`min-height: auto`. Three things
  preserve it and only one was implemented: a flex item did, a GRID item did not
  (there is no grid box kind — `display: grid` lays out as a block, so the fact
  is a declaration on the PARENT), and a specified `aspect-ratio` did not.
  The rule also keyed on the text being empty, so it applied to the initial value
  and not to an `auto` the author wrote — and that file asserts each element
  twice, once each way.
* **A computed `font-family` keeps its case.** `collapse_keyword` ASCII-lowercases,
  which is right for `display: BLOCK` and wrong for every family name ever
  written: `Twisty Tie` came back `twisty tie` on every element of every page
  that names a font. That is a real difference in the css-parity dump, not only
  in WPT.
* **The CSSOM object model** — constructable sheets, `insertRule` on a grouping
  rule, a MediaList that is a view of its text rather than a stored list, and an
  adopted sheet reaching the author CSS in the order it was adopted in.

### What moved `css/css-values`, 29 -> 48 files

All of it is the value grammar and the serialization, and every row was verified
against a scratch oracle replaying the suite's own assertions before it landed:

* **A percentage with no calculation context is a syntax error**, not a value:
  a percentage in an expression that answers with an angle, time, frequency or
  resolution. All 12 of `percentage-without-context`.
* **`rotate()`/`skew()`/`hue-rotate()` take an angle**, so a math function in one
  of those positions that resolves to another type is invalid —
  `rotate(min(0px))`, `rotate(tan(45deg))`. It was the LAST failing subtest in
  three whole files.
* **Canonical sum ordering**, CSS Values §10.13: percentage first, then the units
  ASCII-sorted. `calc(10px + 1vmin + 10%)` serializes as
  `calc(10% + 10px + 1vmin)`, which needed a second, symbolic evaluation basis
  where `1em` and `1cqw` stay terms of their own.
* **The property's half of the calculation context**: `border-left-width:
  min(1px, 0%)` is invalid where `text-indent: min(1px, 0%)` is not.
* **`progress()`, `ident()`, `inherit()`, `random-item()`** as real functions
  with real argument grammars — three files had been passing VACUOUSLY, because
  an unknown function failed the property grammar for the wrong reason.
* **An unresolved comparison's arguments still simplify**:
  `min(10% + 30px, 5em + 5%)` -> `min(10% + 30px, 5% + 5em)`.

### What is still in front of `css/css-values`

`attr()` is 243 subtests over two files and lives in
`lib/Style/css/substitute.cpp`. `calc-size()` (52), `calc-mix()` (69),
`random()` (62), `position()` (21) and the URL request modifiers (35) are
unimplemented CSS Values 5 features and deliberate gaps. `calc-in-color-001`
needs a `<color>` value model: `color` is `freeform` today and stored verbatim,
so `rgba(calc(0%) calc(100%) calc(0%) / calc(10% * 10))` cannot compute to
`rgb(0, 255, 0)`. The four-corner `/` shorthand serialization for
`border-radius` and the two-component `background-position` are in
`lib/Shell/bindings/computed_style.cpp`, not in the value code.

## 2b. The baseline, 2026-09-03 — and where it stood then

§6 is the 2026-09-07 re-measurement. In short: `css/cssom` 8 -> **21** files and
113 -> **220** passing subtests; `css/css-values` stays at **16** files while its
passing subtests go 549 -> **695**, and the six files it lost and the six it
gained are named and diagnosed there.

### The 2026-09-03 baseline

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `css/cssom` | 8 | 148 | 15 | 0 | 21 | 29 | 221 |
| `css/css-values` | 16 | 211 | 2 | 0 | 42 | 237 | 508 |

Subtests: `css/cssom` 113 PASS / 1,444 FAIL / 21 NOTRUN / 5 TIMEOUT;
`css/css-values` 549 PASS / 4,715 FAIL / 2 NOTRUN / 12 TIMEOUT.

Zero crashes in either suite.

## 3. The skip audit

**Every skip in both suites was checked against the file on disk, and none of
them hides a failure.** The runner reads only the first 8 KB of a test to decide
what it is, so "not a testharness test" could in principle be wrong about a file
that loads the harness late; the audit greps the **whole** file instead.

| suite | skips | `not a testharness test` | `reftest` | wrong |
|---|---:|---:|---:|---:|
| `css/cssom` | 29 | 17 | 12 | 0 |
| `css/css-values` | 237 | 24 | 213 | 0 |

All 41 "not a testharness test" files genuinely never mention `testharness.js`,
and all 225 reftests genuinely carry `rel=match` or `rel=mismatch`.

**But 39 of those 41 are crashtests**, and that is a gap rather than a lie.
WPT's crashtest convention is that a file named `*-crash.html`, or one under
`crashtests/`, passes if the browser loads it without dying — there is no
harness because there is nothing to assert. `run-wpt.py` already tells a crash
from a clean run (it is one process per test and it names the signal), so these
39 are the one category of skip here that the instrument could convert into a
measurement rather than a shrug. The remaining two are genuinely not tests:
`inline-cache-base-uri/inner.html` and
`vh-update-and-transition-in-subframe-iframe.html` are fixtures another test
loads.

## 4. Where the failures actually live

This is the finding worth more than any individual fix, and it is measured
rather than argued. Splitting the failing subtests by whether the engine
produced a **wrong value** or **no value at all**:

| `css/css-values` failing subtests | count |
|---|---:|
| a missing JS/DOM name, or a property that is `undefined` | 796 |
| a real, wrong value | 3,933 |

and the ranked causes over both suites are dominated by two Shell-side gaps:

| count | cause | where it lives |
|---:|---|---|
| 1,034 (`cssom`) | `getComputedStyle(el).someProperty` is `undefined` | `lib/Shell/bindings/computed_style.cpp` |
| ~1,000 (`css-values`) | `e.style[prop] = v` then read back is the raw text | `lib/Shell/bindings/element/views.cpp` |

Two specific things gate most of both suites:

- **`getComputedStyle` exposes CSS names only, never the IDL ones.** The object
  holds `background-color`; every test in the corpus asks for
  `.backgroundColor`, `.zIndex`, `.marginLeft`, `.fontSize`. `calc-in-color-001`,
  `calc-rgb-percent-001`, `calc-integer` and dozens more compute the right
  answer inside the engine and then fail on the spelling of the read.
- **`el.style` is a string store with no CSS in it.** `setProperty` records
  whatever it is given and `getPropertyValue` hands it back unchanged, so a
  value is never validated and never re-serialised. That is the whole of
  `test_invalid_value` (`expected "" but got "round()"`, ~600 subtests) and the
  whole of `test_valid_value`'s canonical-serialisation half.
- A third, smaller one: `CSS.supports()` does not exist, and
  `computed-testcommon.js` asserts it before every single computed-value test.

None of those three is in the CSS engine. The style front end computes an
answer that the CSSOM layer then cannot be asked for.

## 5. What was fixed in the CSS engine, and what it was worth

Two real defects in `lib/Style/css/calc/` (one file, `calc.cpp`, at the time), both found by reading these
failures rather than by guessing. Both are proved by
`unittests/unit/style_calc.cpp`, and neither moved a render golden.

### `calc()` may resolve to a `<number>` — CSS Values 3 §8.1

The evaluator answered "no value" for any expression that came out a number, the
fold read that as "invalid", and the cascade **deleted the declaration**. So
`opacity: calc(2 / 4)`, `z-index: calc(1 + 1)`, `tab-size: calc(2 * 3)`,
`font-feature-settings: "vert" calc(1 + 1)` and `rgb(calc(0), calc(255 + 0),
calc(140 - 139 - 1))` each produced nothing at all.

The reason it was written that way is real and is kept: a number is **not** a
length, and `width: calc(2 * 3)` must stay invalid. What was missing was
somewhere to ask which of the two the property wanted — `math_context_of`, a
short table of the properties whose entire value is lengths. Everything else
accepts a number, because guessing "length" for an unknown property would
silently reject values that are fine.

### `min()`, `max()` and `clamp()` — CSS Values 4 §10.3

Absent entirely. `width: clamp(1rem, 2vw, 3rem)` reached layout as text,
`parse_length` gave up on the leading `c`, and the box got a zero. Bootstrap
uses none of the three, which is why a corpus of one never noticed.

The addition has a third outcome besides "folded" and "invalid", and it is what
makes it safe: `min(10px, 5%)` is 10px on a wide containing block and 5% of it
on a narrow one, so there is no answer at computed-value time and §10.11 says
the computed value is the function as written. That is `unresolved` — the text
survives and the declaration lives. **A comparison function never condemns a
declaration**, because before this file could parse the three at all they were
kept verbatim, so keeping them is the one answer that cannot regress a page.

### It works, and WPT cannot see it

Driven through `ctdrive` against a page that sets these from a **stylesheet**,
`getComputedStyle` now answers:

    styled: opacity=[0.5] z-index=[2] tab-size=[6] color=[rgb(0, 255, 0)]
    clamp(1rem, 2vw, 3rem) => 20.484375px   max(10px, 4px) => 10px
    min(3rem, 2rem) => 32px                 width: calc(2 * 3) => still invalid

Every one of those was empty before. And the suites did not move by a single
subtest, before or after, measured 2026-09-03:

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP |
|---|---:|---:|---:|---:|---:|---:|
| `css/cssom` before / after | 8 / 8 | 148 / 148 | 15 / 15 | 0 / 0 | 21 / 21 | 29 / 29 |
| `css/css-values` before / after | 16 / 16 | 211 / 211 | 2 / 2 | 0 / 0 | 42 / 42 | 237 / 237 |

Per **test** and per **subtest** the two runs are byte-identical — no
regressions, and no gains. The reason is §4, and one more thing the same probe
shows, which is worth stating precisely because it is the single cheapest fix
left in either suite:

    before: keys=11 opacity=[undefined]
    attr=[opacity: calc(2 / 4); ]          <- el.style.setProperty landed
    after : keys=11 opacity=[undefined]    <- and getComputedStyle cannot see it

**`getComputedStyle` does not flush a pending style recalculation.** The object
it returns is built from the style map resolved at load, so a script that writes
`el.style` and reads the computed value back in the same turn — which is what
*every* test in `css/css-values` does — reads the state before its own write.
That, and the camelCase spelling above it, are why a correct answer inside the
style engine is invisible to this corpus.


### Proved load-bearing, three ways

Each fix was reverted on its own and the suite watched go red. The revert was
confirmed to have reached the build machine before the run — rsync preserves
mtimes, so an edit that never landed looks exactly like a guard that fired.

| reverted | assertions that went red |
|---|---|
| `run()` refuses a number answer again | 16, incl. `opacity`/`z-index`/`tab-size`/`color` at cascade level |
| `min`/`max`/`clamp` removed from the evaluator and from the name scan | 19, incl. `clamp(1rem, 2vw, 3rem)` at cascade level |
| `math_context_of` always says `any`, `wrong_kind` always false | 11, incl. two assertions that predate this work: `!fold_calc("calc(-1 * 0)").ok` and the Bootstrap `.row` case, which starts reporting `0` instead of being invalid |

The third is the one worth reading twice: without the property table, the
number-answer change would have re-introduced the exact 24-element regression
the original "a number is not a length" comment was written about.

### The CSSOM wall, priced

`getComputedStyle` exposing the IDL spellings looked like the cheapest unlock —
1,034 `cssom` subtests fail with `undefined`. It was measured with a throwaway
patch to `lib/Shell/bindings/computed_style.cpp` (**not committed, reverted
immediately**) that publishes each property under its camelCase name as well:

    camel marginTop=[32px]  css margin-top=[32px]      <- the patch works
    css/cssom        8 / 148 / 15 / 0 / 21 / 29        <- and moved NOTHING
    css/css-values  16 / 211 /  2 / 0 / 42 / 237       <- not one subtest

So the spelling is necessary and **not sufficient**, and the real blocker is the
one below it: `getComputedStyle` builds its object from the style map resolved
at load and there is no way to flush a pending restyle, so a property an element
only acquires through `el.style` is not on the object at all — whatever it is
called. Anyone taking that on should budget for the flush first and treat the
IDL names as the second half of the same change.

## 6. The wall came down — 2026-09-07

**`css/cssom` 12 -> 21 files and 113 -> 220 passing subtests; `css/css-values`
16 -> 16 files and 549 -> 695 passing subtests.** §5's last section priced the
camelCase spelling at nothing and said the flush had to come first. It was right,
and the flush turned out to be TWO defects rather than one.

### A one-word invalidation bug

`dom_bindings`' mutation hook marked `dirty::paint`, which is BELOW
`dirty::styles` in the ordering, so `frame()`'s
`if (dirty_ >= dirty::styles) resolve_styles()` never fired for a script
mutation. A page that wrote `el.style`, set an attribute or added a class got its
display list re-recorded from the cascade resolved at LOAD. Every caller of
`mutated()` is script changing the document and every one of them can change
which rules match. The corpora never noticed because p5 and Phaser invalidate
through `canvases_.total_revision()` instead.

### And the reason that was invisible

`load_one_page` does `mark(dirty::everything); run_scripts();` — **scripts run
before the first layout ever happens** — and `observe_styles`, `observe_boxes`
and `observe_layout` are called only from `run_layout()`. So at page-load script
time, which is when every WPT test runs, all three of the pointers
`getComputedStyle` reads were NULL. It was not answering the wrong value; it had
nothing to read. That is why the throwaway camelCase patch in §5 moved nothing,
and it is why the patch was the right experiment and the wrong conclusion.

`getComputedStyle` is now wrapped to flush exactly the stages `dirty_` says are
stale before it answers.

### The object

125 longhands from `style::css::known_properties()`, each published under the
hyphenated CSS name AND the IDL one; the 23 shorthands present for `in` and for
`getPropertyValue` but absent from the indexed properties, which is what
`getComputedStyle-getter-v-properties` asserts both halves of; `length`,
`item(i)` and indexed access enumerating lexicographically, which
`getComputedStyle-property-order.html` requires outright; and a property nothing
declared answering its initial value instead of `undefined`.

Four correctness fixes came out of reading the corpus rather than writing the
object: `line-height: normal` reports `normal` instead of the box's px;
`border-*-width` and `outline-width` report `0px` when the style is `none` and
resolve thin/medium/thick to 1/3/5px; `currentcolor` resolves to the element's
computed `color`; and a detached element returns an empty declaration.

### `el.style` is a CSSStyleDeclaration

`style/css/properties.hpp` is the property table this file's §4 said did not
exist: 143 properties, each with a value kind, a keyword set, its CSS initial
value and whether it inherits. `el.style` validates through it and stores the
canonical form, so `test_invalid_value` can be answered at all; `CSS.supports`
and `CSS.escape` exist; and `getPropertyPriority`, `item`, `length`, indexed
access and `cssText` are real.

**The table is conservative and that is the design.** A property not in it is
accepted verbatim exactly as before; a property in it as `freeform` is known to
EXIST — which is what `CSS.supports(name)` and `name in getComputedStyle(e)` ask
— but its values are still accepted verbatim; only a property with a real value
kind can refuse anything. Every shorthand is freeform, because refusing
`margin: 10px 20px` needs the expansion.

### It cost six files, and every one of them was passing by not testing

`css/css-values` gained six files and lost six, ending where it started at 16
while its passing subtests went 549 -> 695. The six it lost are named one by one
in `docs/wpt.md`; the shape of all six is the same, and it is the shape §1 of
this file already described for `css/support/`. Four guard their assertions on
`CSS.supports`, which did not exist; one relied on `el.style` storing any value
it was given; one had `getComputedStyle` answering `undefined` and never reaching
its comparison. Each now RUNS, and fails on the feature it is actually about —
`attr()`, `random-item()`, the `lh` unit resolving to a line box's height.

One defect of this work's own was in the first measurement and is fixed: the
value grammar was re-serialising a value whose syntax it does not model, so
`random-item(auto ,serif)` came back with the spacing changed and
`test_valid_value` asserts the round-trip exactly. The author's bytes are kept
now for anything the table does not model, and `CSS.supports` refuses a value
calling a function this engine cannot evaluate.

### The Bootstrap baseline moved and was read

`test/baseline/bootstrap-*.txt` grew by 631 lines and **not one number moved**:
the geometry is byte-identical and what is new is the properties that used to be
absent. `docs/wpt.md`'s commit for it lists the three answers in that diff worth
reading twice.

### What is still not done here

* **The object is a SNAPSHOT, not live.** CSSOM says `getComputedStyle` returns a
  live `CSSStyleDeclaration`. `test_computed_value` re-calls it every time, so
  this does not block the bulk; a test that holds `let cs = gcs(el)` across a
  write still reads stale.
* **`number_text`'s 1/64 quantum blocks two whole files.**
  `getComputedStyle-margins-roundtrip.html` and
  `getComputedStyle-insets-absolute-roundtrip.html`, 8 subtests: they set
  `20.7px` and expect it back, and we answer `20.703125px`. Chrome quantises USED
  values and not a margin's computed value, so the honest fix is to skip the snap
  for the properties whose computed value IS the specified length and keep it for
  the fragment-derived `width`/`height`.
* **Assignment to a computed style does not throw**, and **pseudo-elements are
  still ignored** (~37 subtests) because nothing generates those boxes.
* **`getBoundingClientRect`, `offsetWidth` and `clientHeight` do not flush.**
  They have exactly the same staleness `getComputedStyle` had, in
  `lib/Shell/bindings/element/views.cpp`. One shared flush hook on `dom_bindings` is
  the shape, and the `getComputedStyle` wrapper is deliberately written so it can
  be deleted when that exists.

## 7. Re-running this

```bash
tools/wpt/fetch-wpt.sh                       # once; now includes css/support/
tools/wpt/fetch-wpt.sh --verify              # checks the helper by name
tools/wpt/run-wpt.py --selftest              # the instrument, first
tools/wpt/run-wpt.py --dir css/cssom      --jobs 4 --json /tmp/<yours>-cssom.json
tools/wpt/run-wpt.py --dir css/css-values --jobs 4 --json /tmp/<yours>-values.json
```

`/tmp` on the devbox is shared by several working copies at once. Name the JSON
after the checkout that wrote it, and check the `directories` field of the file
you read before quoting a number out of it.
