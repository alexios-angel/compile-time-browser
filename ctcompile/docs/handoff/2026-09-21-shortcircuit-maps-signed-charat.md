# Short-circuit Maps and signed charAt indices, 2026-09-21 UTC

Continued clean `3e3ebde3` from the recorded Data short-circuit boundary.
No uncommitted native draft or `codex-wip-20260907` branch remained. Windows CIM
checked 359 processes without a Claude match; Linux executable reads were denied
for 57 processes, so availability remained uncertain and concurrent-agent rules
applied. No browser, runtime or shared build implementation changed.

Three agents supplied source fixtures, independent signed `charAt` support and
read-only proof review. The user continuation interrupted the agents; they resumed
their saved work. Root owned integration, devbox gates and separate commits.

## Changes

`f5ac5a7a` extends `normalizeNestedMaps` to statically selected single-result
branches. The existing full census checks both arms. A separate complete-use
check permits an outer `set`/`clear` result to travel through nested unused yields;
a cell, observer, return or escape still refuses, including in an unselected arm.
Selected branch/get correspondences resolve transitively during simulation.
Reverse branch rewriting replaces live SSA results and cached cell values before
erasing their producers. Child Maps keep their original owning entry and the
unchanged record-owner proof checks every surviving borrow.

Three new admissions cover nested `has || set`, `has && get`, repeated registration,
distinct keys, cached Boolean results, absent short-circuit results and saved
child/record aliases across replacement and cleanup. Eleven complete-source
refusals include caller conditions, outer-result escape, observed dead mutation
results, dead user calls/coercion, absent gets, captured observers and record
lifetime/escape. The repeated witness retains fingerprint, forged-proof and
budget rollback checks; its first complete budget is **1704**. The earlier
conditional/direct nested witnesses now measure 1001/836. All 42 previous class
fixture files, including original Bootstrap sources, remain byte-identical.

`d8f532c1` reuses the signed String-index proof for `charAt`. A single literal
negation must have only admitted index uses; magnitudes are bounded by uint32.
Lowering clamps unsigned magnitudes safely and uses the UTF-16 length as the empty
substring position for negative indices. Negative zero selects the first unit.
Exact direct `charAt(0)` and one-argument `slice(1)` retain their earlier authority;
`charAt(-0).toLowerCase()` does not acquire that separate proof.

Three new export admissions include the unchanged original negative-index refusal,
empty/Unicode/NUL/extreme-index checks and captured Symbol descriptions. Fourteen
new refusal bodies, missing-String contracts, low budgets and a stale fingerprint
accompany them. All 51 prior positive sources and 33 general refusal sources are
preserved. Native/Node UTF-16 expectations remain intact; six known VM casing or
byte-indexing differences are explicit.

## Focused validation

| Check | Result |
| --- | --- |
| Final exact CTest `ctcompile_host_contract` | 1/1, 0.57 s; 0.58 s total |
| `CTNative/Lowering/Objects/class-captured-map-helpers.test` | PASS, 306.84 s |
| `CTNative/Lowering/Objects/class-terminal-publication.test` | PASS, 139.34 s |
| `CTNative/Exports/native-intrinsic-symbols.test` | PASS, 399.77 s |
| `CTNative/Browser/native-dom-strings.test` | PASS, 210.36 s |

The final three-case selection passes **3/3 in 399.79 s**. Captured class checks
measure **136 source observations, 296 main native executions, 272 unprepared
refusals and 174 preparation refusals**. Three new admissions add 24 native
executions. The adjacent terminal fixture measures 58 observations, 160 main
executions, 116 unprepared refusals and 70 preparation refusals. Both also run
constructed-method controls (16 executions/20 refusals) and original helper
controls (eight executions/four refusals), separate from the main counts.

Intrinsic exports measure **432 native executions, 466 refusals and two mutations**,
with **62 Node/VM agreements and six known differences**. Three new admissions
add 24 executions. DOM Strings measure **809 Node/VM observations and eight
GCC/Clang binaries**, plus 1,000 source refusals, 44 provenance/depth refusals,
24 method provenance checks, 241 capture provenance/budget checks, 101 replacement
checks, 22 branch checks and 31 completion checks.

All builds and native executions ran on the devbox under the shared lock.
Explicit targets were `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`
and `ctcompile-test-native-reference`. Build 1 rejected a `FromBoolOp::getValue`
accessor typo; `getBit` fixed it and build 2 passed five steps. Review moved the
mutation-result census ahead of arm selection before the final four-step build.
A heredoc build invocation did not execute its trailing CTest; the test ran
separately and passed 1/1 in 0.55 s (0.56 s total), then passed again on the final
sources as listed above. No native fixture failure occurred.

All **eight final code/test SHA-256 hashes** match the devbox. Three C++ and four
Python files pass scoped formatting and syntax/whitespace checks. All 14 new class
Node observations and 68 intrinsic Node observations pass; the final lit gate also
checks VM expectations. A local preservation check initially used unpadded file
numbers; correcting the paths confirmed all 42 original fixtures are unchanged.
Required `tools/format.sh --check` retains **16 existing diagnostics** in untouched
`ctbrowser/tools/ctdrive/ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`. An initial formatting run
also saw active drafts; the final frozen run contains only that baseline.
Full CTest/compiler lit, broad corpus/native matrices, full Bootstrap, WPT/test262,
Windows, new sanitizers, local native builds and push were skipped.

Evidence uses `/tmp/ctcompile-shortcircuit`: `-build1.log`, `-build2.log`,
`-terminal.log`, `-terminal.json`, `-gate3.log`, `-final.json`, `-evidence.log`,
`-local.sha256` and `-format-final.log`.

## Exact next boundary

Start with exact entry calls to a captured three-argument registration helper.
`Proof.cpp` currently runs nested routing before `normalizeCapturedMapHelpers`;
the latter admits only straight-line direct captured-Map operations. Prove and
substitute the original captured Map and three actual arguments, expose its
structured short-circuit/child operations in the owner entry, then resolve nested
routes. This requires complete helper/callee/cell-use and expansion-budget checks.

Constructor publication is the following seam: `normalizePublicationHelper` and
`sinkConstructorPublication` currently expect narrower direct registration.
Original `Data.set(element, componentKey, this)` needs both key origins and the
selected child owner without discarding sibling `get`/`remove` observers. Actual
element identity, conflict reporting (`console.error` and key iteration), nullable
lookup and conditional empty-parent cleanup each retain proof obligations. Keep
owner, exception and reentry ordering intact before moving publication across
construction. Original B/Data+B remains refused with element normalization,
configuration, disposal and complete helper bodies preserved. No full-Bootstrap
admission or coverage gain is claimed.

Dynamic/coercing String indices, broader String methods, conditional callees,
branch-mutated boxed locals, String ordering, document views and the application
driver remain separate work.
