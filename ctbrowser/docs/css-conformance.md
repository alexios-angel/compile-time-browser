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

## 2. The baseline, 2026-09-03

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
| ~1,000 (`css-values`) | `e.style[prop] = v` then read back is the raw text | `lib/Shell/bindings/element.cpp` |

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

Two real defects in `lib/Style/css/calc.cpp`, both found by reading these
failures rather than by guessing. Both are proved by
`unittests/unit/style_basics.cpp`, and neither moved a render golden.

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

## 6. Re-running this

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
