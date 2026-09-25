# Captured Map helpers and global intrinsic helpers, 2026-09-21 UTC

Continued clean `fb5b8c04` from its captured/nested Data Map handoff.
The previous iteration committed its work; there was no dirty predecessor draft
or unmerged `codex-wip-20260907`. Startup inspected 69 Linux cmdline/comm
identities without failures and 355 Windows CIM process identities, with no
matching Claude executable, Node CLI or loop. No browser or shared files changed.

## Landed

- `de86d5ba` normalizes exact local helpers capturing one fixed Map before
  examining class setup. Each original call must be in the Map's entry block,
  after both allocation and capture-cell initialization. Dynamic calls and
  resolver-produced direct calls retain exact callee, target, receiver,
  new-target and argument checks. Bodies contain only constants, captured Map
  reads/calls, returns and bookkeeping. Complete closure, binding-cell and symbol
  censuses precede charged expansion at the original call positions.
  Only a fully retired helper discharges its hoisted capture obligation.
  Existing class Map identity, present-read and concrete stack-owner proofs then
  run unchanged. No record ownership crosses a frame or a nested Map.
- `fdf79a61` admits exact uncaptured global intrinsic helpers. Original numeric
  closure identities, enclosures and unique declarations must agree with the
  complete global store/load census. Existing closed-declaration and inert-wrapper
  proofs precede private helper expansion and full typed entry reproof.
  Global-to-global chains, repeated calls with different primitive kinds,
  argument evaluation order, Symbol identity and description absence remain
  observable. The former global-helper refusal source now executes unchanged.
  Captures, global helpers containing local declarations, mutation, escaping
  functions, unknown globals, recursion and wrapper effects remain refused.

Parallel agents implemented global helpers and Map fixtures while a third
audited the owner seam. That audit confirmed that widening only
`NativeMap.cpp::proveRecords` would be unsound: payload/origin lookup, inference,
object identity and emission also rely on direct entry-block owners.
The audit agent then hit a service limit. A separate final review of the new
normalizer found no concrete correctness blocker. Root reviewed, completed,
validated and committed both changes.

## Focused validation

All compilation and execution ran on the devbox under the shared build lock.
Explicit targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract` and `ctcompile-test-native-reference`.
The final fixture-only sync rebuilt the optimizer, translator and reference
targets with no work. No full-suite target was used.

Exact CTest `ctcompile_host_contract` passed after the corrected code builds;
the last run was **1/1 in 0.57 s**, total 0.58 s.
Four distinct selected lit cases passed across corrected runs:

| Case under CTNative/ | Result |
| --- | --- |
| Lowering/Objects/class-terminal-publication.test | Passed in the initial two-case selection. |
| Lowering/Objects/class-captured-map-helpers.test | Final **1/1 in 61.43 s**. |
| Exports/native-intrinsic-symbols.test | Passed with adjacent DOM Symbols, **2/2 in 159.44 s**. |
| Browser/native-dom-symbols.test | Passed in that same two-case selection. |

The first 11-step build stopped on a TypedValue/Value initializer-list deduction
error in the new normalizer; an explicit Value conversion fixed it.
The initial class selection was **1/2 in 139.90 s** because local source calls
had already become CallDirectOp. Supporting those exact original calls fixed
the implementation without changing the source witness.
The next new-case run reached all four new native positives, then failed its
last refusal fixture's VM oracle (**0/1 in 31.17 s**). The new method
`read() { return read().n; }` calls the outer helper in Node (7), but recursively
calls itself in the VM (stack exhausted). Renaming only that new method to
`inspect` isolates the intended returned-class lifetime refusal.
No VM behavior or pre-existing source body was changed.

Captured Map checks cover **23 source observations**, seven native-positive
bodies and **56 main native executions** across GCC/Clang, explicit/deduced
printing and both optimization settings. Four new admissions add **32 executions**:
captured set/get/size, a reader after terminal constructor publication, saved
record aliases across overwrite/delete, and two distinct Maps with equal keys.
Seven new refusal bodies cover escaping helpers, an alias observer, region calls,
read-after-delete, dynamic keys, mixed payloads and returned class ownership.
The direct helper also runs the existing first-complete budget/rollback check.
Original B/Data+B and representative existing owner/publication controls remain.

Intrinsic exports check **200 native executions, 200 refusals and two mutations**,
with **33 Node/VM observations**. Four new entries add 32 executions.
Eighteen new refusal sources run in both optimization modes; three budget
controls and a stale-fingerprint mutation remain. Generated artifacts retain
the existing Core-only link and no-Script/no-DOM gates.

All **11 final code/test SHA-256 hashes** match the devbox. Four changed C++ files
and four Python files pass scoped formatting; whitespace and source-preservation
checks pass. Required `tools/format.sh --check` retains **16 existing diagnostics**
in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`HostContract/ProviderPaths.h`, `PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.
It stops before repository-wide Python/web formatting. An earlier formatter
run during agent edits also saw three temporary DOMPreparation diagnostics;
the frozen rerun contains only the existing 16.

Skipped: full CTest/compiler lit, broad corpus/native matrices, full Bootstrap,
WPT/test262, Windows and new sanitizers. No local build or push.
Evidence: `/tmp/ctcompile-captured-helpers-{build1,gate2,gate3,intrinsics4,gate5,format-final}.log`
and `/tmp/ctcompile-captured-helpers{,-remote}.sha256`.

## Exact next boundary

Original B/Data+B still refuses at `class own-key snapshot constructor observes
its receiver`. Its constructor calls through the captured Data holder before
receiver initialization is complete; the holder includes conditional child Map
creation, conflicts, nullable gets, deletion and empty-parent cleanup.
Preserve the complete `e.set`, `e.remove`, `P.off`, configuration and disposal
bodies. This session establishes only exact entry-called one-Map helper transport.

The next bounded seam is exact captured holder/helper calls at their original
positions, then construction-time publication through those calls. Reuse existing
callable-holder and captured Map origin proofs, but preserve every observer and
exception/reentry path before moving publication. General nested record storage
still needs concrete owners for every origin, alias and nullable lookup; a Map
schema-family marker is never identity or lifetime evidence. Keep the current
direct `nativeMapRecordPayload`/`nativeMapRecordOrigin` assumptions until all
their consumers can discharge that wider proof.

File 34's coercion/read/helper limits, shared/deeper class families and String
fields remain. Captured intrinsic helpers, global helpers with local declarations,
description String-method narrowing, registry/keyed fields and custom hooks are
separate work. String ordering, document views and the application driver remain
unfinished. No full-Bootstrap admission or coverage gain is claimed.
