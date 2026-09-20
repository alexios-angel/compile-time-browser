# Saved record Map snapshots and OR/XOR index congruence — 2026-09-20 UTC

Continued clean `b0b0d218` and the saved-record Map/disposal thread recorded in
HANDOFF and plan 00. No dirty predecessor draft or unmerged September 7 rescue
remained. Parallel agents implemented the escape proof, prepared source witnesses
and reviewed the constructor-publication boundary; the root integrated and gated.

Linux process inspection found 16 readable executable identities without Claude
matches and 57 unreadable identities. Windows Get-CimInstance returned 345
processes with no matching Claude executable or CLI script. Availability remained
uncertain; concurrent restrictions applied. No browser, shared code or runtime
oracle edits, and no push.

## Changes

- Native snapshot commit: `c4087c5c`. The existing completed-record Map census
  now collects saved aliases before own-key snapshots fold. Snapshots and field
  writes on those aliases participate in the same fixed-field proof as original
  instances and method receivers. The later full receiver census remains.
- Five new positive sources cover ordered alias snapshots, method snapshots,
  disposal by nulling each own field, overwrite/delete with saved aliases, and
  inherited snapshots. They add 40 native executions across both optimization
  modes, explicit/deduced output and GCC/Clang.
- Four new controls retain field addition, method-added fields, deletion and
  constructor publication refusals. The original alias-added-field source is
  unchanged and now receives the precise field-set-change diagnostic.
- Generated code retains stack records, concrete borrowed pointers and the
  existing Map template. Fixed snapshots become constants and the already-proved
  disposal loop becomes ordinary field assignments. No new runtime carrier,
  class ownership graph or Script/VM/GC dependency was introduced.
- `8dd68d45` preserves the transformed low-bit residue of OR/XOR indices from
  the input stride. Existing conversion/sign-band guards, exact replay and full
  reload/write checks remain. Added 12 CFG and 12 SCF rows plus 12 source
  witnesses; all 28 prior OR/XOR source bodies and calls are unchanged.

## Focused validation

Evidence: `/tmp/ctcompile-map-snapshots-1156/`. Devbox builds, tests and artifact
reads held `/tmp/ctbrowser-devbox-build.lock`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`.
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.52s; exact
  `ctcompile_host_contract`: **1/1**, 0.48s.
- Lowering lit `object-argument-lift.mlir`, `object-argument-refusals.mlir`,
  `constructor-refusals.mlir`; escape lit `bitor-xor-index-overwrite.test`,
  `bitand-index-overwrite.test`, `composed-index-overwrite.test`,
  `remainder-index-overwrite.test`: **7/7**, 4.32s.
- Final focused class probe: **60 source observations / 224 main native
  executions / 120 unprepared refusals / 85 preparation refusals / zero prepared
  native refusals**. Ancillary constructed-method controls: **16 executions /
  20 refusals**; original-r controls: **eight executions / four refusals**.
  Missing Object/Map identity controls pass. Complete proof budget cutoffs:
  snapshot overwrite/delete **1682**, original record overwrite **794**, helper
  holder **1478**, empty **222**, own-field loop **1337**.
- **80/80** concrete record-pointer artifacts across 20 positive sources and
  **10/10** raw forged Map/receiver refusal controls pass. All **nine** tested
  file hashes match. Inspected disposal and saved overwrite/delete C++ outputs.
- OR/XOR recording: **120 observed sites / 27 sound / 27 of 34 confined
  precision (79.4%)**; zero violations, partial, pending, unobserved claims or
  unclaimed sites. Seven imprecise Stored claims remain. These numbers reuse
  the focused lit recording; no additional corpus execution ran.
- Node syntax/scalar checks: nine new class sources; all 40 OR/XOR source
  functions execute and 12 new source checks pass. A supplementary arithmetic
  model checked 608,954 signed-band enclosure outputs. Python AST/Black, changed
  C++ formatting and whitespace checks pass. Original class fixtures 01–28
  remain byte-identical.
- Completed `tools/format.sh --check` retains **20 baseline diagnostics in six
  unchanged paths**: ctdrive.cpp, PrefixAnalysis.cpp, ProviderCallbacks.cpp,
  ProviderPaths.h, Heap.h and Facts.cpp. Whole-repository formatting does not pass.

The first native probe executed all five positives before two new negative
fixtures hit an out-of-range snapshot-index refusal ahead of the intended field
mutation check. Their extra index read was removed; the mutation, alias and
snapshot remain. The final probe checks the intended exact diagnostics.

Skipped: full CTest/compiler lit, whole class-initialization lit, broad native
matrices/corpus, DOM replay, full Bootstrap, WPT/test262, Windows and sanitizers.
Historical browser compliance measurements remain historical.

## Next boundary

Original B/Data+B still refuse `e.set(..., this)` during construction at the
own-field receiver-observation proof. The completed-record alias snapshot and
disposal prerequisite now works; partial publication still needs initialization
order, exception/reentry behavior and owner lifetime proof.

Three existing restrictions must connect: source `retainedMapAliases` and
closure `retainedByLocalMap` require completed entry-block owners; native
`proveRecords` requires same-block concrete records and Map operations.
Original Data additionally transports the receiver through a captured outer
element Map and nested DATA_KEY Map, conditional conflict checks, nullable gets,
and delete/empty cleanup. Preserve conflict checks, `e.set`, `e.remove`, `P.off`,
configuration and disposal bodies. Transported/nested and region-local record
Maps remain separate proof work. Full Bootstrap and the application driver are
unfinished.
