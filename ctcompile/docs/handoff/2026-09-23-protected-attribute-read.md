# Protected attribute reads and zero/unit power indices, 2026-09-23

Resumed clean **cdd54063** and the retained conditional Boolean snapshot
`9c10be3d` from HANDOFF, current00 and iteration 85's final journal. No dirty
drafts or unmerged `codex-wip-20260907` remained at session start; unrelated
branches were preserved. Native, source-support, escape and read-only review
work continued through rate-limit interruptions with their drafts preserved.

## Landed

**bb25b945** extends the protected attribute helper proof to one exact
`hasAttribute` read feeding a terminal `setAttribute`. Preparation stays under
the original guard, in its original order, including the write-method lookup
before argument evaluation. The write retains its suppression region. Complete
DOM reproof establishes the initial methods, Element receiver and primitive
arguments before publication. A valid literal attribute name and String or
Boolean value then allow existing lowering to discharge the write suppression.
No invocation verifier, runtime carrier or browser implementation changed.

Raw checks cover ordinary, direct and captured helpers; exact read/write
ordering and call evidence; matching non-Element receiver and non-String name
refusal; extra, late and observed reads; invalid write names; and exact/incomplete
expansion and typed proof budgets. Invalid read names remain valid inputs to
`hasAttribute`, matching the public DOM API.

The original conditional source now executes unchanged, SHA-256
`9c10be3d2195cc8f67076aa662dc494ef69547ddae351f7d84016e0cf35f88e8`.
The prior literal-close witness remains. An additional source changes only the
close read key to an absent attribute to exercise a false Boolean write.
Both pre-close payload values, guard outcomes, natural exhaustion, repeated
calls, ordered DOM writes, separate documents and owning-session foreign
rejection are checked. All 233 historical source bodies and 86 metadata rows
remain unchanged.

**6e1a2eeb** admits induction-derived power indices only with a proved
invariant exponent zero or one. Existing bounded primitive conversion and power
transfer supply exact endpoints. Unit exponents preserve the index lattice;
zero exponents produce the singleton index one, including `0 ** 0`. Operand
order, complete reload/store census, actual-write replay and budgets remain.
General powers and unproved conversions still refuse. Raw CFG/SCF controls and
a focused source fixture cover the new proof; historical raw tests are retained.

## Focused validation

- `ctcompile_host_contract`: **1/1 PASS, 2.35 s test / 2.36 s total**.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.38 s test / 2.39 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.11 s**; 355 other lit cases excluded.
- Seven saved primitive throw sources: **112 native executions, 24 refusals,
  40 Node/VM observations PASS**, both providers, both optimization policies,
  explicit/deduced C++, GCC/Clang.
- Selected `normal`, `body-return-ordered`, `body-return-branch-number` and
  refusal controls: **48 native executions, 78 refusals, zero nonexecuted
  admissions and 24 Node/VM observations PASS**.
- Five historical throw oracles: **20 exact completion/effect observations
  per engine PASS** in Node and VM.
- Complete `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**;
  final `git diff --check` passes.
- Local source preservation: **233 historical iterator bodies, 86 metadata rows
  and 233 syntax checks preserved**. The seven saved throw sources passed
  **20 local Node observations** before devbox execution. Escape source evidence:
  **17 exact Node outputs/own-key sets and complete syntax PASS**; both full
  historical raw test files reconstruct unchanged after removing the added blocks.

All eight final code/test hashes match the devbox and local tree. All 80 generated
C++ files contain no Script/VM protocol or nullable-scalar fallback; compiled
harnesses pass the linked-symbol checks. Escape build, arrays and lit passed
on their first run. No production change followed the final host/source gate.

Explicit build targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`.
All builds/native/VM execution used the devbox under its shared lock. The first
native build caught mixed MLIR `TypedValue`/`Value` initializer-list deduction;
an explicit `SmallVector<Value>` fixed it. The corrected host gate and original
source preflight passed, then final raw controls received the final host gate.
No browser build repair or runtime-oracle change was needed.

Commands, logs, baselines, frozen hashes, generated C++, refusal IR and source
oracles are preserved in
`../../../../test-results/2026-09-23-protected-attribute-read/` under `native86/`,
`tests86/` and `escape86/`. The native command is
`native86/final-native-gate.sh`; the focused escape command is
`native86/escape-gate.sh`.

Full CTest/compiler lit, the complete custom-iterator fixture, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed or rewritten. Process identity
checks found no Claude executable, CLI or loop: initially
WSL-root Linux82 / Windows CIM350, and before docs Linux79 / Windows349,
without errors. The first nonroot helper could not overwrite old root-owned
evidence; a new path and WSL-root execution completed the census.
No browser or shared implementation changed. The external plan has no Git
repository; its current-work journal was updated alongside this handoff.

## Exact next boundary

Continue with the unchanged mixed throw/return source `43ab2b64`, SHA-256
`43ab2b64690c3c05d2f9a3ecfeb02156652b4500daa9b949cc87f7438bca0f05`.
Both optimization policies still refuse **DOM helper completion observes an
inactive value**. Prove the actual selected completion slots and preserve the
saved pre-close payload, effects and complete prefix/global/reentry reproof.
Do not redo completed conditional `9c10be3d` or replace the mixed source with a
simpler witness.

Mutable Number snapshots, throwing/primitive close methods, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain unfinished. This round
claims no full-Bootstrap admission or coverage gain.
