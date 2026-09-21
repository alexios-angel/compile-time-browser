# Multi-slot holders and intrinsic Strings, 2026-09-21 UTC

Continued clean `c2dc4554` from HANDOFF and plan 00/24/25. Its previous keyed
registration and Symbol-description work was committed; no interrupted native
files or `codex-wip-20260907` remained. Startup inspected 76 Linux process
identities and 356 Windows CIM identities without errors or a Claude executable,
Node CLI or loop match. No browser/runtime file changed.

## Changes

`95c290a2` extends the existing captured-Map helper normalizer to an exact
holder whose registration method is captured by a source class constructor.
It enumerates every fixed holder cell consumer and original constructor capture
read before projecting those uses into the shared callable-object census.
Every slot must precede the original call or constructor creation. Only sibling
calls in the owning entry block expand; the constructor's registration call
stays for the existing singleton publication proof after those siblings retire.
The terminal/base publication, complete observer census and concrete record-owner
proofs are unchanged. No borrowed record crosses a new frame.

Four new source admissions exercise keyed/unkeyed registration, called
get/remove/count siblings, overwritten saved aliases and an inherited base
followed by literal leaf writes. Eight new refusals cover extracted/replaced
methods, escaped holders, nonconstructor captures, constructor-time observers,
missing records, unused effectful slots and late slot initialization. The old
unused second-slot and captured outer-key bodies remain refused. All six previous
fixture files 30/33/35/36/37/38 retain their original bytes, and the full original
Bootstrap B/Data+B bodies remain in the focused gate.

`8e6e2e4d` permits already-proved ASCII-literal `startsWith` and
`charAt(0).toLowerCase()` calls in intrinsic exports. Existing String identity,
receiver, refinement, source effect, index and UTF-16 lowering proofs supply
all authority. Three new source bodies cover typed prefixes, saved descriptions
and immutable captured guarded descriptions. Empty/NUL inputs, Unicode expansion,
Greek sigma, supplementary pairs and lone surrogates remain exercised.
All 38 earlier positive source/native checks are unchanged.

The source oracle explicitly retains two known VM casing differences. Script's
`toLowerCase` at `ctbrowser/lib/Script/builtins/text/string.cpp:743` remains
ASCII-only: dotted I and Greek sigma do not lowercase there. Node and native
use Unicode behavior. A ten-observation devbox probe isolated exactly those two
mismatches; the export gate now checks 49 agreements plus two explicit VM false
results, with Node true and native Unicode assertions. This matches the
September 18/19 handoffs; no runtime was changed to fit native.

Parallel agents supplied the intrinsic implementation and a read-only ownership
review. The fixture agent and reviewer hit service limits; root completed the
source fixtures and all serialized gates.

## Focused validation

Three distinct lit cases pass across the corrected selections:

| Case under CTNative/ | Final result |
| --- | --- |
| Lowering/Objects/class-terminal-publication.test | PASS, 142.68 s |
| Exports/native-intrinsic-symbols.test | PASS, 257.77 s |
| Lowering/Objects/class-captured-map-helpers.test | PASS, 163.53 s |

The corrected middle selection passed 1/2 in 257.78 s: intrinsic exports passed;
the inherited class fixture still lacked its mandatory heritage contract.
The final class-only selection passed 1/1 in 163.54 s. No C++ change was needed
after the first successful build; only the new fixture classification/oracle
expectations were corrected.

The captured class fixture measures **82 source observations, 184 main native
executions, 164 unprepared refusals and 97 preparation refusals**. Four new
admissions add **32 native executions**, with eight new refusal bodies. The
new multi-slot first-complete budget is **1849**; keyed/single-holder/captured-holder
controls now measure 1219/799/921, and captured/direct-helper controls remain
802/679. Budget exhaustion, stale fingerprints, forged annotations and rollback
remain checked. The adjacent terminal fixture measures 58 observations,
160 main executions, 116 unprepared refusals and 70 preparation refusals.
Both class drivers also run the existing constructed-method controls
(16 executions/20 refusals) and original helper controls (eight executions/four
refusals); these are separate from the main counts.

Intrinsic exports measure **328 native executions, 314 refusals and two mutations**.
The three new admissions add **24 native executions**. Their source oracle
checks **49 Node/VM agreements and two explicit VM ASCII-casing differences**.
All **seven final code/test SHA-256 hashes** match the devbox.

All builds and native executions used the devbox under the shared lock. The
explicit targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract` and `ctcompile-test-native-reference`.
The initial six-step build passed, followed by no-work explicit builds for
fixture corrections. Exact `ctcompile_host_contract` passed 1/1 in 0.55 s,
0.56 s total. Required repository formatting retained 16 existing diagnostics
in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`HostContract/ProviderPaths.h`, `PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.
Two C++ and four Python files pass scoped formatting, syntax and whitespace
checks. No full suites, broad corpus/native matrices, full Bootstrap,
WPT/test262, Windows or new sanitizer runs were performed. No local build or push.

The initial 1/3 lit selection took 142.69 s: terminal publication passed;
intrinsic exports stopped at the known VM casing difference, and the new
inherited fixture needed the existing generic-assembly workaround for the
upstream `cf.switch` parser. Its original body was unchanged. Naming it with
the established `class-map-inherited` group also supplies the required heritage
intrinsic contract; no compiler guard was relaxed to pass the test.

Evidence: `/tmp/ctcompile-multislot-build1.log`,
`/tmp/ctcompile-multislot-gate{2,3,4}.log`, their devbox lit JSON files,
`/tmp/ctcompile-lowercase-oracle.log`,
`/tmp/ctcompile-multislot-format-final.log`,
`/tmp/ctcompile-multislot-local.sha256`,
`/tmp/ctcompile-multislot-evidence.log` and
`/tmp/ctcompile-multislot-results.log`.

## Exact next boundary

Original Data still needs three-argument registration with conditional nested
Map creation, conflict reporting, nullable lookup and child/empty-parent
cleanup. Original B additionally retains configuration/disposal, `e.set`,
`e.remove` and `P.off`. Prove every concrete owner, observer, exception and
reentry path before widening; preserve the complete bodies. This new flat Map
holder proof only handles called siblings in their owning entry block.
Unused sibling slots, nonconstructor captures, method-frame operations, the
captured outer key and broader constructor families remain separate.

Broader intrinsic String methods/indices, conditional callees, branch-mutated
boxed locals, String ordering, document views and the application driver remain.
No full-Bootstrap admission or coverage gain is claimed.
