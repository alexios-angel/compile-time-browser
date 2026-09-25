# Mutable throwing iterator closes and negative varying powers, 2026-09-23

Resumed clean **011b60ec** and the retained mutable throwing closes `cd9eb979`
and `83b34eed` from HANDOFF, current00 and iteration 92's final journal.
No dirty drafts or unmerged `codex-wip-20260907` remained. Unrelated branches
were preserved. Parallel agents prepared the escape proof and source regressions;
the parent completed source promotion after that agent's rate-limit interruption.

## Landed

**f94cc18d** moves the existing terminal literal close-throw normalization
before mutable-state body validation. The private candidate can now validate
its frame and scalarize its captured cells or receiver fields. All payload
producers and preceding effects remain. Original and post-completion
holder/method confinement, unique closure and symbolic-use checks, immediate
saved-throw completion and every surviving call's suppression remain mandatory.
Complete helper and typed DOM proofs still run before publication.

The retained source hashes are:

- Mutable: `cd9eb97969115a36c6dcf68dc9f7328b6b4249a86ec3916e5a3611c9ceecd07d`.
- Conditional mutable: `83b34eed7c61e54f9370c4df6f4fbe7f6918f0cb8c42d96fc9c17833263de5f2`.
- Conditional getter: `2b72726428ee0a299e573c43698894a3aa1c99f5764cfd145c8481ef1f2d1ac5`.

All three now execute unchanged. The additional `7aced404` witness writes
whether close sees count 13 while the saved exception remains Number 3.
Checks cover stop/no-stop, exhaustion, reentry and ordered DOM writes.
Ordinary mutable break/return closes, nonliteral close throws and invalid
attribute names retain refusal controls. Generated code uses ordinary owning
values and public DOM calls.

**19c4e507** permits bounded negative varying exponents when the proved
signed-unit base lattice excludes zero. Nonzero unit powers retain the output
lattice `{-1, 1}` (or singleton 1), allowing an invariant reload from the unwritten
middle slot. Containing congruence, whole-key refinement, complete reload/store
census and actual-write replay remain. General powers and zero bases with
negative exponents still refuse.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.47 s test / 2.48 s total**.
- Four selected saved-throw sources: **64 native executions, 104 refusals and
  24 Node/VM observations PASS**, across both providers and policies,
  explicit/deduced C++, GCC and Clang.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS,
  2.32 s test / 2.33 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`: **1/1 PASS,
  0.13 s**, with 355 other lit cases excluded.
- Full `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
- All 233 historical iterator source bodies and 86 metadata rows are preserved;
  local syntax checks and 12 selected Node observations pass.
- All 89 historical power source bodies/CHECKs and raw witnesses are unchanged;
  101 exact Node outputs and own-key sets, syntax and scoped formatting pass.
- All **seven final code/test hashes match the devbox**. All **32 generated
  C++ files** contain no Script/VM protocol or nullable-scalar fallback.
  The compiled harnesses pass linked-symbol checks.

Two initial host runs failed only four new raw fixture expectations: an untyped
arithmetic-only protected helper lacks the independent inert-body proof needed
for expansion. The corrected tests assert successful structural state transport
and retain that later refusal. The real source cases contain the supported typed
attribute leaf and pass complete native lowering. No production change was made
for this synthetic expectation.

The first escape CTest failed ten assertions across two new reload witnesses;
the exact lit case also failed new source 94's confinement expectation, with
zero oracle soundness violations. Preserving the nonzero-unit output congruence
fixed both without changing any old or new source/expected result. The final
focused rerun passes.

The read-only native reviewer confirmed the original and final observer guards
remain and found no stale ThrowOp identity before a rate limit interrupted its
final pass; this was a partial review. The parent traced the caller's disposable
clone through complete helper and DOM reproof. The escape agent separately
confirmed that containing congruence remains valid when range refinement is used.

Explicit build targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All devbox builds, tests and reads held the shared
build lock. Commands and evidence are under
`../../../../test-results/2026-09-23-mutable-throwing-close/` in `native93/`,
`tests93/` and `escape93/`.

Skipped: full CTest/compiler lit, complete custom-iterator fixture, unaffected
native-source replays, broad corpus/matrices, full Bootstrap, browser/WPT/test262,
Windows, sanitizers and local C++ builds. No push or history rewrite.
No browser/shared implementation files changed. Initial WSL-root Linux 80 and
Windows CIM 353 process identities showed no Claude executables, CLI hosts or
loops and no errors; a later check covered Linux 86 and Windows 353 with the same
result. Final publication checks covered Linux 82 and Windows 353, also clean.
The unsuccessful initial sudo attempt was superseded by WSL-root checks.

## Next boundary

The retained nonliteral mutable close `822a327b` writes the attribute, then
throws current count 13; the original saved body exception must remain Number 3.
It still refuses **DOM iterator method must return one fresh own-field record**
in both policies. Prove the ignored scalar throw without erasing its producers
or weakening ordinary-close and escape checks. Terminal literals are the current
ceiling. Nonterminal exceptional state, multiple protected regions, implicit
cleanup, nested custom iterators, unguarded Bootstrap defaults, the application
driver and full native Bootstrap remain unfinished. General powers remain refused.
