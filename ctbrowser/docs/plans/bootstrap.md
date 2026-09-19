# Bootstrap 5.3.8 at Chrome parity

**Where it is. The Chrome diff over the six fixtures is 1,443, down from 7,820 when
this started; 29 coarse screen cells differ, down from 68 at the preceding rung.
The authoritative per-fixture numbers are in `tools/check/css-parity.txt`, and
`bootstrap_layout` pins ctbrowser's side of the same table without a browser.**

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

· **S11b** in-layer stacking contexts: integer `z-index`, the supported CSS 2.1
Appendix E phases, atomic opacity/transform contexts, and ancestor clips retained
across reordered paint.

· **CSS 2 block margin collapsing**, including empty collapse-through chains,
parent/child edges, formatting-context boundaries and the sequential merge of
parallel layout results · block `min/max-height`, which margin eligibility needs.

**Next:** collapsed-border conflict resolution for tables, a real blur for
`box-shadow`, and fixed/sticky as their own non-scrolling layers. Chrome's
`LayoutUnit` quantisation remains LAST:
102 of the grid fixture's 137 are one 1/64px rounding difference propagating, and
that work is worth zero screen cells today.

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
| `:root, [data-bs-theme=light]` | **dropped whole.** `:root` is an unknown pseudo, so the alternative is `impossible` (`third-party/compile-time-css/include/ctcss/value.hpp:195`), and `[data-bs-theme=light]` is an attribute selector, which is not modelled. All 128 global `--bs-*` are unreachable |
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

## Detailed sections

The following sections retain the complete original text and measurements.

