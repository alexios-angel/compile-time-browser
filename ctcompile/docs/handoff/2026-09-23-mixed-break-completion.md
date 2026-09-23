# Mixed break iterator completion, 2026-09-23 UTC

Resumed clean `df14ba50` and unchanged `mixed-body-throw-break-close`, SHA-256
`962dd3f8b420e5589371dc2c9f4d4fd27a9c5a7d54d4223dca03c687819b52a1`, from
HANDOFF, Current native work, the previous detailed handoff and sync journal.
`codex-wip-20260907` is already an ancestor. No interrupted branch or predecessor
edits were discarded. This iteration's source/raw checkpoints were resumed after
an interruption and repeated agent rate limits.

## Landed

`f65d48e5` proves the original mixed `throw`/`break` cleanup paths. CFG structuring
puts the protected close in one switch arm and yields literal integer tags from
the other arms. The following exact equality/inequality selects their normal
close. The existing bounded census now follows this relationship only for inert
single-yield arms, exact parent results and literal integer comparisons. Every
selected continuation must still prove a close. Effectful arms, wrong tags,
missing closes, observers and incomplete budgets retain refusals.

The fully checked normal close throws before a remaining source `hasAttribute`
read. The DOM body proof now permits property reads and calls after a fully
proved throwing branch, checking their ordinary operand, dominance, receiver,
argument and effect contracts. They remain after the original owning throw in
native output. Direct abrupt regions keep their strict continuation rule.

The unchanged method and getter (`2ed13bd0`) execute under both native policies.
Saved Boolean body exceptions, Number 2 cleanup exceptions, original DOM
reads/writes, preexisting attributes, exhaustion and return snapshots remain.
Three raw variants cover the actual three-arm topology, getter and reversed
`ne` operands, reusing both providers, payload/frame assertions, complete DOM
reproof and budget cuts. Four negative topologies cover wrong tags, missing
normal/protected closes and an effectful deferred-close arm. All 49 baseline raw
MLIR literal strings, 353 historical source tuples, 153 saved sources, 143 general
refusals and 34 normal oracles remain unchanged. Independent production review
completed cleanly and is bound to the committed production/raw hashes.

The parallel escape task was interrupted by repeated rate limits. Parent review
of `9b83e175` found no defect in the three-line Undefined unary proof or its
shared scalar/table/mask/count consumers. Original literal provenance, 64-step
limit, property/arithmetic separation and mutation controls remain. No escape
implementation changed, no escape tests ran, and no new precision gain is claimed.

## Focused validation

- Targeted source gate: **64 native executions, 36 expected refusals and 46
  Node/VM observations**. This selects the two new mixed-break sources, the
  existing mixed-return getter and an existing saved Boolean getter. It covers
  explicit/deduced output, both native policies, both DOM providers, GCC/Clang,
  standalone compilation and linked-symbol checks.
- Exact `ctcompile_host_contract`: **1/1 PASS**, 2.67 s, 2.68 s total.
- `tools/format.sh --check`: **1126 C++, 157 Python and 114 web files PASS**.
  `git diff --check` passes. The executed candidate and committed Python fixture
  have identical ASTs.
- All four final code/test hashes match the devbox. All **32 generated C++ files**
  contain no Script namespace.
- Source-policy preflights used ten frozen sources under both policies. Baseline:
  **4 admissions, 16 refusals**. Final admission preflight: **8 admissions,
  12 refusals**. The subsequent common-suffix fix passed the host regression and
  final native gate. Local preparation recorded **10 Node syntax checks and 21
  Node observations**; those completed checks were not repeated.

The initial debug command used a nonexistent `.pipeline.mlir` path; the completed
build was reused with the prepared source path. The first proof change exposed
the DOM suffix check rather than completing admission. The first host gate caught
an early return on a missing else region that prevented the existing common-close
suffix scan; that was corrected. New raw negative fixtures incorrectly expected
the disposable candidate to remain byte-identical, and one removed only the call
inside a required Invoke shape. The fixture now removes the complete Invoke and
refreshes the candidate fingerprint before checking absence of evidence. All
failures were corrected before the final passing gate and commit. Temporary dumps
were removed. Successful validation was not replayed.

Targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference` and
`ctcompile-test-host-contract`. Builds and tests ran on the devbox under its lock;
Git staging/commits used the Git lock and explicit paths. No local C++ build,
browser/runtime/shared implementation edit, push or history rewrite occurred.
Process identity checks inspected 73 Linux processes and 340 Windows records:
no actual Claude executable, CLI or loop matches, with no access errors. Claude
was confirmed stopped; broader browser authorization was not needed.

Skipped: full CTest/compiler lit, the complete iterator source fixture, unaffected
native replays, escape tests, broad corpus/matrices, full Bootstrap, browser
WPT/test262, Windows and sanitizers. These focused results are not full-Bootstrap
measurements.

Evidence: `../test-results/2026-09-23-mixed-break-completion/`, relative to the
repo, with SHA256SUMS. Working checkpoints: `/tmp/ctcompile-native137`,
`/tmp/ctcompile-tests137` and `/tmp/ctcompile-raw137`.

## Exact next boundary

Unchanged `mixed-body-throw-return-mutable-close`, SHA-256
`b705b1fbae55b8a2cfa1a8d201f566e650acf222e1c259c8dcce1528c0113df7`, refuses
**DOM iterator protected state requires an immediate saved throw** under both
policies. Its getter twin is `13d9387b`. Preserve captured `count` updates, the
saved Boolean body exception, the normal return replaced by cleanup Number 13,
ordered DOM reads/writes, snapshots and exhaustion. The protected close must not
expose or discard state that a later source observer can reach.

Normal object-throwing getters, nested custom iterators, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain. Escape still
excludes unproved globals, chains over 64, general computed Number proofs, unsafe
String exponents, Strings over 32 bytes, general powers and legacy SCF retention.
The VM fractional-index discrepancy remains separate. No full-Bootstrap gain is
claimed.
