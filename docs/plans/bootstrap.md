# Bootstrap 5.3.8 at Chrome parity

**Where it is. The Chrome diff over the six fixtures is 2,419, down from 7,820 when
this started; 88/88 green; the numbers are in `tools/check/css-parity.txt` and
`bootstrap_layout` pins the same table without a browser.**

**Done:** S0 the harness · S1 the CSS front end (`style/css/` is a real Syntax Level 3
tokenizer and grammar; no public header includes `<ctcss.hpp>`) · S2 the selector engine
(**93.8% of Bootstrap's selectors can match**, up from 86.4%) · S3a **inheritance as a
real cascade stage** · S4a **`var()`** - all 1,370 of Bootstrap's calls, and the page
started looking like Bootstrap · S4b the two-pass cascade, so the `border` shorthand
expands and borders draw · `calc()`, real `@media` evaluation, `line-height`, S7a
min/max-width and auto-margin centring · **S9 flex**, which took the grid fixture from
490 differences to 173 and is where most of the remaining geometry was.

· **S10** inline-block shrink-to-fit and `<button>` out of `is_replaced_tag` · the
border half of **S7b** · `border-radius` from **S12**.

· **S11a** positioning as a pass over the fragment tree.

**Next:** S11b (`z-index` and CSS 2.1 Appendix E paint order; `fixed` as its own
non-scrolling layer), the rest of S12 (`box-shadow`, `opacity`, per-side borders),
`text-align`, which no formatting context reads yet, and Chrome's `LayoutUnit`
quantisation - 102 of the grid fixture's 137 are one 1/64px rounding difference
propagating, and the same cluster appears on three other fixtures.

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

### What S1 measured

`style/css/` is ~1,000 lines: a §4 tokenizer, a §5 grammar over component values,
and a selector parser that writes `compiled_selector` directly - so
`engine::compile_selector` is gone, and with it the bug where a selector was
compiled once per DECLARATION and pushed before being checked.

| | |
|---|---|
| Bootstrap: rules / selectors / declarations | 2,539 / 2,950 / 5,524 |
| tokens / component values | 68,584 / 53,097, both dropped after parsing |
| compiled selectors RETAINED | **2,550** - exactly the 2,950 parsed less the 400 that cannot match. Was ~6,289 with ~650 permanently dead |
| `add_sheet` (parse + compile + index) | **3.5 ms**; the parse alone is 2.4 ms, about 124 MB/s |

The selector subset was deliberately NOT widened, which is what makes those
numbers trustworthy: the resolved styles are byte-identical to the old front end's
on all six fixtures, so the only thing that changed is how they were arrived at.
Two spec details corrected a wrong assumption along the way, both recorded in
`tests/unit/css_syntax.cpp`: a `<hash-token>`'s id flag does not distinguish a
colour from a selector (`#fff` is id-like, and `#fff {}` really does select
`id="fff"`), and a `<percentage-token>` carries no type flag at all.

The one real bug the gate caught was worth the gate: a component value's extent
was being inferred from its last CHILD, so `var(--x)` came back as `var(--x` and
every `rgba(...)` lost its `)` - the colour then failed to parse and the element
was painted with nothing. `component_value` records `[token, end_token)` now.

### S2a, and a finding about the harness itself

Attribute selectors and `:root` landed, and **the Chrome parity numbers did not move
by one**: 343/564/1596/2435/724/2158 differ, byte-identical before and after.

That is not a failure, it is the plan's own warning arriving — *"the front end will
outrun layout; score the front end separately or the win is invisible"*. The
compared set is 48 properties layout and paint consume. Everything the newly
matching rules deliver is either a custom property, which nothing reads until S4,
or a property not in the set (`border-radius` on `.form-check-input[type=checkbox]`,
for instance). The parity harness measures the END of the pipeline and cannot see a
correct front-end rung at all.

So the front end now has its own score, in `tests/unit/css_syntax.cpp`: how many of
Bootstrap's 2,950 selectors can match, with a FLOOR that may only be raised. A rung
that widens the grammar and does not move that number has not done what it claimed.

|  | matchable | unmatchable |
|---|---|---|
| after S1 (tag/id/class, descendant/child) | 2,550 (86.4%) | 400 |
| after S2a (+ attributes, `:root`) | 2,603 (88.2%) | 347 |
| after S2b (+ `+` and `~`) | 2,651 (89.9%) | 299 |
| after S2c (+ functional and structural pseudos) | **2,767 (93.8%)** | 183 |

The remaining 183 are pseudo-ELEMENTS (123 of them) and the form-state pseudos that
need shell wiring - `:focus-visible`, `:focus-within`, `:placeholder-shown`,
`:valid`, `:invalid`, `:indeterminate`, `:required`, `:read-only`. Neither is a
selector-grammar gap: a pseudo-element cannot match an element by definition, and it
is box generation (S6) that gives `::before` something to match. ~94% is where the
grammar tops out, not ~100%, and the plan was wrong to say otherwise.

**And the census cannot see correctness.** A selector counts as matchable if it
parses into something the matcher will consider. `:disabled` counted from the very
first rung - as a state bit that NOTHING EVER SET. It parsed, it was counted, and it
never matched. That was invisible while `:not()` was unsupported; implementing
`:not()` turned it into a wrong render, because `.btn:not(:disabled)` then matched
disabled buttons too, and Bootstrap writes exactly that eight times. So `:disabled`,
`:checked` and `:link` became facts about the element in the same rung, and the
number did not move by one. Countable is not correct; the tests are where correct
lives.

`:root` is worth more than its 5 selectors suggest: it and
`[data-bs-theme=light]` are the two alternatives on the rule carrying every global
`--bs-*`, and both were dead. `getComputedStyle(document.documentElement)` now
answers `--bs-blue: #0d6efd`, and `body` inherits it — which is the input S4's
`var()` substitution needs to exist at all.

Of Bootstrap's 93 attribute selectors, 48 are now in selectors that can match; the
other 45 sit in selectors still dead for a second reason (`+`, `~`, `:not()`), which
is what S2b and S2c clear.

### S2b, and what a sibling combinator costs

`+` and `~` cannot be answered from the tree: there is no previous-sibling link to
walk back along. So the traversal keeps what it has already seen - `levels_[d]` is
every element visited so far at depth d, `path_[d]` says which of them the current
chain runs through - and the matcher's cursor becomes a `(depth, index)` pair rather
than a node, because a sibling combinator moves SIDEWAYS.

That same structure removes the thing the plan named as probably the hottest single
cost in the engine: `matches` used to call `facts_of` for every ancestor of every
candidate rule, and `facts_of` interns the element's id and each of its classes,
each intern taking a `shared_mutex`. For an element twelve deep with four classes
against forty candidates that is up to 2,400 lock acquisitions to re-answer what the
DFS already knew on its way down. **Matching now asks the tree nothing.**

Three details that a node-walking implementation gets wrong, each with a test: a
TEXT node between two elements is not a sibling (`+` is about elements); siblings do
not cross a parent boundary; and the first element of a level has nothing before it,
so the cursor must fail rather than read off the front of the list. A fourth is
about reuse rather than CSS - `levels_` is kept for its capacity across
`resolve_all` calls, so its CONTENTS have to be cleared, or a second document finds
the first one's `<html>` sitting before its own and `html ~ x` matches across two
documents.

The measured effect on the render is honest and small: five values across two
fixtures now arrive that did not before - `.breadcrumb-item+.breadcrumb-item`'s
`padding-left`, `.card-link+.card-link`'s `margin-left`,
`.list-group-item+.list-group-item.active`'s `margin-top` - so `substituted` falls
by 5 and `differ` does not move at all, because every one of them arrives as `auto`:
their values are `var()` and `calc()`, which is S4's business. S2b fixed the
MATCHING. No geometry moved.

### S2c, and the rung where the Chrome gate finally moved

The functional pseudos needed the matcher to become RE-ENTRANT: `:not(.wrap > p)` has
the same subject as the compound it sits in, so its argument runs the whole walk from
that same cursor. Once the cursor was a `(depth, index)` pair rather than a node -
which S2b needed anyway for `+` - that was a wrapper around the existing walk rather
than a second matcher.

`:has()` is deliberately absent and stays unmatchable. It looks FORWARD at
descendants, and the traversal that answers everything else here has not visited them
yet, so it would need a second pass over the subtree rather than a lookup. Bootstrap
uses none. An argument the engine cannot represent also makes the whole pseudo
unmatchable rather than vacuously true - the direction matters, because a dead branch
inside `:not()` would otherwise read as "matches nothing, therefore `:not` passes".

The measured effect on the render is the first one the parity harness could see:

| | differ | substituted |
|---|---|---|
| after S2b | 7,820 | 19,876 |
| after S2c | **7,703** | **19,848** |

Most of it is one rule. `.collapse:not(.show) { display: none }` now matches, so the
closed accordion section stops generating a box at all - `display: block` becomes
`none` and the element loses its width, height and font-size because it has no box to
read them from. `examples/pages/bootstrap-components.html` carries the comment
"Hidden, so it must generate no box" on that element, written before the engine could
do it.

### S3a: inheritance, and what the split cost

Before this, `resolve` produced only the declarations that MATCHED, and inheritance
happened five separate ad-hoc ways downstream - `font-size`, the face, text decoration
and white-space threaded as parameters through `box_builder`, `color` threaded through
the recorder, and a fifth walk in `getComputedStyle`. Five mechanisms for one idea,
none of them reachable from the cascade, and no way for a custom property to travel at
all.

`computed_style` now holds an OWN declaration list and a pointer to an interned
INHERITED half, and `get` checks own then inherited - which is the cascade in one line.
The split is what keeps the interning invariant alive: a single combined list would
make an element's style depend on its parent's, so two `<li>` in different lists would
stop sharing and the rate would collapse to (inheritance contexts x own halves).

**The load-bearing shortcut:** an element that declares nothing inherited keeps its
parent's pointer VERBATIM - no copy, no hash, no intern. That is the difference between
this being an optimisation and a regression, because Bootstrap's `:root` carries 128
custom properties and a 2,500-element page would otherwise hold 2,500 copies of them.

Measured, per fixture:

| fixture | elements | distinct inherited halves | largest half | resolve_all |
|---|---|---|---|---|
| box | 40 | **4** | 134 | 5.4 µs/el |
| grid | 111 | **9** | 134 | 4.8 µs/el |
| position | 64 | 16 | 183 | 7.3 µs/el |
| type | 62 | 26 | 136 | 5.9 µs/el |
| kitchen | 165 | 69 | 164 | 7.0 µs/el |
| components | 189 | 88 | 171 | 8.1 µs/el |

The sharing works: 9 halves for 111 elements on the repetitive page, 4 for 40 on the
box one. **Two targets are NOT met and should be read carefully.** "Distinct styles
under 15% of elements" runs at 37-90% here - but these fixtures were built so that
every element isolates a different concern, which is the least shareable document
possible; the target was written about a large repetitive page and cannot be judged on
them. "Under 4 µs/element" runs at 4.8-8.1, genuinely over, on pages small enough that
per-element fixed costs dominate - and the three perf items the plan lists for this
rung (O(1) `put()`, values as views, packed specificity in `rule`) are still deferred.

`inherit` reads the parent's WHOLE style rather than its inherited half, which is what
makes `display: inherit` work at all - the keyword takes the parent's value for any
property, inherited or not. `initial` is an EMPTY own value that shadows the inherited
one, since every consumer already treats empty as "nothing said". `unset` drops the
declaration, which is correct for both kinds. `revert` is treated as `unset`: doing it
properly needs the value the previous ORIGIN would have produced, which means keeping
the cascade's intermediate states rather than folding as it goes.

**`font-size` is deliberately NOT in the inherited set**, and it is the one real gap.
Its computed value is an absolute length, so inheriting the text would let `1.5em`
compound against each descendant's own size instead of being resolved once.
`box_builder` already resolves it correctly against the parent's px, so it stays there
until S3b's unit folding moves the resolution into the cascade. The same argument
covers any inherited property carrying a relative unit.

The render effect: 24 lines across four fixtures, all of one shape - the literal string
`inherit` becoming the parent's actual value. `.alert-heading { color: inherit }` and
`.form-check-input { line-height: inherit }` used to reach paint as the word "inherit",
where `parse_color` simply failed. The Chrome diff falls 7,703 -> 7,695, and only that
much because the values those now resolve to are themselves unresolved `var()`.

### S4a: the rung where Bootstrap starts looking like Bootstrap

Substitution is a TOKEN-STREAM operation, and `rgba(var(--bs-body-color-rgb), .5)` is
the case that settles why: the var() expands to `33, 37, 41`, three arguments where the
source had one. So it runs on text, at computed-value time, before any grammar looks at
a value - which is also what will let `border: var(--w) solid var(--c)` be expanded into
longhands once S4b can count its components.

**753 unresolved `var()` in the baselines became 0**, and `.btn` came alive:
`padding: auto auto` became `6px 12px`, `line-height` became `1.5`, and the button grew
from 128x26 to 152x38 as its padding finally applied.

| | differ | substituted |
|---|---|---|
| after S3a | 7,695 | 19,848 |
| after S4a | **5,355 (-30.4%)** | 20,364 (+516) |

**`substituted` went UP, and that is the metric being honest about something rather
than a regression.** Bootstrap ships seventeen EMPTY custom properties, and
`body { text-align: var(--bs-body-text-align) }` reads one of them. An empty token
stream is a valid substitution but not a valid value for an ordinary property, so per
§3 the declaration is invalid at computed-value time and therefore `unset` - which is
why ctbrowser now answers nothing where it used to answer the literal string
`var(--bs-body-text-align)`. The harness substitutes the CSS initial value, `start`,
Chrome says `start`, and the difference disappears. So `differ` fell *because*
`substituted` rose: garbage was replaced by a correct absence. The ratchet treats a
rising `substituted` as a regression and it is right to in general - but not across a
rung that makes wrong values correctly absent, and it needed `--advance` for that
reason.

Getting that right was worth the detour. Storing the empty value instead of unsetting
would have SHADOWED the inherited one - behaving like `initial` rather than `unset` - on
405 elements of a single fixture.

Two spec details that are easy to get backwards, both now pinned by tests. IACVT means
`unset`, NOT "drop the declaration and let the earlier one win": `color: red; color:
var(--missing)` renders as the INHERITED colour in Chrome, not red. And `!important` on
a custom property is the DECLARATION's importance, so it is peeled where every other
declaration's is and a var() cannot smuggle it into the property that reads it - the
obvious guess is that the value keeps it.

### S4b: why the cascade needs two passes

Custom properties are themselves cascaded, so substitution cannot run inside the fold
that produces the values it needs to read. Pass one applies ONLY custom properties; pass
two substitutes everything else against them, expands shorthands, and folds. Both passes
walk the same sorted list with the same inline-style splice, so priority is identical -
and expansion in pass two keeps source order for free, because a shorthand's longhands
land at the shorthand's position in the fold. That is exactly what
`test_shorthands_expand` pins, and it passes verbatim.

Expansion HAD to move there. `border: var(--all)` is one token that becomes three, so a
shorthand's component count is unknowable before substitution - which is why the `border`
shorthand had never been expandable and had therefore never been expanded. Paint reads
`border-width` and `border-color`; the shorthand set neither; every card, alert and table
border was invisible. Bootstrap writes it 34 times.

`border` is `<width> || <style> || <color>` in ANY ORDER, so its parts are classified by
what they are rather than by where they sit - unlike the positional side lists. And a
shorthand sets every longhand it governs including the ones it did not mention, which is
what makes `border: 0` reset a style set elsewhere.

**I added this without a test first, and it was wrong in a way the render hid.** The page
came back byte-identical, which I nearly accepted: `<button>` is a REPLACED element whose
border is drawn by the widget painter rather than from CSS, so the buttons that fill the
visible region looked right either way. Querying the computed values instead showed
`.alert` reporting `border-color: "1px solid #9ec5fe"` - the whole shorthand in the
colour - because a stale expansion site was still running BEFORE substitution. The
lesson is the ordinary one: the test would have said so in one second, and the render
could not.

### S10 has a dependency the plan did not record: inline-block shrink-to-fit

`display: inline-block` is PARSED and then never used. `parse_display` returns
`display_kind::inline_block`, and `box_builder` maps everything that is not
`inline_level` to `box_kind::block` (`include/ctbrowser/layout/box.hpp`), so an
inline-block box fills its containing block. That is already visible: `.btn` on an
`<a>`, and `.btn` with `.d-grid`, span the whole row in the components render.

So de-replacing `<button>` cannot come first. Today a button's width is
`text_width(label) + 16`, which shrink-wraps by accident because it is a REPLACED
element sized from its content; take that away and every button becomes full-width.
The order has to be:

1. **inline-block as an inline-LEVEL box with shrink-to-fit width** -
   `clamp(min-content, available, max-content)`. The machinery is already there:
   `intrinsic_sizes` with `min_content`/`max_content`, and `inline_flow` already
   measures children that way. The likely shape is a `bool inline_level` on
   `box_node` - `kind = block` for the inside, inline-level for the outside - which
   is the same field S9 needs for `inline-flex`, so it should be added once.
2. **then** `<button>` out of `is_replaced_tag`, with its size coming from the UA
   sheet's padding and border rather than from `intrinsic_size_of`.

Worth knowing before starting: the button branch of `browser::paint_replaced` draws
only a frame and the label - the FACE already comes from the UA sheet's
`button, select { background-color: #e9e9ed }` through the ordinary paint path. And
`<input type=button|submit|reset>` stays replaced and keeps using that branch, so it
does not move.

**AND THE IMAGE GOLDENS DO RUN ON THE DEVBOX**, which S0 left as unverified and
`docs/build.md` still denies: `ctest -R "render-widgets|render-elements"` passes there
with no SDL and no display, because `check-render.cmake` pins `CTBROWSER_FONTS=font8x8`
and the software rasteriser is deterministic. So a golden-moving rung can be verified
in the ordinary loop after all, rather than needing a machine with SDL.

### @media is now the bottleneck, and that reorders the plan

The biggest single cluster left is a `+44px` `@x` shift on 27 of 40 elements of the
smallest fixture: `.container`'s auto-margin centring (32px) plus its padding (12px).
`min/max-width` and auto margins are implemented and tested now - and they buy **six**
differences, because:

    .container  max-width=1320px      ctbrowser
    .container  max-width=960px       Chrome at a 1024 viewport

Every `@media` block flattens in, so the `xxl` breakpoint's 1320px wins by source
order, and 1320 does not clamp 1009. Auto margins then have no remainder to centre
with, because `.container` is `width: 100%` and only a clamped max-width creates one.
So the whole cluster - the shift, the widths, and everything measured against the
wrong basis - is one missing feature: **real media-query evaluation**. It should move
ahead of the rest of the box model.

`line-height` was the other half of the text work and it did pay: 5,355 → 4,451
across two changes, because it needed BOTH the layout fix and a reporting fix -
`getComputedStyle` answered the cascade's factor `1.5` where Chrome reports the
resolved `24px`, which differed on every text-bearing element for a reason that had
nothing to do with layout.

**Two bugs in this rung were caught by tests that did not exist an hour earlier**, and
both would have shipped silently. An unset margin is `unit::auto_` exactly like an
explicit `auto`, so reading the length rather than a flag centred every definite-width
box that declared no margins - the event tests found it by clicking where an element
used to be. And `min_width_`/`max_width_` never reached the constructor's initialiser
list, so they were null atoms reading empty strings and the clamping was dead code that
compiled, linked and passed everything except its own test.

### S9: flex, and the grid stops being a stack of full-width blocks

The remainder really was concentrated in one feature. On the grid fixture 102 of 111
elements differed on `@y` for a single reason - every `.col` was a block, so every
column sat below the one before it.

| fixture | differ before | after | substituted before | after |
|---|---|---|---|---|
| box | 69 | 69 | 1,387 | 1,308 |
| type | 136 | 136 | 2,093 | 1,969 |
| **grid** | **490** | **173** | 3,457 | 3,234 |
| components | 1,091 | 977 | 5,921 | 5,554 |
| position | 303 | 291 | 2,078 | 1,951 |
| kitchen | 1,051 | 886 | 5,243 | 4,915 |
| **total** | **3,140** | **2,532** | 20,179 | 18,931 |

(and 2,458 after the intrinsic-sizing bug flex uncovered in `block_flow::measure`, below)

The grid fixture is where the rung is gated and it fell **65%**. What is left on it is
no longer flex: 102 of its 173 are one `@y` offset of **-0.0156px** - a single 1/64
rounding difference on `h1.fs-4`'s height, propagated down the page by the report's
own ranking rule - 24 are `.row`'s `margin-top: calc(-1 * var(--bs-gutter-y))`
answering `auto` where Chrome reports the initial `0px`, and most of the rest are
Chrome's `LayoutUnit` flooring `33.333333%` of 960 to 319.984375 where this answers 320.

The traced case works exactly as predicted: `.container` 960 with 12px padding gives
936, `.row`'s -12px margins widen it back to **960**, three `.col`s take **320** each.
So does the case that needs the real §9.7 loop rather than one proportional pass - a
`.col` holding one unbreakable 624px token takes 648 and its sibling the remaining 312,
and the row below it with `min-width: 0` splits 480/480, which is why Bootstrap's
`.card` carries that declaration.

**`min-width`/`min-height` are answered rather than left blank, and the answer depends
on the parent.** Chrome reports `auto` for a flex item - where `auto` means the
content-based minimum and genuinely is not a length - and resolves it to `0px`
everywhere else. That was 139 of the grid fixture's 490 differences, every one about
which parent an element has rather than about the element. The harness's initial-value
table cannot fix it: forcing `auto` there moved 566 differences the *wrong* way,
because most elements on most pages are not flex items.

**Two things landed with flex because flex cannot be correct without them.**
`display` is *blockified* on a flex item (CSS Display 3 §2.7), which is why
`<a class="nav-link">` inside a `.nav` reports `block`; and anonymous item generation
wraps **text runs only** - an `<img>` between two text nodes is an item in its own
right, and wrapping it would make two images one item.

**The parallel driver was the thing most likely to ship broken, and it did not.**
`engine::run_parallel`'s guard existed for inline containers, whose children merely
share a line. A flex container's children share *free space*, so item i's width is a
function of item j's - the exact opposite of the independence the driver rests on.
Both halves are closed: `split_point` refuses to descend into a flex container, and the
guard refuses to split at one. Two cases in the parallel-equals-sequential test landed
in the same commit, at `parallel_min_boxes = 0`: 64 flex rows (the driver must split
*above* them) and one flex container holding 64 items (it must refuse and fall back).

**An 18-claim adversarial review of the freeze loop found six real spec bugs that 25
passing tests could not see**, all of them latent on Bootstrap - `bootstrap_layout` came
back byte-identical after fixing them, which is the evidence that they were latent
rather than that the fixes were nothing:

- **§9.7.4's sub-one factor share is of the INITIAL free space**, not of what is left
  after some other item froze. `flex-grow: .25` beside an item that hit its `max-width`
  came out 225 where Chrome says 250. Invisible with one item or with factors summing
  past one, which is every other test here.
- **A shrink is weighted by the item's INNER (content-box) base size.** Two 200px items
  shrinking into 300px are 150/150 only if neither is padded; give one 100px of padding
  and it gives up a third of the deficit rather than half.
- **"Single-line" means `flex-wrap: nowrap`** (§5.2), not "produced one line". The two
  agree for the default `align-content`, which stretches the one line to fill the
  container anyway - which is why the Bootstrap rows do not move.
- **Stretch is clamped by the item's own min/max cross size** (§9.4.11). The column axis
  already clamped and the row axis did not, so the two disagreed about one rule.
- **A percentage maximum that cannot resolve behaves as `none`.** Resolved against zero
  it became a definite maximum of 0, which clamped the automatic minimum to 0 too and
  collapsed the item - `max-height: 100%` in a column with no stated height gave 0.
- **The leading margin is the one at the flex-START edge**, which under `row-reverse` is
  `margin-right`. Naming them physically offset every reversed item by exactly
  (trailing - leading), which is invisible whenever the two are equal.

Recorded as known differences rather than approximated: `flex-basis: content`, baseline
alignment, absolutely-positioned items, a `%` basis against an indefinite main size, and
§9.9.1's max-content flex fraction (a growable item's max-content contribution is the
larger of its base and its content size, which is the same answer when one item
dominates and an under-estimate otherwise).

