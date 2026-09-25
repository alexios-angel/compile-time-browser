# Branch-local throw payloads and unary Number Plus, 2026-09-23

Resumed clean **c46ef2e9** and the exact `b216ee20` normal-close source from
HANDOFF, Current native work and the journal. The old `codex-wip-20260907` is
already an ancestor; unrelated branches were preserved. Source fixtures, raw
proofs and escape review ran in parallel. Interruptions resumed saved work.

## Landed

**f2a0fae3** projects a helper result's sole immediate throw into its original
single-result tail branches. Each branch keeps its payload, reads and writes;
observed results and effectful suffixes retain the ordinary typed join proof.
The existing budget, complete DOM reproof and private publication checks remain.
Optional String throws inspect presence once and throw an owning `js_string` or
`js_null_t`. The target verifier accepts that exact Null carrier while retaining
homogeneous protected-throw/catch checks. Saved-body suppression is unchanged.
Independent native production review is complete and clean.

The original `b216ee20` and body-return `e79de406` now execute byte-identically.
Four variants execute the branch-local String and absent-attribute null paths.
The six cases also check Boolean suffix throws, normal exhaustion, both documents,
both providers, both compiler policies and GCC/Clang output. The saved `39355ceb`
body exception remains observable. Historical source/refusal constructions and
all eight previous normal-close oracle constructions remain unchanged. One raw
Null throw input moved from refusal to admission without changing its source.

**0c9850c2** admits one original Number literal's unary Plus through the existing
bitwise conversion proof, alongside the existing literal/Neg paths. Arithmetic,
property identity, table mutation and saved-child proofs remain separate. The
previous **f3fd445d** wide-String change's interrupted independent review finished
cleanly; the new Plus change was also reviewed by its author and parent.

The frozen escape source SHA-256 is
`160d41d5bd6e2cd1e3695131444d4568d478150008baca769843682e3eb4a33f`.
Baseline and final bytes match, both identifying program `763fa966d37fd6dd`.
Functions 109–114 change from six stored children to **three confined and three
stored**. Each child is made once, with zero unresolved or unchecked observations;
the final claims agree with the recorder. All 108 historical functions, 108 claim
lines and 120 RECORD lines remain. Six actual-source Node witnesses pass.
No browser/runtime implementation changed.

## Focused validation

- Normal closes: **96 native executions, 48 Node/VM observations**.
- Saved exception and selected controls: **16 native executions, 48 refusals,
  8 Node/VM observations**.
- **34 final source-policy checks**: 32 for six normal admissions, the saved
  exception and nine controls; two for the next getter boundary.
- **17 source syntax checks and 24 local Node observations**, plus the six
  escape witnesses and their source syntax check.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.23 s / 3.24 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.15 s, 356 excluded.
- `ctcompile_host_contract`: **1/1 PASS**, 2.57 s / 2.58 s total.
- `Target/Cpp/primitive-exceptions.mlir`: **1/1 PASS**, 2.93 s, 356 excluded;
  this case includes its existing GCC/Clang and ASan/UBSan checks.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  `git diff --check` passes.
- All **56 generated C++ variants** contain no Script namespace; the source
  fixture's standalone compile and linked-symbol checks pass.
- Final source/code hashes match the devbox. A final TableGen prose-only wrap
  was synchronized after the build; no executable behavior changed afterward.

Initial probes reached the later primitive-carrier admission refusal after the
DOM join passed. The admission and target Null checks were completed. One build
failed on a const MLIR operation handle, corrected before gating. Initial host
failures identified the historical Null expectation and two new refusal tests
missing a provider declaration. The first arrays run failed one new assertion:
unknown Plus is rejected as UnsupportedOperation before the loop proof, rather
than UnsupportedControlFlow. Inputs were preserved in all assertion corrections.
One build command consumed the remaining shell heredoc through SSH; only the
missing checks ran separately. A queued native run was interrupted before it
started. The completed native run and passing final gates were not replayed.

Explicit devbox targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Builds/tests held the shared build lock; Git writes
held the Git lock and staged explicit paths. There were no local C++ builds,
pushes, history rewrites or browser/shared implementation edits.

Initial availability checks inspected 17 readable Linux executables and 344
Windows records; final checks inspected 16 and 346. Both had 60 Linux access
errors and zero actual Claude matches. Status remained uncertain, so concurrent
area rules applied.

Skipped: full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
broader sanitizer suites. These are focused results, not full-Bootstrap coverage.
Evidence with checksums is in
`../test-results/2026-09-23-branch-throw-payloads-unary-plus/` relative to the repo.
Working checkpoints are `/tmp/ctcompile-native132`, `/tmp/ctcompile-tests132`,
`/tmp/ctcompile-raw132` and `/tmp/ctcompile-escape132`.

## Next boundary

`normal-selector-branch-throw-read-getter-close`, SHA-256
`3b17dc574eafb195042252f444cfde6e26e63b7b47025b06d8071bd52009ab89`,
retains the original cleanup body but exposes it as `get return()`. Both policies
refuse **DOM iterator getter requires a terminal suppressed throw**. A normal
close must preserve the getter's observable exception, optional String/Boolean
payloads and branch/read/write/suffix order.

Mutable and mixed-suppression cleanup, broader control flow, nested custom
iterators, unguarded Bootstrap defaults, the application driver and full native
Bootstrap remain. Escape still excludes general computed fractional/nonfinite
origins, loaded-value and nested unary Plus chains, unsafe String exponents,
Strings over 32 bytes, general powers and legacy SCF retention. The VM
fractional-index discrepancy remains separate.
