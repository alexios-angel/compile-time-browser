[Back to wpt.md](../wpt.md)

## The negative proofs

**A harness that has only ever been seen to report passes is not evidence of
anything.** `tools/wpt/run-wpt.py --selftest` runs five pages from
`tools/wpt/selftest/` and **asserts** each outcome and each subtest count —
never reads them off a table.

    fixture                              want            got   subtests
    must-pass.html                       PASS           PASS   2 {PASS: 2}      ok
    must-fail.html                       FAIL           FAIL   2 {FAIL:1,PASS:1} ok
    never-done.html                   TIMEOUT        TIMEOUT   1 {PASS: 1}      ok
    throws-on-load.html         HARNESS_ERROR  HARNESS_ERROR   0                ok
    no-harness.html             HARNESS_ERROR  HARNESS_ERROR   0                ok

**All five reported the outcome they must**, measured 2026-09-02 and again on
2026-09-03 after six DOM changes. Three of them did not, the first time they
were run, and each miss was a real engine defect — see the table above.

### And the gate itself, proved both ways

A gate nobody has watched fail is not a gate. Re-proved on 2026-09-03 against
the current expectations file, on the `--gate` subset:

| what was done to `expectations.txt` | gate says | exit |
|---|---|---:|
| nothing | `matches expectations.txt exactly` | 0 |
| deleted one SUBTEST line of a test that fails | `+ dom/events/Event-dispatch-click.html SUBTEST FAIL "basic with click()"` | 1 |
| added a `FAIL` line for a test that passes | `1 UNEXPECTED PASS(ES): - dom/events/Event-type.html FAIL` | 1 |
| restored | `matches expectations.txt exactly` | 0 |

**A line that names a test outside the `--gate` subset proves nothing**, which
the first attempt at this found: deleting
`dom/events/AddEventListenerOptions-passive.any.js FAIL` left the gate green,
because `--check` and `--gate` scope the comparison to the tests the run
actually ran. That is the documented behaviour and it is what lets one file
serve both the two-minute gate and a full-suite sweep — but it means a
falsification has to pick a line the gate will actually visit, or it silently
asserts nothing.


`tools/wpt/selftest/README.md` says what each one is for. The two that matter
most: `must-fail.html` contains one assertion that is false on purpose beside one
that passes, so a runner that called the whole file bad would be caught as
surely as one that called it good; and `no-harness.html` loads the report hook
*without* `testharness.js`, which is exactly the shape every test in the corpus
had before the document root existed — zero subtests, no error, and a naive
runner scores it green.

## Moving the pin

Deliberate, with a re-baseline attached. WPT adds and renames tests every day and
`expectations.txt` is keyed on test paths.

```bash
# 1. edit WPT_COMMIT in tools/wpt/fetch-wpt.sh
tools/wpt/fetch-wpt.sh
# 2. re-measure every suite in the baseline table, one at a time
tools/wpt/run-wpt.py --dir dom/nodes --update-expectations
# 3. update the table in this file, with the date and the real numbers
```

`run-wpt.py` refuses to run if the checkout's `HEAD` is not the commit
`fetch-wpt.sh` names. A table read against the wrong corpus is worse than no
table, so the two are compared rather than assumed.
