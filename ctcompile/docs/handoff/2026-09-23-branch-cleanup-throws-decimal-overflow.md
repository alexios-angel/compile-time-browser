# Branch-local cleanup throws and decimal overflow Strings, 2026-09-23

Resumed clean **b7cc6822** and retained source `65c65b0b` from HANDOFF,
Current native work and the previous detail/journal. There was no dirty draft;
`codex-wip-20260907` was already an ancestor and unrelated branches were preserved.
Source, raw analysis and escape work ran in parallel. Repeated user interruptions
and agent rate limits resumed saved checkpoints without replaying passed gates.

## Native change

**64c24085** makes the completion normalizer join the payload of one exact terminal throw
through its existing structured continuation proof, then emits the throw. Each
path keeps its original effects: suffix work belongs only to paths that reach it.
The custom iterator proof still requires a unique private close method, exact
unused-result suppression and its complete surviving-call/holder census before
replacing that throw with an ignored result. Ordinary throwing helpers remain refused.

The cleanup scanner now checks forwarded guard results with its complete use
census. Completed reads enter that census after each guarded write, so a later
constant write does not inherit an obsolete pending read. The ordered clone,
selector validation, DOM/Style reproof and work budgets remain. No browser or
runtime implementation changed, and there is no boxed or Script fallback.

The retained source is unchanged. Four cases cover the original shape, an actual
else throw with a different cleanup payload, its getter variant and a then-arm
throw. Executable assertions require the remaining cleanup suffix to be absent
and the original body exception to win. The pre-existing textual checks describe
a single linear suffix; they remain active for historical cases. The new joined
paths use executable path/order checks. Two early fixture attempts stopped at
those textual layout assumptions before native binaries ran; only the failed
gate was resumed after correcting the new-case selection.

Three new raw admissions and one observer refusal reuse the existing trace,
typed proof and incomplete-budget checks. All 28 raw MLIR literals, 125 original
named constructions and 286 historical invalid constructions remain. The new
snapshot assertion permits an explicitly unused guard payload. Source preservation
retains 233 general sources, 86 metadata rows, 143 saved cases and their oracle
constructions, and 331 remaining refusal bodies. Only the exact retained refusal
was promoted.

## Escape change

**274c03fd** lets the existing bounded String grammar provide decimal overflow
infinity to public Core bitwise conversion. Arithmetic still rejects nonfinite
values before integer conversion. NaN remains refused because Core's radix parser
can return NaN for a finite JavaScript radix value outside uint64. The 32-byte
source bound, checked exponent magnitude, finite-magnitude limit, original property
spelling, computed provenance, mutation and receiver-reload proofs remain.

Baseline `a82de37e760ad6da` classified all six child sites as stored. The unchanged
bodies in final `470dce10c09df0b7`, functions 91–96, measure three confined and
three stored. Each child is observed once with zero unresolved or unchecked
instances. Saved-child, table-mutation and original-property controls remain stored.
All 90 historical source bodies, 90 claim lines, six baseline bodies and 65 raw
constructions remain; six local Node identity/read/write/conversion witnesses pass.

The exact arrays CTest passed on its first attempt. The first selected lit run
passed claims and the oracle with zero violations but its new function-94 RECORD
assertion used site 4 instead of the observed site 5. Correcting that assertion
made the selected case pass; arrays were not replayed. An initial baseline command
used an unsupported CLI flag and failed before classification; it was corrected once.

## Focused validation

- Native: **64 executions, 84 refusals, 32 Node/VM observations**.
- Final source preflight: **26 policy checks**; **14 syntax checks** and **16 local
  Node observations**. Completed observations were retained through interruptions.
- `ctcompile_host_contract`: **1/1 PASS**, 2.58 s / 2.59 s total.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.08 s / 3.09 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.14 s, 356 excluded, after the RECORD correction described above.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
- `git diff --check`; all **seven final code/test hashes** match the devbox.
- All **32 generated C++ files** contain no Script namespace; the fixture's
  standalone and linked gates pass.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Only failed preflights/native assertions and the
failed selected lit case were resumed after relevant edits. The native production
probe was removed before the final compiler build and host test. Completed host,
arrays and selected lit gates were not repeated after source-only fixture changes.
Independent native production review is complete and clean; parent escape review
is complete. No completed independent escape review is claimed.

Initial identity process checks inspected 18 readable Linux executables and 346
Windows records; final checks inspected 21 and 344. Each saved check contains 60
Linux permission errors and zero actual Claude matches, so availability remained
uncertain and concurrent-area rules applied. There were no browser/shared
implementation writes, local C++ builds, pushes or history rewrites. Git writes
and devbox work used their shared locks and explicit paths.

Skipped: full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. These focused checks are not a full-suite or full-Bootstrap pass.
Evidence is in `../test-results/2026-09-23-branch-cleanup-throws-decimal-overflow/`
(relative to the repo), with SHA256SUMS. Working checkpoints are
`/tmp/ctcompile-native129`, `/tmp/ctcompile-tests129`, `/tmp/ctcompile-raw129` and
`/tmp/ctcompile-escape129`.

## Next boundary

`unsupported-selector-branch-throw-read`, SHA-256
`39355ceb5b9f0319cf0a76cc5e778f00a4fde9a3873d3bf611850955867a99ea`, uses
`throw anchor.getAttribute('data-closed')` in the cleanup else arm. Both policies
refuse **DOM protected helper needs an independent inert-body proof**. Preserve
the read, original String payload, both branch paths, suffix order and saved body
exception. An earlier exploratory `matches` payload (`46ca8e1a`) already admitted
under the unoptimized policy and was retained as evidence.

Broader cleanup/control flow, nested custom iterators, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain. Escape proofs
still exclude Infinity/NaN String token grammar, wider finite Strings, unsafe
exponents, Strings over 32 bytes and computed fractional/nonfinite origins.
General powers and legacy SCF retention remain. The known VM fractional-index
discrepancy is separate. These focused results measure no full-Bootstrap gain.
