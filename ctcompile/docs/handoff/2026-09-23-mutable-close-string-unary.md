# Mutable cleanup exceptions and String unary inputs, 2026-09-23

Resumed clean **e51d742c** and exact source `8bab7f24` from HANDOFF, Current native
work, the preceding detail and journal. Recent compiler/browser/Shell commits and
unmerged branches were inspected; `codex-wip-20260907` is already an ancestor.
Independent source, raw-test and escape tasks used disjoint claims. Rate-limit
interruptions resumed saved checkpoints; the parent completed source/raw edits
and integration. No successful execution or build was replayed without a change.

## Landed

**01a14465** permits normal always-throwing iterator closes with private mutable
state. Existing scalarization supplies current state and preserves updates and
payload snapshots. Such a close returns its payload only for the immediate
owning rethrow; it does not read synthetic state fields from that scalar. Its
zero-result branch retains incoming state on exhaustion and has no continuation
on the throwing path. Complete confinement, observer, budget, frame, typed DOM
and private-publication proofs remain. Mixed protected/normal cleanup still
refuses. No runtime carrier or browser implementation changed.

Nine normal method/getter sources cover the original return, break, updated
payloads, effectful return evaluation and exhaustion. Two existing saved-body
controls retain their exceptions. Four raw method/getter cases check literal
and updated-state payloads, sole immediate throws, both providers and incomplete
budgets. All 348 historical source constructions, 153 saved cases, 143 general
refusals, 23 prior normal oracles and 47 raw MLIR blocks remain. Four historical
expectations were promoted with their inputs intact. Independent native review
is complete and clean.

**8094f34b** follows original String unary Number conversions through the existing
bounded bitwise proof. At least one unary edge is required before consulting the
String literal; existing grammar, byte-size, exponent and 64-operation bounds
remain. Property keys, arithmetic, mutation and retention keep separate proofs.
Three historical raw String controls now admit unchanged.

Frozen source SHA-256:
`73dc026c4709fdab1a3350b4dd7e6be967cf8fbbe9dc45093e8372a687e95552`.
Baseline/final bytes are identical, program `8b477d06a902c872`. Functions 127–132
change from six stored children to **three confined and three stored**; each is
made once with zero unresolved/unchecked observations. All 126 historical
functions/calls, 126 claim checks and 156 RECORD lines remain. Six actual-source
Node witnesses and their source syntax check pass. The previous unary-chain
implementation's independent review also completed; the parent reviewed this
four-line extension.

## Focused validation

- Main source driver: **176 native executions, 44 refusals, 98 Node/VM
  observations** (144 normal executions plus 32 saved-body executions).
- Older effectful-getter regression: **16 native executions, 10 Node/VM
  observations**. Total: **192 executions, 44 refusals, 108 observations**.
- Source-policy preflight: **32 checks**, 22 admissions and 10 refusals, with
  exact frozen inputs. Nine new normal sources also pass **45 local Node
  observations**. Preservation checks cover historical source and oracle tuples.
- `ctcompile_host_contract`: final **1/1 PASS**, 2.71 s / 2.72 s total, after
  adding raw controls. The initial production-only gate passed in 2.64 s.
- `ctcompile_escape_analysis_arrays`: final **1/1 PASS**, 3.30 s / 3.31 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.17 s, 356 excluded, using the generated build-tree lit configuration.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  `git diff --check` passes. All six final code/test hashes match the devbox.
- All **96 generated C++ files** contain no Script namespace. The source harness
  checks standalone compilation and linked symbols under GCC/Clang, explicit and
  deduced declarations, both native policies and both DOM providers.

The first arrays gate failed 30 assertions in five new Neg fixtures: the test
replacement helper changes only the first occurrence, leaving the second unary
operand positive over a negative String. Those keys correctly refused. The final
fixture replaces each operand explicitly and retains the asymmetric negative-key
case as a refusal control; production did not change. The parent also restricted
the new mutable-payload test classification so it cannot change an older getter's
Number 2 expectation, then ran that older case separately. Formatting was fixed.
An SSH child consumed the remaining commands in one shell heredoc after the
escape target build; the missing checks ran separately without rebuilding.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All devbox work held the build lock; Git writes
held the Git lock with explicit staged paths. Initial process checks read 16
Linux executable identities and 342 Windows records, with no actual Claude
matches and 60 Linux access errors: uncertain, so concurrent-area rules applied.
No browser/shared implementation edits, local C++ builds, pushes or history
rewrites occurred.

Skipped: full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. These are focused checks, not full-Bootstrap measurements.
Evidence is at `../test-results/2026-09-23-mutable-close-string-unary/`, relative
to the repo, with SHA256SUMS. Working checkpoints are `/tmp/ctcompile-native135`,
`/tmp/ctcompile-tests135`, `/tmp/ctcompile-tests135-regression` and
`/tmp/ctcompile-escape135`.

## Next boundary

Unchanged `mixed-body-throw-return-close`, SHA-256
`24815b879b5a4c24b49cdfc5f6c477c8a7f991a2f50775a296a0a770b487db9b`, refuses **DOM iterator primitive close requires saved-throw
suppression** under both policies. Its `stop` path throws a saved Boolean DOM
read; its `advance` path returns a saved Boolean read. Cleanup writes
`data-closed` from `hasAttribute('data-visited')`, then throws Number 2. Preserve
the body exception on the protected path and propagate Number 2 on the normal
path, including original return-expression/read/write and exhaustion ordering.
The complete source is retained in the source checkpoint and evidence archive.

The normal object-throwing getter still refuses an unproved owning primitive
payload. Nested custom iterators, unguarded Bootstrap defaults, the application
driver and full native Bootstrap remain. Escape excludes chains over 64,
general computed Number proofs, unsafe String exponents, Strings over 32 bytes,
general powers and legacy SCF retention. The VM fractional-index discrepancy
remains separate.
