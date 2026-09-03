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

## 5. Re-running this

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