### And one bug flex found in code eight rungs older than it

`block_flow::measure` and `inline_flow::measure` asked how wide a child's CONTENT
wanted to be and then added nothing - so a child's own padding, margins and stated
width were dropped from every intrinsic size in the engine. A `<td>` holding a
`<div style="padding: 50px">hello</div>` measured 19px wide, and the div was then laid
out at a content width of `max(0, 19 - 100) = 0`, where `words_that_fit` answers 0 for a
non-positive width and **the text produced no fragment at all**.

Before flex that path was reachable only from a table cell, and no page in the suite
puts a padded box in one, which is why it sat there. Flex reaches it on every item whose
base size comes from its content, and the visible symptom was every Bootstrap
`.nav-item` measuring exactly its `.nav-link` child's padding too narrow. `outer_intrinsic`
is now the one function that answers "what does this child contribute", and flex asks it
too rather than keeping a second copy: components 977 → **927**, kitchen 886 → **862**,
no image golden moved.

### An invalid calc() is an invalid declaration, not a string nobody can read

`fold_calc` left an unevaluable `calc()` as text, on the stated reasoning that
"whoever reads the value next cannot parse it and drops the declaration". That was
wrong, and only the Chrome diff could show it: layout's `parse_length` answers
**`auto`** for a string it cannot read, and `auto` is not the same as absent. Chrome
reports the property's *initial* value, which is what an absent declaration produces
here.

