# Iterator state lifetime and structured retention

Continued clean `ctcompile-v1` at `82b3c7c4` from the preceding handoff's
observed Try/Invoke boundary. The old WIP branch is already an ancestor; no
abandoned checkout edits or branch integration were needed.

## Native checkpoint

`edbb1eb8` checks the lifetime needed before discarding protocol state at Try
exit. The original getter `1a7fb166` and method `7acf503b` have no mutable private
scalar state. Their exact holder/record aliases are followed through If and
IndexSwitch results and observed Invoke saved-unwind positions. A live escape,
TryExit transport, unknown observer or live Iterable refuses. The anchor cell
is intentionally separate: it remains the original caught identity.

Only the exact custom-open/undefined comparison proves the eager fallback
inactive. Its operations remain in the candidate; a similar test of an unrelated
value supplies no authority. Real record observers require the canonical record,
so the earlier complete done-field and item-use proofs remain applicable.

Eight additional controls exercise late holder escape, unused and observed catch
transport, a saved-slot escape, private scalar state, the exact inactive fallback,
a lookalike guard and live Iterable. Existing effects, both close(false) flags,
three retained Invokes, 15 saved registers and whole-preparation rollback remain.
The original sources still report `observed completions require state transport`.
There is no new native source admission or full-Bootstrap claim.

A scratch Try/Invoke rewrite was parked before compilation. It exposed a separate
completion prerequisite: `DOMSource::normalizeCompletion` clones a Try opaquely,
while the final custom-iterator census still expects the completed direct/loop
shape. The scratch is unbuilt and unverified, not a tested implementation.

## Escape checkpoint

`d5b74486` adds a retention-local input census only when the legacy storage/load
census is incomplete. It charges the walk, checks current operand/result alias
lattices and supported Stored targets, allows If/While regions and refuses
nested allocation sites. It adds no new alias model and leaves the public legacy
census untouched. Existing exact contents, all-write acyclicity, return reachability
and transactional verdict publication remain necessary.

The byte-identical lifted `d1302d9a` witness previously had complete contents but
incomplete retention. Its overwritten child now changes from Stored to Confined.
Returning the child preserves it; selecting another array retains any child still
reachable on either path. Two existing structured arithmetic loop cases also
now discharge their cleared child. Three new controls cover replacing the holder
after the receiver snapshot, live publication and an overwritten transient cycle.
The latter has complete final contents but correctly refuses retention.

Every retention cutoff preserves prior verdicts; the existing harness also checks
exact completion budgets, unchanged legacy census/load lattices and source IR.
The raw-source oracle remains `502bee7b1676c84d`: four claimed/observed sites, two
confined, zero violations and three frame pops with unlimited budget. That raw
measurement is unchanged; the new gain is in structured retention evidence.

## Validation and limits

All C++ builds and devbox commands used `/tmp/ctbrowser-devbox-build.lock`.
Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-exception-recovery`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-claims`, `ctcompile-test-type-oracle` and
`ctcompile-test-escape-analysis-arrays`.

- Exact `ctcompile_exception_recovery`: baseline **1/1, 5.30 s**;
  final after the review tightening **1/1, 5.44 s**.
- Exact `ctcompile_host_contract`: **1/1, 2.75 s**.
- Exact `ctcompile_escape_analysis_arrays`: baseline **1/1, 4.70 s**;
  final **1/1, 4.70 s**.
- Build-tree lit filter
  `^ctcompile :: Analysis/Escape/escape-claims/cached-shortcut-length[.]test$`:
  **1/1, 0.10 s, 363 excluded**.
- Frozen raw escape oracle: **4 sites observed, 0 violations, 2 confined**.
- Native selection: `invocation-return`, `write-boolean-snapshot`,
  `helper-next-record`, plus all original/refusal controls. Both DOM providers,
  printing/optimization modes, GCC/Clang and Node/VM comparisons remain enabled.
  All 24 generated C++ files contain no Script/AOT/ctjs namespace; the harness
  binary checks pass. All six final implementation/test hashes match the devbox.
- `tools/format.sh --check`: **1126 C++, 159 Python, 114 web files PASS**;
  `git diff --check` passes. All eight DOMURI raw literals and the frozen
  structured function are unchanged; the two older SCF source bodies are intact.

The first native run rejected the original inactive eager-array read; exact
inactive-arm proof fixed that. A stale expectation for another inactive control
was corrected; the first scripted edit missed its formatted line break, caught
by the rerun. The source reviewer identified the unnecessary live Iterable
allowance; the parent removed it and added the focused control. Escape candidate
runs exposed five old incomplete-retention expectations; those were updated only
after the gain was measured. Initial failures remain in the evidence archive.

Agents worked independently on source tracing and escape inputs, were resumed
after interruption, then hit rate limits. The parent completed the escape tests
and static review and verified the native review fix. Independent final review
signoff is not claimed.

Skipped: full CTest/compiler lit, whole native fixtures, broad corpus/matrices,
full Bootstrap, WPT/test262, Windows/macOS, sanitizers and performance. No browser
or shared implementation, local C++ build, push or history rewrite occurred.
Availability check: 20 readable Linux identities, 61 permission failures, 354
Windows Get-CimInstance records and no Claude matches. Status remains uncertain;
concurrent ownership rules were used.

## Next boundary

Use the lifetime proof to implement next success/exhaustion/failure and both
unsuppressed close outcomes through the original Try/Invoke completions. Invoke
must remain call plus InvokeExit. Extract done/value only in its normal arm;
next failure leaves done true. Already-done close skips all return lookup/effects.
The normalized throwing helper returns the original thrown anchor: select the
original unwind projection, including its saved state, rather than throwing in
Invoke's unprotected normal arm. Keep Try single-result.

Then normalize the retained Try body's completion tuples and reprove the final
next/done census. Saved return and caught anchor-cell identity must remain exact.
Mutable scalar exceptional state and nested/repeated allocation origins remain
separate proofs. Raw helper CFG normalization, escaping node owners, broader
iterators, unguarded Bootstrap defaults, the native application driver and full
native Bootstrap are still unfinished.

Evidence: `../test-results/2026-09-25-iterator-state-lifetime` relative to the
monorepo root; focused logs, source preservation, scratch work and checksums.
