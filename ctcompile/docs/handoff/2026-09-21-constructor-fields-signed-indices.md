# Constructor field initialization and signed indices, 2026-09-21 UTC

Continued clean `7b055fd6` from HANDOFF and the current plan journal. Both commit
histories and unmerged branches were read; `codex-wip-20260907` is already an
ancestor. There was no unfinished code at entry. Linux cmdline/comm inspection
completed for 71 processes and Windows CIM for 348, without a Claude executable,
Node CLI or loop match. No browser or shared implementation was edited.

## Changes

`49eeb949` preserves categories from the already-proved constructor stores in
`ClassInitialization/DOMData.cpp`. Each original construction gets its own field
map. A formal is resolved to that construction's exact primitive literal actual;
literal stores use their own categories. The last constructor store wins, and the
map is installed when the entry walk reaches that allocation. Existing entry
writes still replace or invalidate the field category, and saved reads retain
their read-time category through later mutation.

The constructor, return, instance-use and saved Map-alias censuses remain required.
These facts stay private to preparation: they do not replace values, choose
branches, erase constructors or grant native owner authority. Constructor
expressions, missing actuals, object arguments and unproved effects remain refused.

Nine witnesses cover literal/formal stores, two instances with different categories,
repeated constructor stores, registration and saved reads. Three are the exact
former constructor-only, wrong-record and early-read refusal sources. All preserve
the complete vendor Data declaration, separately observed class computation, five
constructions and eight functions. Controls distinguish Boolean from Number
arithmetic and preserve last-write and read-time categories. Original complete
composite-result witnesses remain unchanged and refused.

`91e64124` admits one unary minus on either Number-literal operand of the existing
single arithmetic String bound, including its already-supported outer negation.
It reuses native Number arithmetic and UTF-16 clamping. Each signed operand and
arithmetic result passes the complete bound-use census. Dynamic/coercing operands,
further expression nesting and escaped arithmetic remain refused; computed zero
and one grant no direct-literal casing authority. Eleven former refusal bodies
now execute unchanged. Review narrowed the Add discriminator so existing
`typeof`/String addition keeps its earlier path, with an execution regression.

## Focused validation

Explicit devbox targets under the shared build lock:
`ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract` and
`ctcompile-test-native-reference`.

- Final exact CTest `ctcompile_host_contract`: 1/1, 0.55 s, 0.56 s total.
- Selected lit `CTNative/Browser/native-class-dom-data.test`: PASS, 110.2659 s;
  `CTNative/HostContract/dom-data-inputs.test`: PASS, 1.40266 s.
  Total: 110.27 s. These ran alongside the focused String selector.
- New constructor fields: nine Node/VM observations, nine preparations,
  33 refusals. Existing class fields: five observations/preparations,
  26 refusals after the three promotions. Existing public family: five
  observations, three preparations, 29 refusals. Existing local DOM checks:
  four observations/preparations, 30 preparation and eight session refusals,
  24 object-key native executions. Published records: nine observations,
  56 native executions, 54 refusals.
- String selector: 128 native executions and 84 proof refusals, including
  GCC/Clang, both printing modes and both optimization policies, with native
  output and symbol gates. Thirteen Node/VM agreements and one known UTF-16
  difference. All 121 earlier complete intrinsic cases and 33 general refusals
  were preserved; eleven indexed refusal bodies moved intact to executions.
- All four final code/test hashes match the devbox. Both C++ files pass the
  pinned formatter; both Python files pass repository Black and syntax checks.
  Eight prior DOM functions are structurally unchanged; the other two changes
  move the three exact controls and register the new fixture.
- Required repository `tools/format.sh --check` retains 16 pre-existing
  diagnostics in untouched `ctdrive.cpp`, `HostContract/ProviderPaths.h`,
  `PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.

The independent String agent supplied its frozen implementation and selector;
root rebuilt after the final Add correction. The fixture agent saved its draft
before a service limit; root completed formatting and the devbox gate. Read-only
review found no remaining issue in the constructor change or its fixtures.

No full CTest/compiler lit, full intrinsic-export replay, broad corpus/matrix,
full Bootstrap, WPT/test262, Windows, new sanitizers, local native build or push
ran. Existing String executable mutation tests were skipped by the selector.

Evidence: `/tmp/ctcompile-constructor-fields-build{0,1}.log`,
`/tmp/ctcompile-constructor-fields-gate2.log`,
`/tmp/ctcompile-constructor-fields-lit.log`,
`/tmp/ctcompile-string-signed-operands-focused.{py,log}`,
`/tmp/ctcompile-constructor-fields-final.json` and
`/tmp/ctcompile-constructor-fields-format-final.log`.

## Next ownership sequence

A fresh host analysis of the prepared constructor-only witness reports
`unsupported provider behavior through ctjs.construct`: five candidate public
edges, zero proved edges. Its report is on the devbox at
`/tmp/ctcompile-constructor-initial-probe/constructor-only-host.report.json`.

1. Recompute the exact constructor/local-Map/field graph in the final
   `HostContractAnalysis`. `environmentProblem` currently recognizes the public
   captured Map family; private preparation categories do not survive into this
   analyzer. Require every construction's closure, actuals and nonreplacement
   return, all instance and saved Map aliases, ordered field accesses, and every
   local Map allocation and operation before authorizing scalar/public-call edges.
2. Extend `OwnedGlobalRoots::analyzeMethodTable` in `OwnedGlobalMethods.cpp` with
   the same complete source graph. Its current census rejects the additional
   constructor function, then constructors, allocations, fields and calls.
   Preserve original allocation identity and all source function counts.
3. Add an explicitly proved constructor lift to the host preparation path.
   `LowerToEmitC.cpp` checks ownership before and after host transformations;
   `ClosureLifting/MethodTables.cpp` currently lifts wrapper, factory and public
   methods. Reuse `ClosureLifting/Constructors.cpp` and `Rewrite.cpp` guards before
   Map/object preparation. The generic lifter is skipped for host contracts.
   Account for lifted receiver/callee provenance: the ordinary constructor lift
   emits an absent callee, while `Values.cpp::exactCall` requires a proved target.

This is a composition of proofs and transformations, not a constructor allow-list.
The full vendor composite `result` additionally needs local Map observations and
Boolean-to-Number category evidence. Multiple DOM-input alias partitions,
original B/Data+B, broader Strings, conditional callees, mutable cells, String
ordering, document views and the application driver remain. No full-Bootstrap
admission or corpus coverage gain is claimed.
