# Primitive iterator close results and negative-unit power indices, 2026-09-23

Resumed clean **563a6a4a** and original primitive-close source `5c738524`,
identified in HANDOFF, current00 and iteration 88's final journal. No dirty
work or unmerged `codex-wip-20260907` remained; unrelated branches were preserved.
Parallel agents supplied escape changes, source candidates and read-only review.
The parent preserved and completed candidates after agent rate-limit failures,
then reviewed, gated and committed each concern separately.

## Landed

**9386caf3** admits ignored literal primitive return results only for
proved suppressed iterator closes. The custom iterator's done field has a
truth-only observer census. Its existing source truth test now supplies Boolean
state to each arm without replacing that field's value or removing its producer.
After completion selects an exit, a generated normal-close guard with constant
true done state and an empty then arm can be removed. Every surviving primitive
close must retain exact unused-result suppression. Normal break/return calls
still refuse primitive results; next still requires its complete fresh record.
Private state is passed into the protected method, but no synthetic state fields
are stored on its primitive result. Terminal state and complete DOM reproof
requirements are unchanged.

Protected attribute helper expansion accepts a literal primitive return in place
of its discarded fresh result record. It retains all scalar producers, member
lookups, the optional attribute read and the protected write in source order.
Complete typed DOM proof still excludes invalid receivers, coercion and names.
The actual output uses public DOM and ordinary owned primitive values;
no Invoke verifier, runtime carrier, browser API or fallback changed.

Original `body-throw-close-primitive` executes unchanged:
`5c7385244ee513bdf1da175b172e0f197ab7f507e3814d85e2ea30479dc658b4`.
Its body throws Number 1; close writes `data-closed=yes` and returns Number 2.
The original throw remains Number 1, and exhaustion does not invoke close.
A derived unconditional Boolean snapshot, `ea1f3f50`, saves false before close
writes the attribute and returns 2. Existing checks cover repeated calls, two
documents, both providers, foreign-session rejection, exact DOM write order,
missing contracts and budget refusals. All 233 historical source bodies and
86 metadata rows survive unchanged.

**beee2449** proves `(-1) ** i` for bounded integer exponent lattices. The
existing scalar transfer supplies parity. Even strides and singleton exponents
produce a singleton; odd strides enclose both signs with stride two, including
an interior opposite sign when endpoint powers agree. Thus an invariant base
reload can remain in the unwritten gap. Full reload/store census, conversion,
endpoint enclosure, actual-write replay and budgets remain. Negative properties,
fractional exponents and array growth retain conservative refusals. All 40 old
source functions and CHECKs remain; 13 source controls and focused CFG/SCF
controls were appended. Local Node verifies 53 exact outputs and own-key sets.

## Focused validation

- Thirteen saved-throw sources: **208 native executions, 84 refusals, 80 Node/VM observations PASS**, across both providers/policies, explicit/deduced C++, GCC/Clang.
- Final exact `ctcompile_host_contract`: 1/1 PASS, 2.39 s test / 2.40 s total.
- Exact `ctcompile_escape_analysis_arrays`: 1/1 PASS, 2.25 s test in the initial combined 4.65 s run. The initial host check failed; the corrected host check above passed separately.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`: 1/1 PASS, 0.11 s, 355 other lit cases excluded.
- Selected prior `normal`, `body-return-ordered`, `body-return-branch-number` and retained refusals: 48 native executions, 74 refusals, zero nonexecuted admissions, 24 Node/VM observations PASS.
- Five historical throw-oracle sources: 20 exact completion/effect observations per engine PASS.
- Complete `tools/format.sh --check`: 1126 C++, 157 Python, 114 web files PASS. Final scoped Python formatting and `git diff --check` PASS.
- Local source checks: 233 iterator source bodies/syntax checks and 86 metadata rows preserved; 40 saved-throw Node observations and 53 escape outputs/own-key sets PASS. All 40 historical escape source bodies/CHECKs preserved.
- All nine final code/test hashes match the devbox. All 128 generated C++ files have no Script/VM protocol or nullable-scalar fallback; compiled harnesses also pass linked-symbol checks.

The initial host check exposed four new-test expectation failures: the raw
positive used two separate done tests rather than the admitted source shape,
its budget probe consequently refused, and the attribute-order check assumed
no harmless constant could separate the read and protected invocation. The raw
positive now uses the source's direct done branch; the order check skips only
constants. Final host passes. A first saved-source run reached the new Boolean
witness with an inherited conditional-case expectation (`true` instead of `yes`);
the local Node witness caught it, the fixture was corrected, and the final focused
saved-source gate was rerun. No full-suite pass is claimed from partial runs.

Explicit devbox build targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All build/native/VM execution held the shared devbox
lock. Full CTest/compiler lit, complete custom-iterator fixture, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and
local C++ builds were skipped. Nothing was pushed or rewritten.

Commands, logs, baselines, source hashes, generated C++, refusal IR and Node/VM
witnesses are under `../../../../test-results/2026-09-23-primitive-close/`, in
`native89/`, `tests89/` and `escape89/`. Final source selectors are
`native89/final-native-gate.sh` and `native89/final-prior-gate.sh`; collection is
`native89/collect.sh`. The external plan has no Git repository; current00 was
updated with these measurements.

WSL-root Linux83 / Windows CIM350 initially and Linux85 / Windows353 before
handoff preparation had no matching Claude executable, CLI or loop and no errors.
The final pre-publication check inspected Linux81 / Windows350 with no matches or errors.
Claude was confirmed stopped. No browser or shared implementation changed.

## Exact next boundary

Continue with original `body-throw-close-throws`, `fa88f9d6`: normalization still
requires one fresh own-field record. Prove the close's explicit throw under the
existing exact suppression while preserving the body's earlier exception and
all close effects. Do not replace this original with a simpler source.

Derived conditional primitive close `1fe1a3e8` is retained in the evidence and
still refuses normal-close suppression: its loop completion and done tuple slots
need correlation. It was not promoted or substituted for original `5c738524`.
Getter close `68ea7208` still lacks complete return/yield. Nonterminal exceptional
state, multiple protected regions, implicit cleanup, nested custom iterators,
unguarded Bootstrap defaults, the application driver and full native Bootstrap
remain. No full-Bootstrap admission or coverage gain is claimed.