Bootstrap hits it on every `.row`: `margin-top: calc(-1 * var(--bs-gutter-y))` with a
gutter of `0` multiplies a number by a number and gets a **number**, which is not a
length. 24 elements of the grid fixture, and 36 differences once the rows below moved
with them.

Which *kind* of invalid it is decides what happens to an earlier declaration, and the
two are observably different - so `fold_calc` reports failure and the cascade
remembers whether the value went through `var()`. A substituted value is invalid at
computed-value time, which §3 spells `unset`, so it removes the earlier declaration it
beat; one that never contained a `var()` is invalid at parse time, so the earlier
declaration simply wins. Both are pinned. grid 173 → **137**, kitchen 862 → **859**,
and `substituted` rose by exactly the same count - the S4a effect again, a wrong value
replaced by a correct absence.

### S10 and S12a: the rung a SCREENSHOT chose, not the harness

The computed-style gate said components was the biggest number left, but not why.
`tools/check/compare.py shot` said why in one image, once it could screenshot a remote
engine at all - and the answer was three things the property diff was blind to or
quiet about.

**Every `.text-bg-*` element painted nothing.** `parse_color` matched `rgb(`
case-SENSITIVELY and Bootstrap writes `RGBA(...)` in capitals 62 times, so every badge
and every coloured label was laid out at the right size, reported the right computed
value, and drew a blank rectangle. **The parity numbers did not move by one when it was
fixed**: css-parity.py normalises both sides' text, so the two spellings already
compared equal. The harness measures computed values and this lived entirely
downstream of them - the plan's "the front end will outrun layout" warning, arriving
from the other end.

