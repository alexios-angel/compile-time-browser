# Caught helper source admission, 2026-09-24 UTC

Resumed clean `0fbcf02a`, latest155 HANDOFF/Current native work, both areas' history
and synchronization journals. `codex-wip-20260907` was already an ancestor.
No abandoned edits remained. The previous session froze helper source `2b755d49`
but had only admitted its raw helper-chain shape. This session completes that
unchanged JavaScript source boundary in `cf112dae`.

## Landed

The original helper reads `hasAttribute`, performs two ordered `setAttribute`
calls and returns `{done, value}`. Its caller reads the result inside a catch that
observes node identity. Before this change the source refused with the URI
fallback diagnostic. The baseline simplified IR is preserved in the evidence.

CFG lifting now preserves complete handler register vectors when local closures,
cells and capture reads occur. Recovery keeps the local Try and every checked
invocation. Only immediately after fresh recovery, with preexisting source poison
excluded, the preparation pass forwards a captured identity whose active incoming
values all name the same dominating cell or closure. It never substitutes cell
contents or infers an identity from call results or error payloads. Unused tuple
positions and matching saved-state arguments disappear; producers and calls stay.

Existing helper expansion can then prove the captures and expand the ordered
attribute chain. Try regions remain intact through this stage. Independent
attribute effects, own-field forwarding and the existing catch completion
projector run before complete DOM reproof and publication. Failure retains the
original module and contract. No browser implementation, emitter or runtime
changed. Output uses typed native values and the existing public DOM helpers.

The exact source now executes under GCC and Clang with both DOM providers,
optimization enabled/disabled and explicit/deduced printing. Repeated calls on
the same element and another document check the saved read, returned identity,
write order, names, targets and values. Invalid attribute names, an impure helper,
an unproved protected getter, missing Element facts, zero budget and supplied
source poison retain refusals. All 31 earlier local source strings, two original
outer strings and 34 frozen artifacts remain byte-identical.

## Exact focused validation

- Locked devbox builds used explicit targets `ctcompile-test-exception-recovery`,
  `ctcompile-test-host-contract`, `ctjs-opt`, `ctjs-translate` and
  `ctcompile-test-native-reference`. No local C++ build.
- `ctcompile_exception_recovery`: final **1/1 PASS, 5.36 s**, **5.37 s total**.
  The preceding successful candidate passed **5.45 s**, **5.46 s total**.
- `ctcompile_host_contract`: **1/1 PASS, 2.96 s**, retained from the preceding
  run. Subsequent production change only restated the frontend preservation
  allowlist to avoid unrelated formatter churn; test controls were corrected.
- Selected `native_dom_caught_node.py` sources `invocation-return`,
  `write-boolean-snapshot`, `helper-next-record`, all refusal controls and both
  original outer programs: **48 native executions, 108 refusals, ten Node/VM
  runs PASS**. This is not a whole fixture or lit pass.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**;
  `git diff --check` passes. Independent nine-file static review is clean.
  Final source hashes match the devbox. Twenty-four generated C++ files contain
  no Script/AOT/ctjs namespace, and the native harness's binary-symbol checks pass.
  A final Python status-line-only edit removed stale whole-fixture counts;
  syntax/Black checks passed without replaying successful native checks.

Initial focused failures exposed lost frontend register vectors, then captured
identities still transported in completion tuples. Those production gaps were
fixed. A later test-only edit accidentally extended an older raw control loop;
its original bound was restored, and the source-poison control was added to the
new helper test before the final pass. No historical expectation was relaxed.
The first devbox invocation completed its build only: SSH consumed subsequent
heredoc commands. Later invocations isolated stdin and recorded the actual tests.

Skipped: full CTest/compiler lit, complete native fixtures, unaffected native
replay, broad corpus/matrices, full Bootstrap, WPT/test262, Windows, sanitizers
and escape tests. Parallel source tracing/test work and final independent review
completed; rate limits stopped the escape agent before a candidate or baseline.

## Exact next boundary

Original observing outer getter `1a7fb166` and method `7acf503b` remain refused at
`DOMIteratorClose.cpp`. Their completion recovery and custom protocol consumer
must be connected without selecting the suppression path. Both source close
calls pass `false`, and failure must reach the observing catch. Discovery still
rejects retained Try/result-bearing Invoke regions, requires a root-local open
and follows direct record users. Preserve original saved-register correspondence,
exhaustion, cleanup, saved returns and caught-node identity while extending it.
The actual next helper's read/two-write/result-record source now works.

Escaping-node exception owners, broader iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain unfinished. The prior
unary loop-key escape witness is complete; no new escape witness was measured.

Linux inspection read 11 executable identities with 62 permission failures;
Windows Get-CimInstance returned 343 records. Neither found actual Claude, but
status remained uncertain and concurrent rules stayed in force. No browser/shared
implementation, push or history rewrite occurred.

Evidence: `../test-results/2026-09-24-helper-catch-sources/`, including original
sources, baseline simplified IR, later recovery probes, focused logs, selected
native output, final hashes, independent review and verified `SHA256SUMS`.
