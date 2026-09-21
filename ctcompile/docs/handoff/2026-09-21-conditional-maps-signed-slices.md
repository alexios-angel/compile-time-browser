# Conditional child Maps and signed String slices, 2026-09-21 UTC

Continued clean `9fd8337c` from the recorded conditional child-Map boundary.
No uncommitted native draft or `codex-wip-20260907` branch remained. Linux
executable/script-path census (74 processes) and Windows CIM (350 processes)
found no Claude executable, Node CLI or loop. An initial broad Linux match was
the checking command itself; exact script-argument matching removed it.
No browser, runtime or shared build implementation changed.

Three agents handled source fixtures, independent String bounds and read-only
ownership review. The first reviewer hit a service limit without changes;
the fixture agent completed the review. Root recovered the frozen String draft
after its agent also hit a service limit, and owned all devbox gates and commits.

## Changes

`e7cff7c2` extends finite outer-Map routing through result-free structured `if`
branches whose conditions follow from exact `has`, `size` and `delete` results.
Boolean negation and strict Number/Boolean equality preserve those facts.
Both arms pass the complete use/effect census before either is discarded.
Coercing operators, unknown calls, boxed cells, record/class creation and global
stores are outside these Map-only arms. Literal keys and ordered, local Map
identities remain mandatory; dynamic branches and absent outer gets refuse.

Selected child Maps move into the original owning entry frame. Unselected
allocations disappear; saved children and records retain their original owners
across replacement, deletion and recreation. Reverse branch splicing and filtering
of unselected operations avoid retaining erased SSA producers. Existing cached
cell facts are repaired before outer observations are erased. The unchanged native
record-owner proof still checks all surviving borrows. Work is charged before
mutation, and failure discards the private module.

Three new admissions cover repeated registration, nested then/else routing,
distinct keys and cleanup/recreation. Thirteen full-source refusals cover dynamic
branches/keys, captured observers, outer/child/record escapes, absent keys,
exceptions, reentry, region-owned records and dead-arm escape/coercion.
The repeated-registration witness retains fingerprint, forged-annotation and
budget rollback controls; its first complete budget is **994**. All 41 original
class fixture files, including original Bootstrap controls, remain byte-identical.

`1f315db4` admits signed integer literal String `slice` bounds with magnitudes
at most `4294967295`. A source unary minus must wrap one bounded literal and
have only direct slice-bound uses. The existing UTF-16 lowering clamps the
unsigned magnitude before subtracting from the String length. Negative zero,
mixed signs, reversed and extreme bounds preserve JavaScript behavior without
signed length conversions. Negative `charAt` remains refused; exact direct
`charAt(0)` and one-argument `slice(1)` retain their original proof authority.

Four new export admissions include the two complete former negative-bound
refusal sources unchanged, Unicode/NUL/extreme bounds and captured Symbol
descriptions. Fourteen signed-bound refusal bodies, missing-String contracts,
low budgets and stale fingerprints accompany them. Prior positive source bodies
and the initial refusal corpus remain intact. Native/Node Unicode expectations
remain authoritative; five known VM casing/indexing differences are explicit.

## Focused validation

| Check | Result |
| --- | --- |
| Final exact CTest `ctcompile_host_contract` | 1/1, 0.58 s; 0.59 s total |
| `CTNative/Lowering/Objects/class-captured-map-helpers.test` | PASS, 283.54 s |
| `CTNative/Lowering/Objects/class-terminal-publication.test` | PASS, 143.21 s |
| `CTNative/Exports/native-intrinsic-symbols.test` | PASS, 380.93 s |
| `CTNative/Browser/native-dom-strings.test` | PASS, 214.05 s |

The final three-case selection passes **3/3 in 380.94 s**. Captured class checks
measure **122 source observations, 272 main native executions, 244 unprepared
refusals and 155 preparation refusals**. The three new admissions add 24 native
executions. The adjacent terminal fixture measures 58 observations, 160 main
executions, 116 unprepared refusals and 70 preparation refusals. Both also run
constructed-method controls (16 executions/20 refusals) and original helper
controls (eight executions/four refusals), separate from their main counts.

Intrinsic exports measure **408 native executions, 430 refusals and two mutations**,
with **59 Node/VM agreements and five known differences**. Four new admissions
add 32 native executions. DOM Strings measure **809 Node/VM observations and
eight GCC/Clang binaries**, plus 1,000 source refusals, 44 provenance/depth
refusals, 24 method provenance checks, 241 capture provenance/budget checks,
101 replacement checks, 22 branch checks and 31 completion checks. Its replacement
subcheck also runs 11 observations and four native executions.

All builds and native executions ran on the devbox under the shared lock.
Explicit targets were `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`
and `ctcompile-test-native-reference`. The first build rejected `llvm::erase_if`
on a DenseSet; explicit iterator-safe erasure fixed it. The next five-step build
passed. Initial class selection passed 1/2 in 143.22 s: the new dead-escape fixture
needed the VM's additional `retained=undefined` output in its expectation. Its
source was unchanged. Review then restricted erased-arm operators and added
the dead-coercion refusal and nested else control before the final seven-step
build and successful three-case selection. Initial exact CTest also passed
1/1 in 0.56 s (0.57 s total).

All **eight final code/test SHA-256 hashes** match the devbox. Three C++ and four
Python files pass scoped formatting; Python syntax, whitespace checks, all 16
new Node observations and 41 original class source preservation checks pass.
Required `tools/format.sh --check` retains **16 existing diagnostics** in untouched
`ctbrowser/tools/ctdrive/ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.
Full CTest/compiler lit, broad corpus/native matrices, full Bootstrap, WPT/test262,
Windows, new sanitizers, local native builds and push were skipped.

Evidence uses `/tmp/ctcompile-conditional`: `-build1.log`, `-gate2.log`,
`-class.log`, `-class.json`, `-gate3.log`, `-final.json`, `-evidence.log`,
`-local.sha256`, `-format.log` and `-format-final.log`.

## Exact next boundary

Compose captured Data helper/constructor calls with the selected child origins.
The original `t.has(e) || t.set(e, new Map)` also needs result-carrying
short-circuit correspondence beyond this result-free branch proof. Original
`Data.set(element, componentKey, this)` requires both key dimensions, conflict
reporting, nullable lookup and child/empty-parent cleanup, with complete owner,
observer, exception and reentry proofs. Original B/Data+B remains refused with
configuration, disposal and full helper bodies intact. No full-Bootstrap admission
or coverage gain is claimed.

Negative `charAt`, dynamic/coercing String indices, broader String methods,
conditional callees, branch-mutated boxed locals, String ordering, document views
and the application driver remain separate work.