**`inline-block` was parsed and thrown away.** Every `.badge` was a full-width bar and
`.btn` on an `<a>` spanned the row. `inline_level` already existed for `inline-flex`;
`shrink_to_fit_width` moved beside the other width functions and flex's private copy
went. Its BASELINE is its last line's, not its font ascent (§10.8.1) - a difference of
exactly the top padding, which is what hangs a badge above the sentence it is in.

**`border-radius`**, as radii on `fill_rect` rather than a second op, with `ring` for
the border. A ring is the difference of two coverages of a signed distance field, which
antialiases both edges for free and gets `.btn-outline-primary` right - a 1px ring
around nothing, which "fill the box then fill the inside" would flood. The §5.1 scaling
is in `display_list::fill_rounded`, the one place every producer goes through: it is
what turns `.rounded-pill`'s 50rem into half a badge's height.

**`<button>` left `is_replaced_tag`** and its size moved into the UA sheet as real
`padding: 3px 8px; border: 1px solid`, chosen to reproduce the old intrinsic numbers
exactly - so the widgets golden moved by a 1px label shift and nothing else. Being
replaced was the only reason it shrink-wrapped, and also the reason a `.btn`'s padding,
border and radius did nothing at all.

That last one put the ratchet UP - components 906 → 964 - because a `.btn` is
`border: 1px solid transparent` and **borders were not in the box model at all**. Every
button came out 2px narrower and 2px shorter, and over fifty button rows that is a
hundred pixels of drift. Rather than record the regression, the border half of S7b
landed with it: `resolved_edges` carries four border floats, `horizontal_inner()` and
`content_top()` replace the padding-only accessors at every one of the twenty call
sites, and `border-style: none` means a used width of zero.

