# Terminal mutable iterator closes and zero-base power indices, 2026-09-23

Resumed clean **2046e48b** and the retained Number sources identified by HANDOFF,
current00 and iteration 87's final journal. No dirty work or unmerged
`codex-wip-20260907` remained; unrelated branches were preserved. Parallel agents
provided escape changes, source fixtures and read-only review. Their drafts and
claims survived rate-limit interruptions; the parent reviewed, gated and committed.

## Landed

**74f07bcc** proves terminal mutable close. The earlier local-cell and
receiver census establishes private state. Each protected close with state must
be immediately followed by a no-inline, single-operation saved throw. Close gets
its current scalar arguments, while the throw retains its previous owning value.
The method's final state cannot have a later observer on that path. Nonterminal
state refuses; no general exceptional-state transport is claimed.

Protected helper expansion retains every scalar producer for complete typed DOM
reproof and leaves the actual attribute call in its exact zero-result invocation.
Only a fresh, unused data record and its ordinary literal-key stores disappear.
The complete use census rejects object self-observation, computed keys and
prototype setters. Field membership uses a set so the charged census stays linear.
No Invoke or throw verifier, runtime carrier, browser API or fallback changed.

Both original source bodies execute unchanged:

- `body-throw-number-snapshot`:
  `7c0874c96b8df0ae643eb0bac18d605c840a2ad2c96b1e5563521d272d1172b4`.
- `body-throw-branch-number-snapshot`:
  `8ee2040d23f6262eba3a35c09661939a12f5572852ec7d39e3d4d06314fc3843`.

A derived close writes the result of `count === 13`, while its saved exception
still contains Number 3. Checks also cover exhaustion returning Number 0,
conditional normal completion returning 1, both guard outcomes, repeated calls,
ordered writes, separate documents, owned-session foreign rejection and missing
contract/budget refusals. All 233 historical iterator bodies and 86 metadata rows
remain unchanged. Raw controls cover terminal state arguments, intervening
observers, missing throw, incomplete budgets, coercive arithmetic and record uses.

**8d789d96** proves zero-base power indices over bounded nonnegative exponents.
Zero exponent writes key one; positive exponents write key zero. Existing
conversion, complete reload/store census, endpoint enclosure, actual-write replay
and budgets remain. Negative exponents and general bases still refuse. All 27
historical power source bodies survive; source 27 and three raw zero/null bodies
are promoted unchanged. Thirteen source controls cover primitive bases, signed
zero, reversed exponents, stable reloads, retained aliases, overlapping/later
stores, negative exponents and attempted array growth.

## Focused validation

- Final `ctcompile_host_contract`: **1/1 PASS, 2.36 s test / 2.37 s total**.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.35 s test** in the initial
  combined run (**4.81 s total**) whose host check exposed the old expectation.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.11 s**, 355 other lit cases excluded.
- Eleven saved-throw sources: **176 native executions, 60 refusals and
  72 Node/VM observations PASS**. Both providers and
  optimization policies, explicit/deduced C++, GCC/Clang.
- Selected `normal`, `body-return-ordered`, `body-return-branch-number` and
  refusal controls: **48 native executions, 74 refusals, zero nonexecuted
  admissions and 24 Node/VM observations PASS**.
- Five historical throw oracles: **20 exact observations per engine PASS**.
- Complete `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Final `git diff --check` passes.
- Local source checks: **233 iterator bodies/syntax checks, 86 metadata rows,
  36 saved-throw Node observations and 40 escape outputs/own-key sets PASS**.

The first host run failed only because an old unobserved record-field input now
expands successfully. That same input is retained as a positive with typed
reproof; computed key, self-reference, prototype setter and coercion controls
were added. The corrected host run passed. Production is unchanged after the
final native execution gate. Nine final code/test hashes match local and devbox
files; 112 generated C++ files have no Script/VM protocol or nullable-scalar
fallback, and compiled harnesses pass the linked-symbol checks.

Explicit build targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All builds/native/VM execution used the devbox
under its shared lock. Full CTest/compiler lit, complete custom-iterator fixture,
broad corpus/native matrices, full Bootstrap, WPT/test262, browser suites,
Windows, sanitizers and local C++ builds were skipped. Nothing was pushed or rewritten.

Commands, logs, source baselines, hashes, generated C++, refusal IR and Node/VM
witnesses are in `../../../../test-results/2026-09-23-terminal-mutable-close/`,
under `native88/`, `tests88/` and `escape88/`. Native commands are
`native88/final-native-gate.sh` and `native88/final-prior-gate.sh`; collection
uses `native88/collect.sh`. The initial combined arrays/host command is
`native88/initial-host-escape.sh`; only the corrected host check needed a rerun.
The external plan has no Git repository; current00 was updated with this handoff.

WSL-root Linux83 / Windows CIM351 initially and Linux81 / Windows354 before
handoff publication had no matching Claude executable, CLI or loop and no errors.
Claude was confirmed stopped. No browser or shared implementation changed.

## Exact next boundary

The final five-source preflight admits both Number snapshots under both policies
and retains six refusals. Continue with original primitive close `5c738524`:
its return value is unobserved for a saved throw, but normalization still requires
one fresh own-field record. Prove that exact abrupt path while preserving normal
close validation, source effects and the original throw. Throwing close `fa88f9d6`
retains the same structural refusal; getter `68ea7208` lacks complete return/yield.
Do not redo the completed Number sources or replace the original refusal bodies.

Nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain. No full-Bootstrap admission or coverage gain
is claimed.
