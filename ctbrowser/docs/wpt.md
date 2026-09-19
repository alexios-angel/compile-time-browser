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
