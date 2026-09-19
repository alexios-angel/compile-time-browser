# The CSS suites, measured

`ctbrowser/docs/wpt.md` is the instrument and the whole-corpus baseline. This
file is the **two CSS suites**, in more detail than that table has room for:
what the numbers are, what moved them, and — the part that matters most here —
**which subsystem each remaining failure actually belongs to.**

    tools/wpt/run-wpt.py --dir css/cssom     --jobs 4
    tools/wpt/run-wpt.py --dir css/css-values --jobs 4

Everything below was measured on the devbox against WPT `3f6b09ae`, four
workers, a 4 GB `ulimit -v` per driver, `CTBROWSER_GL_DRIVER=deterministic`.

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