| fixture | before S10 | after |
|---|---|---|
| box | 69 | **49** |
| grid | 137 | 137 |
| components | 906 | **704** |
| position | 291 | **272** |
| kitchen | 835 | **738** |
| **total** | 2,458 | **2,036** |

`bootstrap-components.html` now reads as Bootstrap: rounded buttons at Chrome's
widths, outline buttons as rings, pills, and alerts with their rule and their last
line. What is left on it is `position`, `box-shadow` and `text-align`.

### S11a: positioning is a PASS, not a formatting context

Every other piece of layout here is a box asking about its own children.
Positioning is the one part of CSS where the box that decides where a child goes is
**not its parent** - an `absolute` box is placed against the nearest positioned
ancestor, which may be ten levels up and which finished laying out long before the
child was reached.

So the flows do the one thing they can: an out-of-flow child reserves no space and
leaves an **empty fragment where it would have gone**. That marker is not
bookkeeping - it *is* the static position, which is exactly the number CSS says to
use for whichever offset is `auto`, and `.position-absolute` with no offsets at all
relies on it. `layout::apply_positioning` then walks down carrying the ancestor
chain and by the time it reaches the marker it knows both the containing block and
the static position. Running after layout also keeps the parallel driver's
invariant intact: an out-of-flow box affects no sibling, so the concurrent pass
never has to know it exists.

