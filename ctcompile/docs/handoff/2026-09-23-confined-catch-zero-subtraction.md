# Confined node catches and zero subtraction, 2026-09-23 UTC

Resumed clean `bed8328f` from the previous HANDOFF, Current native work, detail139
and shared journal. Recent compiler/browser/Shell commits and unmerged branches
were inspected; `codex-wip-20260907` is already an ancestor. Source, escape and
review tasks were delegated. All three agents hit rate limits; the parent resumed
their frozen checkpoints. The reviewer later completed both independent reviews.

## Landed

`2427229f` adds `normalizeDOMCaughtThrow` to the existing private DOM preparation
path. Existing exception recovery supplies the original handler, thrown value
and complete state. Only an unconditional explicit throw with a flat,
independently nonthrowing protected body is consumed. Catch arguments retain the
original SSA values and operation order. The exclusive unreachable normal-result
poison and unused dispatch constants disappear. Complete DOM proof still checks
the catch and rejects escaping returns, storage and rethrows. No native borrowed
exception type, VM fallback or browser implementation is added.

The two frozen direct sources remain unchanged:

- Identity: `8bf8439b9097317499ce5170b8428d9b62d416a2f9fb9a137f3e660bbb878fa5`.
- Attribute read: `1900ee1c74c4e06bf3663bcfe2d0bd947609de39b90cf1016b609d37e1202418`.

`32c9bd81` proves the algebraic conversion rule
ToUint32(ToNumber(x) - 0) = ToUint32(x). The proof uses an independently exact
zero operand and existing non-BigInt conversion evidence. Ordinary replay keeps
only a result conversion snapshot; the loop shortcut applies only to a bitwise
consumer. Property keys, other arithmetic, mutation/reload checks, unary-depth
tracking and the shared work budget retain their existing requirements.
Seven historical subtract-zero inputs now admit unchanged; their original
property/arithmetic refusals remain. The raw controls cover nonzero subtraction
and table mutation. All 148 historical source function bodies and calls remain;
one additional subtract-zero function is appended as 149.

The original standalone source remains SHA-256
`ffcfb029f3e2a15ad11ba23c5983463ece0363b0c35b4269ad3f520b1dc363f5`, program
`822031e8512b11f1`, before and after. Its child at function 1/site 4 changes from
Stored to Confined; the independent key table also becomes confined. Two of four
total sites are now confined, with zero soundness violations, unresolved or
unchecked instances. The child is observed once. This is a witness measurement,
not corpus or Bootstrap coverage.

## Focused validation

- Eleven frozen native source preflights: two direct catches now admit; nine
  rethrow/iterator/uncaught-node inputs refuse. This preflight used one provider
  and the unoptimized policy. The source checkpoint also preserves 11 syntax
  checks and 22 Node observations.
- The dedicated native source gate completes **32 native executions, 20 refusals
  and 4 distinct Node/VM observations**, using both providers, optimization
  policies, explicit/deduced output and GCC/Clang. Each standalone client checks
  16 document lifetimes; no Script/AOT dependency is linked. All **16 generated
  C++ files** contain no Script namespace.
- **Native lit completion was interrupted by the user's continue message.**
  Its on-disk final conditional-refusal request could only be reached after all
  32 executions and the preceding 19 refusals succeeded. All generated sources,
  binaries and 20 request manifests were checked. Only the final refusal was
  repeated to close that checkpoint. The final output is in
  `native/native-complete.log`; **no whole native lit pass is claimed**.
- Exact `ctcompile_exception_recovery`: **1/1 PASS**, 4.86 s, 4.87 s total.
  Includes transactional refusal for protected effects, rethrows, borrowed
  returns, conditional completion and zero budget. It was not replayed after
  later Python-only harness corrections.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.43 s, 3.45 s total.
- Selected `Analysis/Escape/escape-claims/table-index-overwrite.test`:
  **1/1 PASS**, 0.16 s, 357 excluded. The identical standalone baseline/final
  oracle checks also pass.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. All **11 final code/test hashes** match the devbox.
  Native and escape independent reviews are complete and clean.

Affected targets were built with `tools/remote-build.sh`: `ctjs-opt`,
`ctjs-translate`, `ctcompile-test-native-reference`,
`ctcompile-test-exception-recovery`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.
The initial build also relinked `ctcompile-test-host-contract`; its CTest was
not run. All remote operations held the build lock; commits held the Git lock
and staged explicit paths.

The first native recovery check exposed generated unreachable normal poison,
then unused non-Boolean dispatch constants; their narrowly checked removal fixed
the proof. Source harness corrections used the reference tool's positional
filename/global-output convention and retained the session wrapper's existing
`std::invalid_argument` for foreign documents. Those are ordinary validation
errors, not borrowed-node exceptions. The first arrays run had 36 failed
assertions: five remaining historical subtract-zero admissions (35 assertions)
and one exact work-count expectation. Only expectations changed; the production
proof did not change after that run.

Skipped: full CTest/compiler lit, complete iterator fixture, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. No browser/runtime/shared implementation changed, no C++ was built
locally, and nothing was pushed or history-rewritten. Availability inspection
found 11 readable Linux identities with 60 access errors, plus 344 Windows
Get-CimInstance records, and no actual Claude identities. Status was uncertain,
so concurrent-area rules applied throughout.

## Next boundaries

The frozen original iterator getter wrapped in a consuming outer catch,
`caught-node-getter-identity.js`, SHA-256
`1a7fb1661163b19674ee92084bc52ded3f96417279039131df31282ca6d813b4`,
still refuses **DOM iterator close requires one unobserved suppression landing**.
It needs the outer catch's actual payload/state and nested handler correspondence,
preserving cleanup writes, node identity, saved completions and exhaustion.
The method twin `7acf503b` shares that boundary. Original uncaught getter
`67bd3ad9` and method `f38a8b8a` retain the owner-lifetime diagnostic.
Conditional local catches are also still refused.

Escape's next measured source, SHA-256
`4f39da6ed4065c61435949dcb4118d2203dedd247169d668b9d0b47df9d43196`,
program `362f023c9aaba83c`, uses `(keys[i % 2] - 0.5) | 0`.
Its child remains Stored; Node returns `[0,0,0]` and the VM observes it confined
once without unresolved/unchecked instances. Nonzero arithmetic needs independent
result evidence. The previous fractional-property Node/VM discrepancy remains
separate. Protected observers, broader/nested iterators, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain unfinished.

Evidence: `../test-results/2026-09-23-confined-catch-zero-subtraction/`, with
SHA256SUMS. Checkpoints: `/tmp/ctcompile-native140`,
`/tmp/ctcompile-tests140`, `/tmp/ctcompile-escape140` and
`/tmp/ctcompile-review140`.
