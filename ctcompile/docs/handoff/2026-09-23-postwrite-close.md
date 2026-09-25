# Post-write iterator close reads and singleton power bases, 2026-09-23

Resumed clean **00e7f1cb** and retained Boolean source `4def4ec7` from HANDOFF,
current00 and iteration 94's final journal. No dirty drafts or unmerged
`codex-wip-20260907` remained. Unrelated branches were preserved. Parallel agents
prepared escape analysis, source checks and independent reviews. The escape agent
was interrupted after freezing its files; the parent completed its gates and commit.

## Landed

**e7cbb817** admits one unused trailing `hasAttribute` read in an exact suppressed
attribute helper. Expansion places prefix producers before the original invocation,
the write inside it, and suffix producers after it at the same source guard.
Complete typed DOM reproof must establish that the write and moved reads cannot
throw a source exception or reenter JavaScript. The original unused-result
suppression, valid write-name check, primitive argument checks, private callable
census, shadow-frame validation and saved exception ownership remain unchanged.
Trailing method and result uses are confined. Ordinary throwing closes still refuse.

The original source
`4def4ec7b2265c7f0afc31e06ebd309c24fb906ab9d4b0b0fce0aa212530336b`
executes unchanged: the post-write close payload is suppressed while the saved
pre-close Boolean survives. Getter `4510c95b` and missing-attribute `5da2472c`
variants also execute. Checks cover exhaustion, reentry, two documents, ordered
writes and original payloads. Raw tests assert read/write order, direct and captured
calls, exact budgets, unused results, wrong receivers and coercing keys. A bad-name
String remains valid for `hasAttribute`; the protected write still requires a
valid name. Generated output retains the trailing public DOM call after the write.

Parallel **11d95228** recognizes proved singleton bases and routes them through
the existing invariant-base power proof. This adds no arithmetic transfer:
general powers remain conservative. Complete reload/store census and actual-write
replay preserve saved and unwritten children. All 110 historical source bodies,
CHECKs and raw witnesses are unchanged; twelve source controls were added.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.38 s test / 2.40 s total**.
- Three selected saved-throw sources: **48 native executions, 68 refusals,
  24 Node/VM observations PASS**, both providers and policies, explicit/deduced
  C++, GCC and Clang. Preflight measured six admissions and sixteen refusals.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS,
  2.34 s test / 2.35 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`: **1/1 PASS,
  0.13 s**, with 355 other lit cases excluded.
- Full `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
- All **seven final code/test hashes match the devbox**. All **24 generated
  C++ files** contain no Script/VM protocol or nullable-scalar fallback;
  the execution harness's linked-symbol checks pass.
- Source preservation: **233 historical iterator bodies, 86 metadata rows and
  27 existing saved cases unchanged**. Local checks pass 244 source syntax checks,
  twelve positive and sixteen refusal Node observations. Escape checks preserve
  all 110 old source bodies/CHECKs and cover 122 Node outputs and own-key sets.

Independent native and escape reviews found no blockers. The first build was
interrupted after four of six steps and produced no test result. The resumed
build and focused checks completed. No production correction followed the
successful gate; no interrupted run is counted as a pass.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All devbox operations held the shared build lock.
Commands, logs, witnesses and checksums:
`../../../../test-results/2026-09-23-postwrite-close/`.

Skipped: full CTest/compiler lit, complete custom-iterator fixture, unaffected
native-source replays, broad corpus/matrices, full Bootstrap, browser/WPT/test262,
Windows, sanitizers and local C++ builds. No push or history rewrite.
No browser/shared implementation files changed. Initial WSL-root Linux 83 and
Windows CIM 357 process checks found no Claude executable/CLI/loop identities or
errors; the prelanding Linux 83/Windows 355 check agreed.

## Next boundary

Retained `second-postwrite-effect` source
`442af76b4d34d947dcbd8f2f3950aa971fb231514766bdd8311f850d267af2ff`
still refuses **DOM protected helper needs an independent inert-body proof**
in both policies. The close writes `data-closed`, reads it and writes it again.
Preserve both writes, their order and the original saved Boolean while extending
the protected effect proof. Nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain unfinished.
