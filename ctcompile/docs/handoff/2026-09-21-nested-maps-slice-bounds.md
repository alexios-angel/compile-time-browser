# Finite nested Maps and String slice bounds, 2026-09-21 UTC

Continued clean `a9eb0f33` from the recorded Data boundary. There was no
uncommitted native draft or `codex-wip-20260907` branch. Linux cmdline/comm
(72 processes) and Windows CIM (354 processes) checks found no Claude executable,
Node CLI or loop. No browser, runtime or shared build implementation changed.
An external interruption stopped the first agents before they saved code.
The resumed fixture and review agents later hit service limits; root recovered
their saved work and findings. The String agent completed its bounded draft.

## Changes

`8bbcf23b` proves finite outer-Map contents before class preparation. Every outer
use belongs to the same entry block, every key is a literal String, and every
stored child is an existing local Map. The complete method and raw cell census
rejects escaping or captured outer owners. Original-module binding validation
still checks intrinsic identity before publishing the prepared module.

Each outer `get` becomes its exact child allocation at that source position;
`has`, `size` and `delete` retain their observed scalar results. Parent writes,
deletes and clears cannot retarget saved child aliases. Only the proved outer
Map is erased. Child Maps and class records remain subject to the unchanged
source and native lifetime proofs, with ordinary stack record owners. No new
runtime representation, reference-counted ownership graph or VM dependency was
introduced. Cached cell values are updated before producers are erased, and
chained outer aliases are disconnected before their cells are retired.

Three new sources cover constructor-time child registration, distinct children
using the same inner key, and saved child/record aliases across parent and child
replacement, deletion and clearing. Eight refusals retain escaping outer/child/
record values, missing outer/inner keys, conditional child allocation, captured
observers and dynamic outer keys. The direct witness checks stale fingerprints,
forged annotations, complete source census and rollback at every incomplete
budget; its first complete budget is **830**. Original files 30–40, including
the Bootstrap B/Data+B controls, remain byte-identical.

`7f226c55` admits literal uint32 `slice(start, end)` through the existing public
Core WTF-8/UTF-16 conversion and `std::u16string::substr` path. The start clamps
to the string length; the count cannot underflow when the end precedes it.
Only the original one-argument `slice(1)` supplies dataset-tail authority, and
only `charAt(0)` supplies first-unit lowercase authority. Negative, fractional,
nonfinite, oversized, dynamic and coercing end bounds remain refused.

Three export bodies cover the unchanged former slice-end refusal, boundary and
Unicode cases, and immutable captured Symbol descriptions. Fifteen new source
refusals, missing-String contracts, low budgets and a changed-end fingerprint
control accompany them. The original `class_utf16_slice_extra` source is promoted
unchanged. Two full-H refusal bodies ensure bounded tails do not acquire dataset
reconstruction authority. All 44 prior positive export sources/native assertions
and the 33 initial refusal bodies remain intact. Native/Node Unicode expectations
remain authoritative; four explicit VM casing/indexing differences are recorded.

## Focused validation

| Check | Result |
| --- | --- |
| Exact CTest `ctcompile_host_contract` | 1/1, 0.55 s; 0.56 s total |
| `CTNative/Lowering/Objects/class-captured-map-helpers.test`, final fixture | PASS, 272.76 s |
| `CTNative/Lowering/Objects/class-terminal-publication.test` | PASS, 149.07 s |
| `CTNative/Exports/native-intrinsic-symbols.test`, corrected test assertion | 1/1, 303.00 s |
| `CTNative/Browser/native-dom-strings.test` | PASS, 210.66 s |
| Existing class DOM driver, UTF16 cases/refusals plus one full-H positive and two bounded-tail refusals | 100 Node/interpreter observations, eight native executions, 466 refusals |

The final captured fixture measures **106 source observations, 248 main native
executions, 212 unprepared refusals and 134 preparation refusals**. Its three new
admissions add 24 executions. The terminal fixture measures 58 observations,
160 main executions, 116 unprepared refusals and 70 preparation refusals. Both
also retain constructed-method controls (16 executions/20 refusals) and original
helper controls (eight executions/four refusals), separate from those main counts.

Intrinsic exports measure **376 native executions, 394 refusals and two mutations**,
with **55 Node/VM agreements and four known differences**. DOM Strings measure
809 observations and eight GCC/Clang binaries, plus their existing source,
provenance, completion, replacement and budget controls.

All builds and native executions ran on the devbox under the shared lock.
Explicit targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract` and `ctcompile-test-native-reference`. The first
build rejected a Number attribute constructed from a double instead of binary64
bits; the existing `bit_cast` pattern fixed it. The corrected seven-step build
passed. Initial captured/terminal lit passed 2/2 in 221.39 s. The next three-case
selection passed 2/3 in 315.21 s: the String test's new unqualified `js_num`
assertions conflicted with the compatibility alias. Qualifying only those test
types as `ctnative::js_num` fixed the final export run. Subsequent explicit builds
reported no work. No compiler guard was widened to address either failure.

All **11 final code/test SHA-256 hashes** match the devbox. Five C++ and five
Python files pass scoped formatting, Python syntax and whitespace checks.
Required repository formatting retains 16 existing diagnostics in untouched
`ctbrowser/tools/ctdrive/ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.
Full CTest/compiler lit, broad corpus/native matrices, full Bootstrap, WPT/test262,
Windows, new sanitizers, local native builds and push were skipped.

Evidence uses prefix `/tmp/ctcompile-nested-slice`: `-build1.log`, `-gate2.log`,
`-gate3.log`, `-lit1.json`, `-lit-final.json`, `-exports-final.log`,
`-exports-final.json`, `-utf16.log`, `-evidence.log`, `-final-evidence.log`,
`-local.sha256` and `-format-commits.log`.

## Exact next boundary

Extend concrete child origins and record-owner proofs through conditional child
creation, then original `Data.set(element, componentKey, this)` publication.
Both key dimensions, conflict reporting, nullable lookup and child/empty-parent
cleanup need their complete observer, exception and reentry proofs. The new
normalization proves fixed String-key routing among preallocated children;
it does not establish those dynamic behaviors or general nested record transport.
Original B/Data+B remains refused with configuration, disposal and complete
helper bodies intact. No full-Bootstrap admission or coverage gain is claimed.

Broader String methods and negative/dynamic/coercing indices, conditional callees,
branch-mutated boxed locals, String ordering, document views and the application
driver remain separate work.
