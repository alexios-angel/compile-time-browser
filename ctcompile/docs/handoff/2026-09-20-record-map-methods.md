# Saved record Map methods and AND index gaps — 2026-09-20 UTC

Continued clean `a303f203` and the completed-record Map boundary from
`c955af5a`. No unmerged September 7 rescue or dirty predecessor work remained.
Parallel agents handled source witnesses, proof review and the AND index proof.
Rate/service interruptions stopped the agents twice; the root resumed the same
claims and drafts, integrated the work and ran all devbox gates.

Linux: 12 readable identities, no actual Claude executable/CLI matches, 56
unreadable identities. Windows Get-CimInstance initially listed 345 processes;
a final executable/script-token scan listed 348 with no Claude matches.
Availability remained uncertain, so concurrent restrictions applied throughout.
No browser, shared code or runtime-oracle edits; no push.

## Landed

- `6fe314c8` admits instance method calls through saved results of local record
  Maps. Source preparation reuses its exact method-call/receiver census; the
  closure lift connects saved reads to the records chosen before replacement
  or deletion. Only direct calls of immutable selectors are admitted. Method
  replacement, detached method values, constructor selection and escaping
  receivers remain refused.
- Final Map admission carries saved aliases as borrowed receiver pointers only
  when the direct call and target have matching receiver signatures. Final
  target admission still rechecks closed receiver uses and rejects escaping
  call components; raw annotations alone are not a lifetime proof.
- The unchanged original alias-method source now runs natively. Five new
  positives cover read, mutation, overwrite/delete, three-level inheritance and
  distinct receivers sharing one method implementation. Four new controls keep
  replacement, extraction, receiver escape and constructor publication refused.
  Original class source files 01–27 remain byte-identical.
- Generated C++ keeps the existing stack records and
  `map_storage<std::string, concrete_record *>`. Saved aliases become pointers
  passed directly to ordinary functions. No new runtime carrier, class ownership
  graph, Script/VM/GC dependency or browser implementation was introduced.
- `8b9d3f8b` uses `std::countr_zero` to preserve the low zero bits of an AND mask
  as an index stride, including signed results. Zero masks and OR/XOR keep the
  previous stride. Exact replay, full reload/write census and work/depth bounds
  remain. Ten CFG and ten SCF rows cover positive/negative masks, commutation,
  gap retention, overlapping reloads and later writes. Eleven new source
  witnesses leave all 32 prior function bodies unchanged.

## Focused validation

Evidence: `/tmp/ctcompile-alias-method-1127/`. Every devbox build, execution and
artifact read held `/tmp/ctbrowser-devbox-build.lock`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-host-contract`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`.
- Exact `ctcompile_host_contract`: **1/1**, 0.50s. Exact
  `ctcompile_escape_analysis_arrays`: **1/1**, 1.53s.
- Lowering lit `object-argument-lift.mlir`, `object-argument-refusals.mlir`,
  `constructor-refusals.mlir`, plus escape lit `bitand-index-overwrite.test`,
  `remainder-index-overwrite.test`, `composed-index-overwrite.test`: **6/6**,
  4.26s. Earlier native-only host/lowering checks also passed; these are focused
  selections, not full suites.
- Final selected class driver: **46 source observations / 160 main native
  executions / 92 unprepared refusals / 56 preparation refusals / zero prepared
  native refusals**. Six newly native sources add **48 executions**, covering
  both optimization modes, explicit/deduced output and GCC/Clang. Ancillary
  constructed-method controls: **16 executions / 20 refusals**; original-r
  controls: **eight executions / four refusals**, counted separately.
- Five raw mutations with forged Map/receiver proof annotations refuse in both
  modes: **10/10**. The new target that publishes its borrowed receiver retains
  the exact closed-shape refusal at `ctjs.store_global`; its caller component
  also refuses. Existing final standard-Map identity control remains.
- **60/60** generated record-pointer artifacts across 15 positive record sources
  pass concrete pointer/no-identity-object checks. Inspected overwrite/delete
  and inherited method outputs. Source construction preservation passes, with
  the inherited case explicitly retaining its Map and leaf construction after
  proven `super` normalization removes imported ReferenceError guards.
- Existing source proof cutoffs remain overwrite **622**, helper holder **1464**,
  empty **218**. AND recording checker: **129 observed sites / 26 sound /
  26 of 32 confined precision (81.2%)**; zero violations, partial, pending,
  unobserved claims or unclaimed sites. Six imprecise Stored claims remain.
  This report reuses the focused lit recording; no extra corpus replay ran.
- All **ten** final tested file hashes match the devbox. Node checks passed for
  the nine new class sources and all 43 AND sources, including 11 new retention
  checks. Python AST/Black, changed C++ formatting, old-source preservation and
  whitespace checks pass. Completed `tools/format.sh --check` reports **20
  existing diagnostics in six HEAD-identical files**; whole-repository
  formatting does not pass.

The first extended probe exposed two harness requirements for the new inherited
source: declare the existing inheritance hooks, and account for removed imported
ReferenceError constructors separately from the two application constructions.
The new raw fixture initially lacked the required `upvalue_count` attribute.
These test-harness issues were fixed before the final passing selection.

Skipped: full CTest/compiler lit, whole class-initialization lit, broad native
matrices/corpus runs, DOM replay, full Bootstrap, WPT/test262, Windows and
sanitizers. Historical browser compliance measurements remain historical.

## Next boundary

Original B/Data+B still refuse `e.set(..., this)` during construction, first at
own-field snapshot receiver observation. Completed local record storage and
saved-alias methods are connected, but partial publication requires proof of
initialization order, exception/reentry behavior and enclosing owner lifetime.
Constructor/helper captures and the original nested Data Maps also exceed the
current direct entry-block record Map proof. Preserve every conflict check,
registration/removal call, `P.off`, configuration and disposal body.

Own-key snapshots with Map aliases, region-local owners and transported/nested
record Maps remain separate proof boundaries. Full Bootstrap and the application
driver remain unfinished.