<a id="what-the-harness-says-now"></a>
- [What the harness says now](bootstrap/01-what-the-harness-says-now.md#what-the-harness-says-now)
<a id="what-s1-measured"></a>
- [What S1 measured](bootstrap/01-what-the-harness-says-now.md#what-s1-measured)
<a id="s2a-and-a-finding-about-the-harness-itself"></a>
- [S2a, and a finding about the harness itself](bootstrap/01-what-the-harness-says-now.md#s2a-and-a-finding-about-the-harness-itself)
<a id="s2b-and-what-a-sibling-combinator-costs"></a>
- [S2b, and what a sibling combinator costs](bootstrap/01-what-the-harness-says-now.md#s2b-and-what-a-sibling-combinator-costs)
<a id="s2c-and-the-rung-where-the-chrome-gate-finally-moved"></a>
- [S2c, and the rung where the Chrome gate finally moved](bootstrap/01-what-the-harness-says-now.md#s2c-and-the-rung-where-the-chrome-gate-finally-moved)
<a id="s3a-inheritance-and-what-the-split-cost"></a>
- [S3a: inheritance, and what the split cost](bootstrap/01-what-the-harness-says-now.md#s3a-inheritance-and-what-the-split-cost)
<a id="s4a-the-rung-where-bootstrap-starts-looking-like-bootstrap"></a>
- [S4a: the rung where Bootstrap starts looking like Bootstrap](bootstrap/01-what-the-harness-says-now.md#s4a-the-rung-where-bootstrap-starts-looking-like-bootstrap)
<a id="s4b-why-the-cascade-needs-two-passes"></a>
- [S4b: why the cascade needs two passes](bootstrap/01-what-the-harness-says-now.md#s4b-why-the-cascade-needs-two-passes)
<a id="s10-has-a-dependency-the-plan-did-not-record-inline-block-shrink-to-fit"></a>
- [S10 has a dependency the plan did not record: inline-block shrink-to-fit](bootstrap/01-what-the-harness-says-now.md#s10-has-a-dependency-the-plan-did-not-record-inline-block-shrink-to-fit)
<a id="media-is-now-the-bottleneck-and-that-reorders-the-plan"></a>
- [@media is now the bottleneck, and that reorders the plan](bootstrap/01-what-the-harness-says-now.md#media-is-now-the-bottleneck-and-that-reorders-the-plan)
<a id="s9-flex-and-the-grid-stops-being-a-stack-of-full-width-blocks"></a>
- [S9: flex, and the grid stops being a stack of full-width blocks](bootstrap/01-what-the-harness-says-now.md#s9-flex-and-the-grid-stops-being-a-stack-of-full-width-blocks)
<a id="and-one-bug-flex-found-in-code-eight-rungs-older-than-it"></a>
- [And one bug flex found in code eight rungs older than it](bootstrap/01-what-the-harness-says-now.md#and-one-bug-flex-found-in-code-eight-rungs-older-than-it)
<a id="an-invalid-calc-is-an-invalid-declaration-not-a-string-nobody-can-read"></a>
- [An invalid calc() is an invalid declaration, not a string nobody can read](bootstrap/01-what-the-harness-says-now.md#an-invalid-calc-is-an-invalid-declaration-not-a-string-nobody-can-read)
<a id="s10-and-s12a-the-rung-a-screenshot-chose-not-the-harness"></a>
- [S10 and S12a: the rung a SCREENSHOT chose, not the harness](bootstrap/01-what-the-harness-says-now.md#s10-and-s12a-the-rung-a-screenshot-chose-not-the-harness)
<a id="s11a-positioning-is-a-pass-not-a-formatting-context"></a>
- [S11a: positioning is a PASS, not a formatting context](bootstrap/01-what-the-harness-says-now.md#s11a-positioning-is-a-pass-not-a-formatting-context)
<a id="s11b-a-stack-level-belongs-to-a-context-not-a-parent"></a>
- [S11b: a stack level belongs to a CONTEXT, not a parent](bootstrap/01-what-the-harness-says-now.md#s11b-a-stack-level-belongs-to-a-context-not-a-parent)
<a id="a-percentage-height-needs-a-containing-block-to-be-a-percentage-of"></a>
- [A percentage height needs a containing block to be a percentage OF](bootstrap/01-what-the-harness-says-now.md#a-percentage-height-needs-a-containing-block-to-be-a-percentage-of)
<a id="the-screen-cell-metric-and-the-largest-paint-bug-this-project-has-had"></a>
- [The screen-cell metric, and the largest paint bug this project has had](bootstrap/01-what-the-harness-says-now.md#the-screen-cell-metric-and-the-largest-paint-bug-this-project-has-had)
<a id="initial-on-a-custom-property-and-the-shadow-bootstrap-paints-tables-with"></a>
- [`initial` on a custom property, and the shadow Bootstrap paints tables with](bootstrap/01-what-the-harness-says-now.md#initial-on-a-custom-property-and-the-shadow-bootstrap-paints-tables-with)
<a id="the-table-and-three-shorthands-that-were-not-shorthands"></a>
- [The table, and three shorthands that were not shorthands](bootstrap/01-what-the-harness-says-now.md#the-table-and-three-shorthands-that-were-not-shorthands)
<a id="two-findings-that-were-not-predicted"></a>
- [Two findings that were NOT predicted](bootstrap/02-two-findings-that-were-not-predicted.md#two-findings-that-were-not-predicted)
<a id="the-forms-overhaul-and-where-a-stated-width-stops"></a>
- [The forms overhaul, and where a stated width stops](bootstrap/02-two-findings-that-were-not-predicted.md#the-forms-overhaul-and-where-a-stated-width-stops)
<a id="margin-collapsing-and-the-rung-the-screen-cell-metric-valued-far-more"></a>
- [Margin collapsing, and the rung the screen-cell metric valued far more](bootstrap/02-two-findings-that-were-not-predicted.md#margin-collapsing-and-the-rung-the-screen-cell-metric-valued-far-more)
<a id="the-decisions-this-plan-rests-on"></a>
- [The decisions this plan rests on](bootstrap/02-two-findings-that-were-not-predicted.md#the-decisions-this-plan-rests-on)
<a id="the-harness"></a>
- [The harness](bootstrap/02-two-findings-that-were-not-predicted.md#the-harness)
<a id="the-ladder"></a>
- [The ladder](bootstrap/02-two-findings-that-were-not-predicted.md#the-ladder)
<a id="per-rung"></a>
- [Per rung](bootstrap/02-two-findings-that-were-not-predicted.md#per-rung)
<a id="performance-targets-stated-so-they-can-fail"></a>
- [Performance targets, stated so they can fail](bootstrap/02-two-findings-that-were-not-predicted.md#performance-targets-stated-so-they-can-fail)
<a id="the-assertions-that-must-change-and-why-they-were-wrong"></a>
- [The assertions that must change, and why they were wrong](bootstrap/02-two-findings-that-were-not-predicted.md#the-assertions-that-must-change-and-why-they-were-wrong)