Three things that look alike and are not, each with a test:

- `relative` **leaves its slot behind** and `absolute` does not.
- the containing block is a **padding** box, not a content box - invisible until
  the anchor has padding, and then wrong by exactly that padding on every
  descendant.
- **both** offsets on an axis stretch a box where **one** shrink-wraps it, which is
  why `.position-absolute.top-0.start-0` is the size of its text.

`fixed` is placed against the **window**, not the document, which is what puts
`.fixed-bottom` on screen rather than after the last paragraph - that needed a
viewport height threaded into `engine::run`, and it is the only thing in layout
that uses one. `translateX()` and `translateY()` are separate functions from
`translate()`, and Bootstrap writes both.

**The insets are reported as USED values**, which is what Chrome does: `auto` only
for a static element, and for a positioned one the number the box ended up at
whether or not the sheet wrote it. Answering the declared text was 104 of the
position fixture's 222 - and it also drops `substituted` by about 2,000 across the
six fixtures, because four properties on every element stopped being unanswered.

| fixture | before S11a | after |
|---|---|---|
| box | 49 | 49 |
| type | 136 | **132** |
| grid | 137 | 137 |
| components | 704 | **574** |
| position | 272 | **129** |
| kitchen | 738 | **680** |
| **total** | 2,036 | **1,701** |

