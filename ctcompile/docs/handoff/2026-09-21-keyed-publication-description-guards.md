# Keyed publication and description guards, 2026-09-21 UTC

Continued clean `210bbb2d` from HANDOFF and plan 00/24/25. The preceding work
was committed; no interrupted native draft or `codex-wip-20260907` remained.
Startup checked 80 Linux process identities and 355 Windows CIM identities,
with no Claude executable, Node CLI or loop found. No browser/runtime file changed.

## Changes

`346d69e0` extends the existing constructor publication proof to String keys
supplied by every exact construction. A pure direct or single-slot holder helper
may forward `(key, receiver)` to its captured Map. The key keeps its original
evaluation. Publication moves immediately after each completed construction,
using that construction's own literal. The existing whole-source, Map identity,
observer and concrete record-owner checks remain required.

An unconstructed base may defer its key parameter only under the existing
one-base/one-leaf restrictions. Super normalization substitutes the actual
arguments into the leaf; every exact leaf construction must then prove its key.
Unconsumed deferred publication refuses the complete candidate. Four new native
bodies cover distinct keys, replacement/deletion with saved owners, reordered
constructor/helper parameter positions and a keyed base holder followed by
literal leaf writes. Ten new complete-source refusals and a first-complete
budget/rollback control protect the boundary.

`c6ea7670` admits explicit optional String identity and reuses existing
branch String extraction for `typeof` String/undefined and equality with
undefined. Only the same saved description in the proved String arm may call
`charAt(0)` or `slice(1)`. The final export call filter now accepts those two
existing operations. Undefined stays distinct from empty String and DOM null.
Three source bodies cover direct guards, negation/reversed equality and immutable
captures. Missing String identity, wrong arms, different reads, uses after the
guard, prototype writes and coercion remain refused. Shared refinement lowering
selects
the existing checked `global_string` accessor for tagged native Strings and
retains `.value()` for DOM `std::optional<std::string>`. No runtime implementation
changed.

Parallel agents supplied source fixtures, the intrinsic draft and an initial
read-only publication review. Both implementation agents hit service limits;
root recovered their frozen drafts, completed the final intrinsic call filter
and selected the correct String carrier extraction before completing the
serialized devbox gates.

## Focused validation

All compilation and native execution ran on the devbox under the shared lock.
Explicit targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract` and `ctcompile-test-native-reference`.

The initial build found a const generated-op accessor error in the new
publication loop, corrected before the successful five-step rebuild. A later
seven-step rebuild passed. Updating the intrinsic provider header comment and
final call filter rebuilt 82 steps successfully. Exact `ctcompile_host_contract`
passed on all three selected runs; the final run was 1/1 in 0.57 s, 0.58 s total.
The three new description sources pass a separate proof/lowering probe in both
optimization modes. After the carrier correction, a three-step rebuild and
**24 focused native executions** pass with GCC/Clang and explicit/deduced output.

The class fixture measures **70 source observations, 152 main native executions,
140 unprepared refusals and 81 preparation refusals**. The four new admissions
add **32 native executions**. Its keyed-holder first-complete budget is **1201**;
existing holder/helper budgets are 781, 904, 802 and 679. Auxiliary controls retain
16 constructed-method executions/20 refusals and eight original-helper
executions/four refusals. The terminal fixture retains 58 observations and 160
main executions; DOM Symbols retains 40 executions/42 refusals/one mutation.

The final selected run passes **2/2 in 249.49 s**: intrinsic exports in 249.48 s
and DOM Strings in 177.04 s. Intrinsic exports measure **304 native executions,
278 refusals and two mutations**, with **46 Node/VM observations**. The three
new admissions add 24 main executions, separate from the 24-execution probe.
DOM Strings checks **809 Node/VM observations and eight GCC/Clang binaries**,
plus 1000 source refusals, 44 provenance/depth, 24 method, 241 capture,
101 replacement, 22 branch-depth and 31 completion provenance/budget checks.
Its replacement adjunct retains 11 observations and four native executions.
All **14 final code/test SHA-256 hashes** match the devbox.

Five distinct lit cases passed across the corrected selections:

| Case under CTNative/ | Final result |
| --- | --- |
| Lowering/Objects/class-terminal-publication.test | PASS, 141.30 s |
| Lowering/Objects/class-captured-map-helpers.test | PASS, 136.87 s |
| Browser/native-dom-symbols.test | PASS, 76.48 s |
| Exports/native-intrinsic-symbols.test | PASS, 249.48 s |
| Browser/native-dom-strings.test | PASS, 177.04 s |

The initial combined selection passed only DOM Symbols: 1/3 in 225.42 s.
The class failure was an incorrect expectation for the original file-30
dynamic-key body: its key is a captured outer parameter, not a constructor
argument. The source remains unchanged and explicitly refused. The corrected
class fixture passed 1/1 in 136.88 s. The intrinsic failure exposed the separate
final Symbol-only call filter, corrected as described above. A subsequent run
failed after 203.26 s because generated tagged String extraction called a data
member as `.value()`. Root traced every refinement caller and selected the existing
accessor by actual carrier type, then added the affected DOM String fixture to
the focused selection. The adjacent terminal-publication fixture passed 1/1 in 141.31 s.

Required repository formatting retains 16 existing diagnostics in untouched
`ctbrowser/tools/ctdrive/ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`. Eight C++ and four Python
files pass scoped formatting, and Python syntax and whitespace checks pass.
The five original fixture files 30/33/35/36/37 remain byte-identical; all 14 new
Node observations pass independently. Full suites, broad corpus/native matrices,
full Bootstrap, WPT/test262, Windows and new sanitizers were skipped. No local
build or push.

Evidence: `/tmp/ctcompile-keyed-build{1,2}.log`,
`/tmp/ctcompile-keyed-gate{3,4,5,6,7}.log`, their lit JSON files,
`/tmp/ctcompile-keyed-evidence-final.log` and `/tmp/ctcompile-keyed-local.sha256`.

## Exact next boundary

Compose the original Data holder's `set/get/remove` slots and its three-argument
registration with conditional outer/nested Map creation, conflict reporting,
nullable lookup and child/empty-parent cleanup. Preserve the complete B/Data+B
bodies, including `e.set`, `e.remove`, `P.off`, configuration and disposal.
Every concrete owner, observer, exception and reentry path still needs proof.
The old captured outer-key source remains a separate cell/parameter proof;
computed keys and shared/deeper constructor families remain unsupported.

Broader intrinsic String methods/indices, conditional callees, branch-mutated
boxed locals, String ordering, document views and the application driver remain.
No full-Bootstrap admission or coverage gain is claimed.
