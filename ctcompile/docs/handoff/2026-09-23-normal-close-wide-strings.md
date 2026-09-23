# Normal cleanup exceptions and wide finite Strings, 2026-09-23

Resumed clean **8224a1ff** and exact `b216ee20` from HANDOFF, Current native work,
the latest detail and synchronization journal. The old `codex-wip-20260907` was
already an ancestor; unrelated branches were left alone. Source fixtures, raw
proofs/review and escape work ran in parallel. Repeated rate limits and user
continuations resumed saved drafts and logs.

## Landed

**a5cf5588** retains a private terminal cleanup throw's payload as a helper result
and immediately throws it at the original normal close site. Eligibility requires
no protected closes and no mutable receiver/capture state. The complete method
and holder census requires exactly one adjacent throw use; frame, dominance,
budget and typed DOM proofs still gate publication. Existing suppression handles
saved-body exceptions unchanged. Primitive normal returns and throwing getters
remain separate refusals. Native output uses existing typed C++ exceptions.

Eight execution cases cover Number/String payloads, Boolean branch payloads,
normal break and body return, actual branch throws, suffix suppression and normal
exhaustion. The saved `39355ceb` cleanup case still exposes the original body
exception. The original `b216ee20` and body-return variant remain byte-identical,
now reaching a typed optional-String/Boolean join refusal. All 86 general positive
tuples, 143 general refusals, 151 saved source tuples and 347 historical close
tuples remain. Seventy-two historical close expectations were promoted after
measurement. Independent production review is complete and clean.

**f3fd445d** moves the finite String magnitude bound from validated parsing to
bounded arithmetic conversion. Public Core supplies modulo-2^32 bit conversion;
no parser or runtime behavior was copied or changed. Source length, grammar,
exponent limits, radix overflow, property spelling, origin and mutation checks
remain. Parent production review is complete; a separate independent escape
review was rate-limited before completion.

The final source is byte-identical to the preproduction baseline SHA-256
`f6a84ce7c30c4f124239c1c8e9c79cc7715058b27bc3f7b8e7f39cfae3314edf`.
Both runs identify program `48bf7525f0f77603`. Functions 103–108 change from six
stored child sites to **three confined and three stored**; every child is made
once with zero unresolved or unchecked observations. All 102 historical function
bodies remain. Six actual-source Node witnesses pass. Additional local checks
cover the historical wide String mask, three shift counts and BitNot conversions.

## Focused validation

- Normal cleanup: **128 native executions, 64 Node/VM observations**.
- Saved-exception regression and selected controls: **16 native executions,
  44 refusals, 8 Node/VM observations**.
- Source policies: **26 initial checks**, then **204 historical checks**
  (140 admissions, 64 refusals). The historical batch selected 102 normal-close
  inputs and excluded six already measured inputs. It did not run the complete
  iterator fixture.
- **13 source syntax checks, 32 local Node observations**; their saved outputs
  were retained across interruptions.
- `ctcompile_host_contract`: **1/1 PASS**, 2.62 s.
- `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.26 s / 3.27 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.15 s; **356 excluded**.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
- `git diff --check`; all **eight final code/test hashes** match the devbox.
- All **72 generated C++ variants** have no Script namespace; the existing
  standalone, compilation and linked-symbol checks pass.

The first arrays run failed 22 assertions for seven historical mask/count/SCF
BitNot cases. Their original inputs remain; only expectations were promoted.
The host test in that run passed and was not repeated. An interrupted arrays
build resumed its remaining compile/link before the final arrays CTest and the
first selected lit run. An initial shell heredoc lost commands to SSH stdin;
the missing baseline commands ran separately without rebuilding the completed
targets. Interrupted formatter runs had only their startup line and were resumed.
A temporary source formatter introduced unrelated wrapping; final integration
was reconstructed against the baseline, preserving all historical tuples.

Native execution used the frozen candidate. Final integration preserves the
exact eight source hashes and normal-helper AST, plus all historical saved
sources; it only adds the measured admission routing and repo formatting.
Completed execution, host, arrays and lit checks were not replayed.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Builds/tests held the shared build lock; Git writes
held the Git lock and staged explicit paths. There were no local C++ builds,
pushes, history rewrites or browser/shared implementation edits.

Initial process checks inspected 20 readable Linux executables and 343 Windows
records; final checks inspected 20 and 338. Linux access errors were 60 and 61.
No actual Claude identity matched, but availability remained uncertain and
concurrent-area rules applied.

Skipped: full CTest/compiler lit, complete iterator fixtures, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. These are focused results, not a full-suite or full-Bootstrap pass.
Evidence and checksums: `../test-results/2026-09-23-normal-close-wide-strings/`
relative to the repo. Working checkpoints are `/tmp/ctcompile-native131`,
`/tmp/ctcompile-tests131`, `/tmp/ctcompile-raw131` and `/tmp/ctcompile-escape131`.

## Next boundary

The unchanged `normal-selector-branch-throw-read-close`, SHA-256
`b216ee2066b536569e12637b5c9697c354307af5cb963f1908ec7a73ba70ccd5`, now refuses
**DOM entry branch has incompatible scalar alternatives** under both policies.
The body-return variant `e79de406` reaches the same refusal. Preserve the original
`getAttribute` optional-String payload and Boolean suffix throw, their branches,
reads, writes and suffix ordering. Do not suppress a normal cleanup exception or
replace its payload to obtain admission.

Normal throwing getters, mutable and mixed-suppression cleanup, broader control
flow, nested custom iterators, unguarded Bootstrap defaults, the application
driver and full native Bootstrap remain. Escape still excludes unsafe String
exponents, Strings over 32 bytes, computed fractional/nonfinite origins, general
powers and legacy SCF retention. The VM fractional-index discrepancy is separate.