`bootstrap-position.html` now renders the way Chrome renders it, `fixed-top` and
`fixed-bottom` included. What is left on it is `z-index` and the 1/64px cluster.

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
| **S1** | **Tokenizer + component values + grammar**, feeding the existing cascade unchanged. ctcss out of `engine.hpp`, out of style's public interface, and out of the install | **DONE.** All 15 `style_basics` tests pass **verbatim**, the `bootstrap_layout` baselines and `tests/golden/page.ppm` are **byte-unchanged**, `check-package.sh` green. Retained compiled selectors 6,289 → **2,550**, dead ones 650 → **0**. `add_sheet` on 297 KB: **3.5 ms** against a 15 ms target. The remaining perf items - O(1) `put()`, values as views, packed specificity in `rule`, the ancestor-facts stack - are deferred to the rungs that need them, so this one stayed a pure substitution |
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
| **S7b** | `box-sizing`, borders in `resolved_edges`, `min/max-height`, and the `clientWidth`/layout-width inconsistency | `bootstrap-box.html` → near zero; **zero golden movement expected — verify** |
| **S8** | **Text metrics.** `line-height` (replacing the hardcoded `line_height_factor = 1.25f`), `text-align`, `vertical-align` | `bootstrap-type.html`; **moves every text golden** — taken early on purpose |
| **S9** | **Flex**, including the `run_parallel` independence guard | **DONE.** `bootstrap-grid.html` **490 → 173**, the whole Chrome diff 3,140 → **2,458**, and what is left on the grid fixture is not flex. 30 tests in a new `flex_basics.cpp`, two flex cases in parallel-equals-sequential, and six spec bugs found by an adversarial review that the 25 original tests could not see. **No image golden moved** |
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
