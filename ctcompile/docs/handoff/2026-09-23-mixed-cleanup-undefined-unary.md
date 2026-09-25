# Mixed iterator cleanup and Undefined unary Number inputs, 2026-09-23 UTC

Resumed clean `af864871` and the unchanged `mixed-body-throw-return-close`
source `24815b879b5a4c24b49cdfc5f6c477c8a7f991a2f50775a296a0a770b487db9b`
from HANDOFF, Current native work, the previous detailed handoff and sync journal.
The historical `codex-wip-20260907` branch is already an ancestor; no unmerged
interrupted branch or dirty predecessor work was discarded.

## Native change

`88dc57f2` keeps a throwing cleanup helper's original payload for each
caller's completion path. An exact protected Invoke discards the helper's return
edge after checking all observers, while a normal close immediately throws the
owning payload. Payload producers and DOM effects remain in source order. Both
fully checked nonreturning branch arms may use common primitive padding for
unreachable yields; ordinary live scalar joins keep their previous proof.
The existing exhaustion check also removes statically inactive throwing closes.

The original method and its getter twin now execute unchanged. The focused source
checks distinguish saved Boolean body exceptions from Number 2 cleanup exceptions,
including preexisting attributes, return-expression reads/writes, ordinary loop
completion and exhaustion. Historical normal, mutable and branching protected
cleanup sources are retained as focused regressions. All 348 historical source
constructions, 153 saved sources, 143 general refusals, 32 normal oracles and 47
raw MLIR blocks remain. New raw controls cover exact suppression, method/getter
calls, original and updated state payloads, observer rejection and incomplete
budgets. Mixed mutable raw normalization is not claimed as complete admission.
Independent native production/raw review is complete and clean, hash-bound to
the final files.

## Escape change

`9b83e175` converts original unary `+`/`-` Undefined inputs to Number NaN
through the existing public Core bitwise conversion. The explicit unary edge,
original-source identity and 64-operation bound remain. Direct Undefined inputs,
property spelling, arithmetic, unknown operands and mutations keep separate proofs.

The first six frozen sources read global `undefined`, which remains unproved;
they correctly remain Stored and are retained unchanged as controls. Three
additional sources declare a local uninitialized `var undefined`, providing the
original Undefined literal without widening global-load authority. These were
frozen before a measured run with the old proof, then run with the new proof.

Exact final source
`2cb5925d0edf3fdd3e6b31e5582a9db0d69dc4d6f69e20b4072a7586e0dc0eb2`
has identical baseline/final program identity `d072529396ae1d5d`. Nine child sites
change from all Stored to **six global-read Stored and three local Confined**.
Each was made once with zero unresolved or unchecked instances. All 132 historical
functions, the six global-source bodies and prior CHECK/RECORD directives remain.
The previous String unary change's independent review completed cleanly; this
three-line change was parent-reviewed.

## Focused validation

- Final targeted source gate: **96 native executions, 68 refusals and 58 Node/VM
  observations**. This covers two new mixed-return sources, one existing normal
  mutable getter and three existing protected cleanup sources. All 48 generated
  C++ files contain no Script namespace; the harness checks standalone GCC/Clang
  compilation and linked symbols, explicit/deduced declarations, both native
  policies and both DOM providers.
- `ctcompile_host_contract`: **1/1 PASS**, 2.68 s, 2.69 s total.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.37 s, 3.38 s total.
- Selected `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.16 s, 356 excluded, through the generated build-tree lit configuration.
- `tools/format.sh --check`: **1126 C++, 157 Python and 114 web files PASS**.
  Later edits touched only the lit source data; all formatter-scanned final
  code files remain unchanged. `git diff --check` passes. The executed and
  formatted native fixture have identical Python ASTs.
- All eight final code/test hashes match the devbox. The independent native
  review's four hashes match the committed files.
- Two 24-result exploratory native source-policy preflights tracked admission
  and refusal boundaries. Local checks recorded 12 native-source syntax passes,
  47 Node observations for the prepared sources, six original global-Undefined
  Node witnesses, and three added local-Undefined Node witnesses plus syntax.
  The two final new native sources are included in the final execution gate.

The first preflight exposed the both-throw scalar join omission. An early host
run caught stale helper-payload assertions; one new early-exit proof also omitted
its explicit DOM provider. A saved source regression caught a retained `if (true)`
from exhausted throwing cleanup; the existing pruning proof was extended rather
than weakening the source expectation. The first formatter required Python
formatting. The first escape lit failed its three new confinement expectations:
the source operands were global reads, unlike the raw literal operands. Their
expectations now retain Stored; three local literal witnesses provide the measured
precision gain. The successful raw-array test was not repeated: its final proof
and test bytes are unchanged. Repeated agent rate limits were resumed from checkpoints; the parent
finished source/raw/escape edits. These failures were corrected before committing.

Targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
`ctcompile-test-host-contract`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`. All builds/checks ran
on the devbox under its lock. Git staging/commits used the Git lock and explicit
paths. No local C++ build, browser/runtime/shared implementation edit, push or
history rewrite occurred. Process identity checks read 14 Linux identities and
337 Windows records with no actual Claude matches; 61 Linux access errors made
status uncertain, so concurrent-area rules applied throughout.

Skipped: full CTest/compiler lit, the complete iterator source fixture, unaffected
native replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows
and sanitizers. These are focused checks, not full-Bootstrap measurements.

Evidence: `../test-results/2026-09-23-mixed-cleanup-undefined-unary/` relative to
the repo, with SHA256SUMS. Working checkpoints: `/tmp/ctcompile-native136`,
`/tmp/ctcompile-tests136`, `/tmp/ctcompile-raw136`, `/tmp/ctcompile-escape136`.

## Exact next boundary

Unchanged `mixed-body-throw-break-close`, SHA-256
`962dd3f8b420e5589371dc2c9f4d4fd27a9c5a7d54d4223dca03c687819b52a1`, refuses
**DOM iterator completion must close every traversal exit** under both policies.
Keep its protected Boolean body exception, normal `break` cleanup Number 2,
original DOM reads/writes, snapshots and exhaustion ordering. Its getter twin
has the same boundary. Mixed mutable return/method/getter variants instead refuse
**DOM iterator protected state requires an immediate saved throw**; their exact
sources remain in the fixture/evidence.

Normal object-throwing getters, nested custom iterators, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain. Escape still
excludes unary chains over 64, general computed Number proofs, unsafe String
exponents, Strings over 32 bytes, general powers and legacy SCF retention. The VM
fractional-index discrepancy remains separate. No full-Bootstrap gain is claimed.
