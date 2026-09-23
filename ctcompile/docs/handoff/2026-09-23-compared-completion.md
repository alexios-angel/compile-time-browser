# Compared iterator exits and additive offsets, 2026-09-23

Resumed clean **51f90cf6** and conditional Boolean snapshot `9c10be3d` from
HANDOFF, current00 and iteration 84's final journal. Its predecessor had already
completed original `f1b3f6b8` and Boolean snapshot `92e23bbe`; those were retained.
No dirty drafts or unmerged `codex-wip-20260907` remained. Other branches were
preserved. Parallel escape, source-support and read-only review agents
contributed; parent retained and completed drafts through interruptions.

## Landed

**ba5e3163** extends the existing loop-exit projection to exact `arith.cmpi`
equality/inequality and `scf.if`. A single-use comparison selects one constant
integer tag, with either operand order. A preceding comparison constant must
have no other use; earlier constants must already be mapped and dominate the
comparison. Only the exact selector's use is discharged. The existing complete
selected-arm census still rejects inactive payload observations, including
nested and post-dispatch uses. Effects stay in their original selected arm.
No new IR operation, runtime carrier or exception fallback was added.

Raw checks cover equality, inequality, reversed operands, earlier constants and
first exhaustion. Existing hostile payload cases run through switch and
comparison forms. Wrong tags, ordering predicates and externally observed tags,
constants or comparisons refuse. Finite completion thresholds and sampled
incomplete budgets are checked. Normalization publishes no partial DOM evidence.

A new witness derives from the historical conditional source by changing only
its close value to literal `'yes'`. Its SHA-256 is
`6f5b574c5fd95179ba885dbf92edd99f957e92728208272a3457b555b56d8d86`.
It checks false/true saved pre-close payloads, natural exhaustion, repeated calls,
ordered DOM writes, separate documents and owning-session foreign rejection.
It calls public DOM/Core with ordinary ownership. The historical conditional and
mixed sources remain unchanged in the refusal inventory; no admission of either
is claimed. All 233 historical source bodies and 86 positive metadata rows remain.

**c30db6aa** removes a redundant Number-only guard before the existing additive
transfer. Bounded Boolean/null conversion now supplies invariant offsets;
`boundedNumberSum` still excludes String concatenation and unproved conversion.
Endpoint bounds, complete reload/store census, actual-write replay and budgets
remain unchanged. CFG/SCF checks cover zero and unit offsets, either operand
order, primitive reloads in unvisited gaps, overlapping/later stores and invalid
conversions. All 22 historical offset source bodies and CHECKs are unchanged;
eight cases bring that fixture to 30 functions.

## Focused validation

- `ctcompile_host_contract`: **1/1 PASS, 2.53 s test / 2.54 s total**.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.22 s test / 2.23 s total**.
- Exact `Analysis/Escape/escape-claims/offset-index-overwrite.test`:
  **1/1 PASS, 0.11 s**; 354 other lit cases excluded.
- Five saved primitive throw sources: **80 native executions, 12 refusals,
  24 Node/VM observations PASS**. Both providers, both optimization policies,
  explicit/deduced C++, GCC/Clang. Includes the independent conditional witness.
- Existing `normal`, `body-return-ordered`, `body-return-branch-number` and
  selected refusal controls: **48 native executions, 80 refusals,
  zero nonexecuted admissions, 24 Node/VM observations PASS**.
- Five historical throw oracles: **20 exact completion/effect observations
  per engine PASS** in Node and VM.
- Complete `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Final `git diff --check` passes.
- Local escape evidence: **30 functions syntax checked, nine exact Node outputs
  PASS**; all 22 previous bodies and CHECKs preserved. Iterator preservation:
  **233 source syntax checks, 233 bodies and 86 metadata rows preserved**.

Seven final code/test hashes match the devbox. All 64 generated C++ files contain
no Script/VM protocol or nullable-scalar fallback; compiled harnesses pass the
linked-symbol checks. The diagnostic build first located the inactive-value
refusal at the post-loop comparison. The first host gate then found only four
assertions assuming fixed arm order for inequality; correcting the expected
selected payload made it pass. Final comparison dominance and misuse/budget
checks were added, then the final host and source gate passed. Escape build,
arrays and lit passed on their first run. A local preservation invocation first
omitted `PYTHONPATH`; its corrected invocation passed. No browser repair was needed.

Explicit build targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`.
All builds/native/VM execution used `tools/remote-build.sh` and the devbox lock.
The native gate is `native85/final-native-gate.sh`; exact source drivers live in
`tests85/`, and `native85/escape-gate.sh` records the focused escape commands.
Frozen baselines, commands, logs, final hashes, generated C++, refusal IR and
oracle evidence are preserved in
`../../../../test-results/2026-09-23-compared-completion/` under `native85/`,
`tests85/` and `escape85/`.

Full CTest/compiler lit, the complete custom-iterator fixture, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed or rewritten. Process identity
checks found no Claude executable, CLI or loop: initially WSL-root Linux86 /
Windows CIM357, and before docs Linux86 / Windows354, without errors. No browser
or shared implementation changed. The external plan has no Git repository;
its current-work journal was updated alongside this committed handoff.

## Exact next boundary

Final two-source preflight after the escape build preserves both source hashes
and checks both optimization policies:

| Source | SHA-256 prefix | Exact native DOM source refusal |
| --- | --- | --- |
| `body-throw-branch-boolean-snapshot` | `9c10be3d` | DOM protected helper needs an independent inert-body proof |
| `body-throw-or-return-snapshot` | `43ab2b64` | DOM helper completion observes an inactive value |

Start with unchanged conditional source
`9c10be3d2195cc8f67076aa662dc494ef69547ddae351f7d84016e0cf35f88e8`.
Its return method evaluates `anchor.hasAttribute('data-visited')` before
`anchor.setAttribute('data-closed', ...)`. `DOMSource/Expansion.cpp::inlineCall`
currently admits one protected attribute leaf; it cannot carry this second call.
Prove the typed receiver, arguments, source order and no-source-throw behavior
before moving any read across suppression, or preserve all exceptional edges.
Retain complete prefix/global/reentry and DOM reproof. Do not replace the original
source with the simpler witness or relax the invocation verifier.

Mixed source `43ab2b64690c3c05d2f9a3ecfeb02156652b4500daa9b949cc87f7438bca0f05`
still needs a separate completion-slot proof. Mutable Number snapshots,
throwing/primitive close methods, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain unfinished. Prior measurements for the other
five throw refusals are retained in the previous handoff; they were not given a
new full census here.
