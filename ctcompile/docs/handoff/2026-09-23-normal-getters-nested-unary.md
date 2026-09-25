# Normal throwing getters and nested Number unary operations, 2026-09-23

Resumed clean **d1cd1173** and exact getter source `3b17dc57` from HANDOFF,
Current native work and the journal. Recent browser/compiler commits and
unmerged branches were inspected; `codex-wip-20260907` is already an ancestor.
Source regressions, raw proof checks/review and escape work ran in parallel.
After interruptions and agent rate limits, saved checkpoints were continued;
the parent completed the escape implementation. Completed checks were not replayed.

## Landed

**275be0ce** removes the early suppression-only restriction on an always-throwing
`return` getter. Its existing private callable representation now reaches the
same stateless normal-close propagation proof as a throwing method. Terminal
throw, undefined setter, source order, unique target, capture/state confinement,
payload/frame validity, sole immediate rethrow and complete typed DOM reproof
remain required. Mixed or mutable normal closes still refuse. Saved-body
suppression and normal exhaustion are unchanged. Independent production review
is complete and clean, bound to the final implementation hash.

The unchanged `3b17dc57` source executes. Six getter cases cover normal break and
body return, Boolean/String/Null payloads, branch and suffix ordering, both
providers/documents/policies and GCC/Clang. Three saved-throw cases include the
original object-throwing getter whose exception is suppressed by a body throw.
Thirty historical expectations were promoted after source-policy measurement;
all 348 historical source constructions, 151 saved cases, 143 general refusals
and 14 prior normal oracles remain. The normal oracle helper is unchanged.
Two original raw getter constructions move into the existing Number-payload,
frame-exit, immediate-throw, both-provider and incomplete-budget checks; all
47 raw MLIR blocks remain unchanged.

**9b2622d7** follows at most two original Number Plus/Neg operations, tracks sign
parity and uses public Core ToUint32. Longer or nonliteral chains cannot borrow
this proof. Existing property spelling, arithmetic, mutation and alias checks
remain. The prior **0c9850c2** change's independent review completed cleanly;
the new nested-unary change was reviewed and finished by the parent.

Frozen source SHA-256:
`5e45482e189363ebebaddbead72c99c1bca47efca521b30eba416055d7a2597b`.
Baseline and final bytes match, both identifying program `e6ad95781f5f333d`.
Functions 115–120 change from six stored children to **three confined and three
stored**. Each is made once with zero unresolved/unchecked observations, and
claims agree with the recorder. All 114 historical functions/calls, 114 claim
checks and 132 RECORD lines remain. The original nested-Plus raw refusal input
is now an admission check; sign parity, nonfinite inputs and refusal boundaries
have focused raw controls. Six actual-source Node witnesses pass.

## Focused validation

- Normal getters: **96 native executions, 48 Node/VM observations**.
- Saved throws and controls: **48 native executions, 52 refusals,
  16 Node/VM observations**, four selected historical source admissions.
- Source preflight: **86 policy checks**, 72 admissions and 14 refusals.
- **43 source syntax checks, 24 local Node observations**, plus the six escape
  witnesses and their source syntax check.
- `ctcompile_host_contract`: **1/1 PASS**, 2.60 s / 2.61 s total.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.20 s / 3.21 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.15 s, 356 excluded, using the generated build-tree lit configuration.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  `git diff --check` passes. The first formatter found a new raw-test wrap;
  formatting was corrected. No passing execution gate needed replay.
- All **72 generated C++ files** contain no Script namespace. The fixture's
  standalone compilation and linked-symbol checks pass. All six final code/test
  hashes match the devbox. The raw header had a formatting-only change after
  its host gate; it was synchronized with the later escape build.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All devbox work held the shared build lock;
Git writes held the Git lock and staged explicit paths. No local C++ builds,
browser/runtime implementation edits, pushes or history rewrites occurred.
The initial process check inspected 14 readable Linux identities and 342 Windows
records, with zero actual Claude matches and 60 Linux access errors. Status was
uncertain, so concurrent-area rules applied.

Skipped: full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. These are focused checks, not full-Bootstrap measurements.
Checksum-verified evidence is in
`../test-results/2026-09-23-normal-getters-nested-unary/` relative to the repo.
Working checkpoints are `/tmp/ctcompile-native133`, `/tmp/ctcompile-tests133`,
`/tmp/ctcompile-raw133` and `/tmp/ctcompile-escape133`.

## Next boundary

Unchanged `return-getter-close`, SHA-256
`7224b890a1dafd5b6a531901a7276bc18bad988779e038c1536a0e81c06a560e`,
refuses **DOM entry branch has incompatible scalar alternatives** under both
policies. Its body returns Number 1, its getter sets `data-closed` then throws
Number 2, and its exhaustion result is Boolean. Preserve these original paths,
the cleanup exception and DOM ordering while resolving the completion join.

Mutable/mixed cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain. Escape still excludes
longer unary provenance, general computed fractional/nonfinite origins, unsafe
String exponents, Strings over 32 bytes, general powers and legacy SCF retention.
The VM fractional-index discrepancy remains separate.
