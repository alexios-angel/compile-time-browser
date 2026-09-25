# Observed iterator state transport and captured storage

Continued clean `ctcompile-v1` at `b0e40459` from the recorded Try/Invoke
boundary. The old WIP branch was already an ancestor. An interruption during
this iteration left the escape candidate in four files; replacement agents and
the parent completed it without discarding work.

## Native checkpoint

`d1605217` implements the bounded observed-protocol rewrite. The original
getter `1a7fb166` and method `7acf503b` now reach
`DOM iterator observed state requires completion normalization`.

- Next retains a call/InvokeExit body. Only its normal continuation reads the
  proved own `done` and `value` fields; failure carries done true.
- Actual failure projects the new unwind block's payload and saved arguments.
  A normalized always-throwing close instead projects its returned throw payload
  with the pre-call saved values through the original unwind tuple.
- Both close sites retain their original result arity. Their exhausted arms
  return the original normal tuple with undefined, without lookup or invocation.
  Done true flows through both subsequent completion arms.
- All three Invokes retain 15 saved slots each. Try remains single-result;
  catch reads the original saved anchor cell before comparing identities.

The earlier lifetime census supplies the observed holder proof. Its exact
inactive eager branch can no longer be re-refused by the older direct-user
whitelist. Live Iterable, escaped/caught state and mutable scalar controls
still refuse. Both original source bodies and all existing controls remain.

The rewritten entry verifies, then explicitly refuses before completion
normalization. Whole preparation still restores the source and fingerprint.
There is **no new native JavaScript source admission** or full-Bootstrap claim.

## Escape checkpoint

`f0e10301` admits an existing UnknownOp capture witness to retention analysis
only when that exact nested operand has the Stored role. Complete current
region inputs and contents, acyclicity across all writes, and absence from
every returned graph still precede transactional verdict publication.

Ten new rows cover overwritten storage, retained/returned/yielded children,
later direct and nested publication, transient cycles, earlier unrelated
captures and nested allocation origins. Original witnesses survive every
refusal, and the existing harness checks all incomplete budgets and unchanged
source/census/load evidence. The public legacy census and alias model are
unchanged. The prior raw program's 2/4 confined measurement was not replayed.

## Focused validation

All devbox operations held `/tmp/ctbrowser-devbox-build.lock`; no local C++
build occurred. Explicit targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference` and `ctcompile-test-escape-analysis-arrays`.

- Exact `ctcompile_escape_analysis_arrays`: **1/1, 5.27 s**.
- Exact `ctcompile_exception_recovery`: final **1/1, 5.28 s**.
- Exact `ctcompile_host_contract`: **1/1, 2.75 s**.
- Source selection: `invocation-return`, `write-boolean-snapshot`,
  `helper-next-record`, with all original/refusal controls retained. Existing
  providers, printing/optimization modes, GCC/Clang and Node/VM comparisons
  remain enabled. All 24 generated C++ files have no Script/AOT/ctjs namespace;
  harness binary checks pass. This was not the whole native fixture.
- All seven final implementation/test hashes match the devbox. Eight original
  raw literals (five JavaScript, three MLIR), and both historical escape
  Induction witness files, are byte-identical to the starting commit.
- `tools/format.sh --check`: **1126 C++, 159 Python, 114 web files pass**;
  `git diff --check` passes.

The independent reviewer found a missing block-map charge when copying an
IRMapping; it was added. The first native gate exposed test assumptions about
the saved anchor CellGet, normal result versus completion tag, and done state
forwarded through a selection. A focused before/after IR dump confirmed the
actual correspondence. Tests now assert it; temporary dump code was removed.
The same gate exposed the redundant holder check described above. Final static
review includes these changes and is clean. The native test agent hit rate
limits; the parent finished its candidate and all gates.

Skipped: full CTest/compiler lit, whole native fixtures, broad corpus/matrix
runs, the raw escape oracle replay, full Bootstrap, WPT/test262, Windows/macOS,
sanitizers and performance. No browser/shared implementation changed, no push
or history rewrite occurred. Availability remained uncertain: 11 readable
Linux identities, 60 permission failures, 344 Windows Get-CimInstance records
and no Claude matches. Concurrent ownership rules applied.

## Next boundary

`DOMSource::normalizeCompletion` still clones Try opaquely. The eliminated
open's poison marker is outside Try and reaches saved invocation slots, so
the current opaque-region census correctly rejects it as inactive external
state. Prove the exact transport and continuation selections inside Try/Invoke
before discarding those positions; do not exempt arbitrary poison operands.

Then extend Custom.cpp's final next/done census, which currently expects a
direct call or loop test, to the protected normal-result projection. Retain
saved returns, caught-anchor cell identity and every source effect. Remove
the explicit refusal only after both obligations and complete DOM admission
pass. Mutable exceptional scalar state, nested allocation origins, raw helper
CFGs, escaping node owners, broader iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain unfinished.

Evidence: `../test-results/2026-09-25-observed-state-transport` relative to the
monorepo root: focused logs (including initial failures and before/after IR),
selection script, source hashes, preservation checks, final static review and
checksums.
