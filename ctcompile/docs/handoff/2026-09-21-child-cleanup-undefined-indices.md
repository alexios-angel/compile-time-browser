# Child Map cleanup and explicit undefined String indices, 2026-09-21 UTC

Continued clean `d8ec7a5a` from its recorded child cleanup boundary. The earlier
inherited Data and omitted String work was fully committed; `codex-wip-20260907`
is already an ancestor. The initial Linux cmdline/comm census read 74 processes
without denied records; Windows CIM read 355. Neither found an actual Claude
executable, Node CLI or loop. The first executable-symlink census was incomplete
and was superseded by the readable command/identity census. No browser or shared
implementation files changed.

Parallel agents supplied the source fixture draft, the String increment and
read-only proof reviews. Two user continuations stopped the agents and first
build during source sync. Root preserved the drafts, finished the four class
fixture/harness files and owned all builds and commits. The interrupted first
build has no completed validation result.

## Implementation

`0bdef7c9` tracks child membership by allocation identity from allocation onward
in the nested Map preparation walk. Literal-key `has`, `size` and `delete` facts select exact
Map-only branches, including guarded removal and empty-parent cleanup. Both arms
still pass the effect census; all child operations survive for the unchanged
record ownership and native Map proofs. Every saved child alias and captured cell
is checked before folding. A second outer owner is refused because mutations
through its lookups could invalidate the first owner's occupancy facts.

The new source fixture has three admissions: direct captured registration/removal,
a called Data holder with constructor registration, and deletion-result short
circuits. It covers multiple keys, cached observations, saved child/record aliases
and deletion/recreation. Seventeen complete-source refusals retain unknown keys
and conditions, object/Number keys, escaped owners and holders, region owners,
dead calls/coercion, unused slots, exceptions/reentry, nullable record reads and
observed mutation results. The direct witness uses existing stale-fingerprint,
proof-input and first-complete-budget rollback checks. All 46 earlier source
fixtures are byte-identical.

`17509a81` admits proved explicit undefined String indices: `charAt(undefined)`
and `slice(undefined)` start at zero; `slice(start, undefined)` keeps the omitted end. The emitter uses defaults without converting undefined to NaN and
only emits a length when a numeric bound needs it. Direct-literal first-unit and
dataset-tail authority remains unchanged. Five admissions include three original
refusal bodies preserved byte-for-byte, Unicode/default-end and captured-description
cases. The existing native output and linkage checks still exclude Script/VM.

## Focused validation

| Final check | Result |
| --- | --- |
| CTest `ctcompile_host_contract` | 1/1, 0.56 s; 0.58 s total |
| `CTNative/Lowering/Objects/class-captured-map-helpers.test` | PASS, 354.36 s |
| `CTNative/Lowering/Objects/class-terminal-publication.test` | PASS, 143.29 s |
| `CTNative/Browser/native-dom-strings.test` | PASS, 162.82 s |
| Targeted intrinsic exports | 64 executions, followed by corrected 596-refusal control pass |

- Build2: three compile/link steps passed; exact `ctcompile_host_contract` passed
  1/1 in 0.56 s (0.57 s total).
- Twenty new Node source observations and three Python AST/Black checks passed.
- Terminal publication passed in 143.29 s; DOM Strings passed in 162.82 s.
- The first captured-helper run failed in 8.88 s during the new direct witness's
  budget replay (the two-case selection completed in 143.30 s). Isolated 25,000
  step calls passed, but a harness replay captured SIGSEGV at 6,152 steps in
  `eraseRooted` from `normalizeNestedMaps`. The backtrace and disassembly identified
  its retained-operation loop. LLVM 23's tombstone-free DenseSet relocates buckets
  during erase; the earlier erase-while-iterating filter could leave dead-arm
  pointers behind. `remove_if` now filters safely. Three complete budget replays
  pass at the same exact cutoff, **6217**. The corrected captured-helper lit passes
  in **354.36 s** (354.37 s total), measuring **207 source observations, 416 main
  native executions, 414 unprepared refusals and 273 preparation refusals**.
  Three new admissions add 24 executions. Existing constructed-method controls
  also run 16 executions/20 refusals, and original helper guards run eight/four.
- The first standalone String launcher named nonexistent `/usr/bin/node`; it did
  not validate native output. Retrying with the generated lit configuration's
  `/home/ubuntu/tools/node-v26.8.1/bin/node` completed **64 native executions** before
  an incorrect new refusal expectation failed. Assigning global `undefined` is
  ignored by the importer; that control now mutates a writable local instead.
  The native implementation and five admission bodies did not change. The final
  proof-only selector passes **596 refusals, 98 Node/VM agreements and nine known
  differences**, without replaying those 64 binaries. The whole intrinsic-export
  fixture and its existing executable mutations were not replayed; no final
  whole-export-fixture pass is claimed.
- Build5 compiled three steps; the final exact host-contract CTest passed 1/1 in
  0.56 s (0.58 s total). All eight final code/test hashes match the devbox.
- Three C++ and four Python files pass scoped formatting/syntax. The first required
  formatter ran during a String draft (16 old diagnostics plus four draft lines).
  The frozen check retains only the 16 existing diagnostics in untouched files.

These are three distinct lit passes, one exact CTest, the isolated budget
replays and the targeted export checks. All devbox builds used explicit targets
`ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract` and
`ctcompile-test-native-reference` under the build lock. Builds 2–5 compiled
3, 6, 3 and 3 steps respectively; the interrupted build1 has no completed result.
No instrumentation was added to repository code.

Evidence is under `/tmp/ctcompile-child-cleanup-*`: build2–build5 logs, class and
DOM lit JSON files, budget probe/harness logs, final.sha256, final-evidence.log
and format-final2.log. The temporary selector scripts are
`/tmp/ctcompile-string-undefined-focused.py` (eight cases) and
`/tmp/ctcompile-string-undefined-controls.py` (proof/oracle controls only).
Remote logs use the same stems. The final formatter's 16 existing diagnostics
are in `ctbrowser/tools/ctdrive/ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`; none is in a changed file.

No full CTest/compiler lit, broad corpus or native matrix, full Bootstrap,
WPT/test262, Windows, new sanitizers, local native builds or push were run.

## Exact next boundary

The original Data getter returns `outer.has(element) &&
outer.get(element).get(key) || null`. Nullable lookup needs both selected-child
identity and record-or-null transport. Registration still contains the conflict
observer arm (`console.error` and `Array.from`); the helper cannot be erased while
that arm lacks proof. Actual element keys need the public DOM/HostContract owner
seam. Original B/Data+B remains a complete-source refusal; this increment is not
full-Bootstrap admission or a new corpus measurement. Dynamic/coercing String
indices, NaN origins, broader methods, conditional callees, branch-mutated cells,
String ordering, document views and the application driver remain unfinished.
