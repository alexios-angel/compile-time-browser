# Constructor provider graphs and nested indices, 2026-09-21 UTC

Continued clean `955c764d` and its recorded constructor/public DOM Data boundary.
Both commit histories, unmerged branches, synchronization protocol/claims/journal,
HANDOFF and plans 00/24/25 were read. `codex-wip-20260907` was already an ancestor;
there was no unfinished code. Linux cmdline/comm inspection completed for 71
processes and Windows CIM for 348, with no Claude executable, Node CLI or loop
match. No browser or shared implementation changed.

## Constructor provider proof

`aded05d2` adds `HostContract/ProviderRecords.cpp`. The final provider independently
reconstructs exact constructor closures, every construction and primitive actual,
literal/formal field stores and primitive nonreplacement returns. It rejects
captures, extra closure/symbol uses, unknown effects and unimported operations.
All newly admitted operations require valid operand dominance, including keys.

The same bounded graph follows entry-local standard Maps with literal String
keys through set/get/delete/clear and size/has observations. Every Map and record
use is checked. Saved gets retain their original allocation after deletion or
replacement. Field categories follow writes in source order; an unknown write
invalidates the category while an earlier saved scalar keeps its read-time kind.
Both read-only branch arms are checked. Strict equality supplies a Boolean
category and Number/Boolean arithmetic a Number category; none evaluates a
predicate, chooses a branch, substitutes a value or erases an allocation.

Seventeen existing prepared witnesses now prove all five public call edges,
retaining five constructions, eight functions and their original vendor class
computation. The native owner still refuses. No preparation attributes or old
provider reports authorize the source proof or native storage.

## Nested String indices

`ccd97336` admits two binary levels of literal Number arithmetic, with the
existing optional signs and native UTF-16 clamping. The complete use census
follows intermediate arithmetic, including Number addition, to actual String
bounds; escaping intermediates, additional depth and coercing operands refuse.
Computed zero/one retain no literal-only casing authority. Ordinary scalar
Number addition keeps its existing path.

Ten exact former refusal sources now execute. New aggregate and captured
description witnesses cover NaN, infinities, fractional bounds, signed zero,
underflow, mixed operations and UTF-16, with an ordinary-addition regression.
All 134 prior complete intrinsic cases and 33 general refusals remain unchanged.

## Focused validation

Explicit devbox targets under the shared build lock: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract` and `ctcompile-test-native-reference`.

- Exact CTest `ctcompile_host_contract`: 1/1, 0.55 s, 0.56 s total.
- Selected lit passed 3/3 in 105.21 s total:
  `CTNative/Browser/native-class-dom-data.test`,
  `CTNative/HostContract/dom-data-inputs.test` and
  `CTNative/HostContract/Provider/objects.test`.
- The class fixture retains 56 record and 24 object-key native executions. Its
  unchanged source checks include nine constructor-field, five field, five
  family and four local DOM Node/VM observations, plus nine published-record
  observations. The new provider checks cover 17 prepared witnesses.
- A final targeted provider check passed 16 prepared-IR refusals and forged-report
  recomputation. Controls cover constructor effects/replacement/arguments/escape,
  Map method/receiver/result/identity misuse, missing gets and escaping record
  aliases, stale fingerprints, zero budget and marked unimported operations.
  The two marked-operation controls were added after the selected lit run;
  their final targeted pass is not a second whole-fixture pass.
- The final targeted native probe retains refusal in both optimization policies:
  `owned global method table requires unconditional straight-line operations`.
- The focused String selector passed 128 native executions and 174 proof
  refusals, using GCC/Clang, both printing modes and both optimization policies,
  with native output and symbol checks. Twelve Node/VM agreements and one known
  UTF-16 difference. The full intrinsic replay and executable mutations were
  skipped. Two initial standalone runs failed before native execution because
  of incorrect Node paths; the passing run used the generated lit configuration's
  `/home/ubuntu/tools/node-v26.8.1/bin/node`.
- All eight final code/test SHA-256 hashes match the devbox. Four C++ headers/
  sources and two Python files pass scoped formatting and syntax checks.
- Scoped formatting/syntax checks pass. Required repository formatting reports
  16 pre-existing diagnostics in untouched `ctdrive.cpp`, `ProviderPaths.h`,
  `PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.

The first new build caught a `TypedValue` assignment; using `mlir::Value` fixed it.
A build-only heredoc consumed its trailing test commands, so that run is counted
only as a build. Subsequent file-based scripts explicitly ran the reported tests.
The first provider probe reached `ctjs.binary`; adding the strict-equality Boolean
category completed the original class computation. Independent review requested
and verified the skipped-operation and operand-dominance guards; final review
found no remaining issue. Fixture and String agents saved their drafts before
service limits; root completed their gates.

No full CTest/compiler lit, broad corpus/matrices, full Bootstrap, WPT/test262,
Windows, new sanitizers, local native build or push ran.

Evidence: `/tmp/ctcompile-provider-constructor-gate{3,4}.log`,
`/tmp/ctcompile-provider-records-lit.log`,
`/tmp/ctcompile-provider-records-final-gate.log`,
`/tmp/ctcompile-string-nested-native.log`,
`/tmp/ctcompile-string-nested-focused.py`,
`/tmp/ctcompile-provider-records-final.sha256` and
`/tmp/ctcompile-provider-records-format-final.log`.

## Next native boundary

The provider now proves the complete prepared constructor/local-Map witnesses.
The next measured refusal is in `OwnedGlobalMethods.cpp`'s operation census:
`owned global method table requires unconditional straight-line operations`.
Its subsequent exact function-count, constructor, allocation, field and call
checks also need the complete live source graph. Share/recompute that graph;
a constructor allow-list or preparation category is insufficient. Preserve both
read-only branch arms and every original record/Map allocation and alias.

Then extend `ClosureLifting/MethodTables.cpp` with an explicitly proved constructor
lift. The generic constructor guard still rejects Map payload uses, and its
rewrite supplies an undefined callee whereas host `Values.cpp::exactCall`
requires exact callable provenance. Both issues must be resolved before native
owner checks before and after transformation can pass.

Original `published_source()` composite-result witnesses still refuse class
preparation. Provider-only scalar evidence does not widen that preparation pass.
Multiple DOM-input alias partitions, original B/Data+B, broader Strings,
conditional callees, mutable cells, String ordering, document views and the
application driver remain. No full-Bootstrap admission or corpus coverage gain
is claimed.
