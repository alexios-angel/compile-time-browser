# Mixed iterator completions and unit-base power indices, 2026-09-23

Resumed clean **e4e06268** and retained mixed throw/return `43ab2b64` from
HANDOFF, current00 and iteration 86's final journal. No dirty drafts or unmerged
`codex-wip-20260907` remained; unrelated branches were preserved. Native,
source-support and escape work continued through interrupted agents with their
drafts retained. Parent review and focused gates completed both changes.

## Landed

**e2dee4cc** extends effectful iterator completion proof beyond two exits.
The loop carries a bounded ordinal for its selected exit. Each exit retains its
exact tuple until its continuation selects the live payload, avoiding an early
join of inactive throw/return slots. Existing complete selected-arm use checks,
dominance, depth and work budgets remain. Effects, suppression, saved values and
frame exits keep their source order; complete DOM proof still runs before native
publication. No invocation or throw verifier, runtime carrier or browser API changed.

The original mixed source executes unchanged, SHA-256
`43ab2b64690c3c05d2f9a3ecfeb02156652b4500daa9b949cc87f7438bca0f05`.
The harness checks saved false/true on both throw and return, throw precedence
when both guards are true, natural exhaustion, repeated calls, ordered DOM writes,
separate documents and owning-session foreign rejection. Raw checks add three-way
and fallback selection, selected/external inactive-slot refusals and selector
observer refusal. The existing budget search now checks both three-way specimens
at early, middle and last incomplete budgets. All 233 historical iterator bodies
and 86 metadata rows remain unchanged.

**43392dca** admits bounded unit-base power indices such as `1 ** i` through
existing primitive conversion and power transfer. Every proved finite exponent
maps the unit base to index one. Operand order, invariant reload/store census,
actual-write replay and budgets remain. Nonunit bases and general powers refuse.
Two historical raw unit-base source bodies are promoted unchanged; all 17 previous
power source functions and CHECKs remain, with ten new controls. These cover
Number/String/Boolean unit bases, negative and crossing exponents, stable reloads,
later writes, overlap, retained aliases and zero-base refusal.

## Focused validation

- Final `ctcompile_host_contract`: **1/1 PASS, 2.39 s test / 2.40 s total**,
  including the new budget controls.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.30 s test / 2.31 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.11 s**, 355 other lit cases excluded.
- Eight saved-throw sources: **128 native executions, 24 refusals and
  56 Node/VM observations PASS**, both providers and optimization policies,
  explicit/deduced C++, GCC/Clang.
- Selected `normal`, `body-return-ordered`, `body-return-branch-number` and
  refusal controls: **48 native executions, 76 refusals, zero nonexecuted
  admissions and 24 Node/VM observations PASS**.
- Five historical throw oracles: **20 exact completion/effect observations
  per engine PASS** in Node and VM.
- Complete `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  final raw C++ changes received scoped formatting, and final diff checks pass.
- Local iterator preservation: **233 bodies and syntax checks, 86 metadata rows**;
  saved-source checks pass **28 exact Node observations**. Escape checks pass
  **27 exact Node outputs/own-key sets and complete syntax**. Both historical raw
  files reconstruct after restoring only the two promoted unchanged-body controls
  and removing appended checks; all 17 historical source bodies/CHECKs survive.

Seven final code/test hashes match the devbox and local tree. All 88 generated
C++ files contain no Script/VM protocol or nullable-scalar fallback; compiled
harnesses pass their linked-symbol checks. The initial diagnostic build captured
the three-way source tuple, and instrumentation was removed before the production
build. Production and initial host gates passed; subsequent raw controls received
focused host reruns. No production change followed the saved-source gate.

Explicit build targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All builds/native/VM execution used the devbox
under its shared lock. Full CTest/compiler lit, the complete custom-iterator
fixture, broad corpus/native matrices, full Bootstrap, WPT/test262, browser suites,
Windows, sanitizers and local C++ builds were skipped. Nothing was pushed or rewritten.

Commands, logs, source baselines, frozen hashes, generated C++, refusal IR and
Node/VM witnesses are in
`../../../../test-results/2026-09-23-mixed-completion/`, under `native87/`,
`tests87/` and `escape87/`. Native commands are `native87/final-native-gate.sh`
and `native87/final-prior-escape.sh`; escape commands are `native87/escape-gate.sh`.
The external plan has no Git repository; its current-work journal was updated
alongside this handoff.

Process identity checks found no Claude executable, CLI or loop: initially
WSL-root Linux80 / Windows CIM353, and before docs Linux85 / Windows352, without
errors. Initial sudo was unavailable; the successful WSL-root census completed
both process checks. No browser or shared implementation changed.

## Exact next boundary

Continue with original `body-throw-number-snapshot`, SHA-256
`7c0874c96b8df0ae643eb0bac18d605c840a2ad2c96b1e5563521d272d1172b4`,
then conditional Number snapshot `8ee2040d`. Both policies still refuse
**DOM iterator protected close needs a mutable-state proof**, at the explicit
`protectedCloses` / `stateInitials` guard in `HostContract/DOMCustomIteration.cpp`.
The first source saves Number 3 before its close method adds ten to captured
`count`; preserve that payload while proving close-state transport through
suppression, frame joins and complete DOM/prefix/global/reentry reproof.
Do not redo completed mixed `43ab2b64` or replace the retained source.

The final five-source preflight preserves ten refusals: both Number snapshots
need mutable-state proof; throwing and primitive close methods still need the
fresh own-field record contract; the close getter lacks a complete return/yield.
Multiple protected regions, implicit cleanup, nested custom iterators, unguarded
Bootstrap defaults, the application driver and full native Bootstrap remain.
No full-Bootstrap admission or coverage gain is claimed.
