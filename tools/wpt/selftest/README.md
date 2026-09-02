# The negative proofs

Five pages, each with one required outcome, run by `tools/wpt/run-wpt.py
--selftest` and **asserted** rather than read off a table.

A harness that has only ever been seen to report passes is not evidence of
anything: every one of these failure modes can be, and at some point was,
reported as a pass by something.

| fixture | must be reported as | why it is here |
|---|---|---|
| `must-pass.html` | `PASS`, 2 subtests, both PASS | the floor: a working test is not reported as broken |
| `must-fail.html` | `FAIL`, 2 subtests, 1 PASS + 1 FAIL | **a test that must fail is reported as failing** — and the passing subtest beside it proves the whole file is not simply being called bad |
| `never-done.html` | `TIMEOUT` | `setup({explicit_done:true})` and `done()` is never called. The harness loaded and ran; it never finished |
| `throws-on-load.html` | `HARNESS_ERROR` | an exception during load, before any test is defined. Not a pass, and not a timeout |
| `no-harness.html` | `HARNESS_ERROR`, 0 subtests | `testharnessreport.js` without `testharness.js`. **A page with no harness reports zero subtests, and zero subtests must never read as a pass** |

They are ordinary WPT pages: they load `/resources/testharness.js` through the
same document root every real test does, so a run of these also proves the
server-absolute path resolution the whole corpus depends on.
