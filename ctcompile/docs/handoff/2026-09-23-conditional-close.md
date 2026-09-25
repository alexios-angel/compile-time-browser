# Conditional iterator closes and powers with two varying operands, 2026-09-23

Resumed clean **b73eba63** and conditional primitive close `1fe1a3e8`, followed
by throwing close `fd04cbc3`, from HANDOFF, current00 and iteration 91's final
journal. No dirty drafts or unmerged `codex-wip-20260907` remained; unrelated
branches were preserved. Parallel agents prepared the source regressions and
escape proof, and independently reviewed the native change. The parent completed
source validation after that agent's rate-limit interruption.

## Landed

**7b28e708** preserves Boolean loop-result facts separately for each selected
effectful completion arm. Every normalized false condition contributes its tuple
before inactive-slot padding or selector replacement. A nonliteral or disagreement
makes that slot unknown permanently. Only the selected continuation receives
fresh Boolean constants; the loop's original producers remain. The two-arm and
multiple-arm paths use the same refinement. Existing source visitation,
dominance, work/depth budgets, saved payloads and complete DOM/frame reproof remain.

The existing normal-close guard still requires literal done=true and its exact
empty exhaustion arm before removing the unreachable close. Every surviving
primitive or throwing close retains suppression. New raw controls cover both
providers, primitive and throwing closes, and conflicting true/false done facts
on the same ordinary exit. Ordinary break/return result validation is unchanged.

The retained source hashes are:

- Primitive: `1fe1a3e898369f7a42ccbc4055ebcb9175e7ab18f38b840de30d3d1c0dac15b4`.
- Throwing: `fd04cbc355be37275ff36c8a0880f5df39945873f2c30966a36e56fc8bc0d658`.

Both execute saved true/false Boolean payloads, stop/no-stop outcomes, exhaustion,
reentry and ordered DOM writes. The source bodies are unchanged. Generated C++
uses ordinary owning values and public DOM calls, with no runtime carrier.

**0d1ce73b** extends the existing index proof to two varying power operands when
the base remains within the integer set `{-1, 0, 1}` and the exponent has bounded
nonnegative integer values. Exponent parity bounds the sign; zero-to-zero retains
one. Independent range bounds conservatively lose correlation, while existing
whole-key refinement checks reload gaps and actual-write replay records only
visited keys. Negative/fractional exponents, general bases, overlapping reloads
and later writes retain refusal checks. All 77 historical source bodies and
CHECKs remain; one existing raw SCF `i ** i` body now proves unchanged and retains
its unvisited child. Twelve source controls were appended.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.42 s test / 2.43 s total**,
  including the new conditional-close and conflicting-done controls.
- Two conditional sources: **32 native executions, 64 refusals and 16 Node/VM
  observations PASS**, with both providers/policies, explicit/deduced C++,
  GCC and Clang. This selected check also covers ordinary primitive/throwing
  break and return refusals.
- One affected mixed throw/return source: **16 native executions, 40 refusals
  and 16 Node/VM observations PASS**. It exercises the multiple-arm refinement
  path separately from the two conditional sources.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS,
  2.32 s test / 2.33 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.12 s**, with 355 other lit cases excluded.
- Full `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Scoped formatting and `git diff --check` also pass.
- Local source checks preserve **233 historical iterator bodies and 86 metadata
  rows**, with 233 syntax checks and eight selected Node observations.
  Escape checks preserve **77 historical sources/CHECKs** and pass **89 exact
  Node outputs and own-key sets**, syntax and scoped formatting.
- All **seven final code/test hashes match the devbox**.
  The **16 conditional and eight mixed generated C++ files** have no Script/VM
  protocol or nullable-scalar fallback. Native harness linked-symbol checks pass.
- Independent read-only native review found no blockers. No production edits
  followed the passing gates.

The initial compile rejected implicit aggregate construction of LLVM DenseMap;
an explicit member initializer corrected it before the passing host gate.
The source agent was interrupted after promoting its prepared candidate; the
parent's baseline assertion detected those changed bytes, verified they exactly
matched the candidate, and continued without overwriting them. No source or
runtime workaround was used.

Explicit build targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All builds, tests and devbox reads held the shared
build lock. Commands and evidence are under
`../../../../test-results/2026-09-23-conditional-close/`, in `native92/`,
`tests92/` and `escape92/`; `tests90/` retains the reused first-preflight helper.
The native scripts select the host, conditional
sources, mixed source and exact escape checks. `verified.json` and
`devbox-hashes.txt` record final artifact checks.

Full CTest/compiler lit, the complete custom-iterator fixture, broad corpus/native
matrices, full Bootstrap, WPT/test262, browser suites, Windows, sanitizers and local
C++ builds were skipped. Nothing was pushed or rewritten. Historical measurements
remain historical.

Initial WSL-root /proc and Windows CIM checks inspected 82/351 processes;
the pre-handoff check inspected 86/351, with no matches or errors. Claude was
confirmed stopped. An unavailable sudo path was replaced by the successful
WSL-root check. No browser or shared implementation changed. The external plan
has no Git repository; current00 was updated separately.

## Exact next boundary

The saved Number state witnesses with a terminal throwing close still refuse
**DOM helper has no complete return or yield** under both policies:

- `mutable-number-throwing-close.js`:
  `cd9eb97969115a36c6dcf68dc9f7328b6b4249a86ec3916e5a3611c9ceecd07d`.
- `conditional-mutable-number-throwing-close.js`:
  `83b34eed7c61e54f9370c4df6f4fbe7f6918f0cb8c42d96fc9c17833263de5f2`.

Their complete sources live in `tests92/next/`, with diagnostics in
`native92/boundaries/` and `native92/next-gate.log`.
Continue with the first: carry the current private state into the close,
preserve its preceding effects and the saved Number, and prove the terminal
close throw before scalarizing the mutable method body.

The conditional getter source `2b727264` admits under both policies after this
change, but only compilation was checked; no execution pass is claimed for it.
Nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver and
full native Bootstrap remain unfinished. No new full-Bootstrap admission or
coverage measurement is claimed.
