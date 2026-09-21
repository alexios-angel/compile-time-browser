# Constructor holders and intrinsic captures, 2026-09-21 UTC

Continued clean `1cae9bf5` from HANDOFF and plan 00/24/25. Recent histories and
unmerged branches contained no unfinished ctcompile draft or `codex-wip-20260907`.
Startup checked 74 Linux process identities and 354 Windows CIM identities:
no Claude executable, Node CLI or loop. The Windows check succeeded after an
initial PowerShell quoting error. No browser/shared repository file changed.

## Landed

- `6fcfbbbf` extends exact publication-helper normalization to a fresh captured
  holder with one ordinary own slot. The shared callable census sees the actual
  captured read and receiver call; the slot must precede constructor creation.
  Complete raw cell, alias, helper and symbol-use checks establish exclusive
  transport before the holder retires. The original pure helper body still
  requires one literal-String-key `Map.set` and an undefined return. The existing
  terminal-publication proof and concrete stack-owner checks run afterward.
  Three new native bodies cover direct publication, repeated construction with
  replacement/deletion and saved-owner mutation, and numeric field initialization.
  Fifteen refusals retain escaping/aliased/replaced/late holders, shared constructors,
  extra slots, early publication, throws, receiver/helper effects and Map observers.
- `7fde70e4` replaces the blanket intrinsic-helper capture ban with the
  existing complete immutable-cell and closure proof. Only an exact callee-slot
  LoadUpvalue reaches that proof; original implicit uses, capture writes, object
  premises, global declarations, budgets and refreshed typed proof retain their
  restrictions. Three former capture refusals execute with their source intact.
  Further cases cover all four primitive parameter categories, Symbol identity,
  loops in captured helpers, nested description captures and argument evaluation.

Parallel agents supplied fixtures, intrinsic implementation and a read-only holder
review. Both implementation agents later hit service limits; root recovered their
saved drafts and finished validation. The holder review found no blocker and
recommended retaining the ordinary-key restriction, which root applied. A second
intrinsic review stopped on the service limit; root traced the existing source,
cell/capture, call, fingerprint and typed-proof checks.

## Focused validation

All compilation/execution ran on the devbox under the shared build lock.
Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`
and `ctcompile-test-native-reference`. Initial builds completed four and three
steps; the final C++ formatting sync rebuilt three steps. Subsequent fixture-only
syncs reported no work. Exact `ctcompile_host_contract` passed **1/1, 0.57 s**,
0.58 s total. No local build was run.

Four distinct selected lit cases passed across these runs:

| Case under CTNative/ | Result |
| --- | --- |
| Lowering/Objects/class-terminal-publication.test | PASS, 146.51 s in the initial 2/2 selection. |
| Lowering/Objects/class-captured-map-helpers.test | Final PASS, 127.27 s. |
| Browser/native-dom-symbols.test | PASS, 81.02 s. |
| Exports/native-intrinsic-symbols.test | Final PASS, 216.62 s; selected run 1/1 in 216.63 s. |

The intermediate three-case selection passed **2/3 in 204.53 s**. Its intrinsic
failure exposed the existing conditional-callee boundary: the frontend joins
`make` and `cycle` identities before their call. That source remains an explicit
refusal. A quick admission probe then exposed the existing branch-mutated
boxed-local restriction; that intermediate source remains a refusal control.
Separate positive bodies use exact calls and ordinary local values. The
compiler guards did not change to resolve these test-scope failures. A later run
completed all 280 positive executions but reached an overly strict refusal for
straight-line argument assignment; its original body was restored as a positive,
and only that mistaken extra refusal was removed. The final
probe passed all three new substantive capture bodies in both optimization modes
and refused the original conditional callee in both modes before the selected
intrinsic test was rerun.

The final holder fixture measures **55 source observations, 120 main native
executions, 110 unprepared refusals and 62 preparation refusals**. New admissions
add **24 native executions**. First-complete budget/rollback checks pass at
**780** steps for constructor-holder publication, 904 for entry-holder calls,
802 for the free captured helper and 678 for constructor-helper publication.
Existing auxiliary controls add 16 constructed-method executions/20 refusals and
eight original-helper executions/four refusals. The adjacent terminal fixture
retains 58 observations and 160 main executions.

Intrinsic exports measure **280 native executions, 252 refusals and two mutations** with **43 Node/VM observations**.
The adjacent DOM Symbol test retains 40 executions, 42 refusals and one mutation.
Generated programs retain their no-Script, Core/subsystem-only link checks.
All **eight final code/test SHA-256 hashes** match the devbox. Two C++ and four
Python files pass scoped formatting; Python syntax, whitespace and original
file-30/33/35/36 preservation checks pass. Root also checked all 18 new holder
Node observations locally. Original Bootstrap fixture extraction remains intact.

Required `tools/format.sh --check` retains **16 existing diagnostics** in untouched
`ctbrowser/tools/ctdrive/ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`. It stops before repository
Python/web formatting; scoped Python checks pass. An initial in-flight check also
reported two transient intrinsic draft formatting errors, fixed before final sync.

Skipped: full CTest/compiler lit, broad corpus/native matrices, full Bootstrap,
WPT/test262, Windows and new sanitizers. No browser/runtime edits or push.
Evidence: `/tmp/ctcompile-constructor-holder-{build1,gate2,gate3,evidence}.log`,
`/tmp/ctcompile-capture-gate{4,5,6}.log`, the associated lit JSON files, and
`/tmp/ctcompile-constructor-holder{,-remote}.sha256`.

## Exact next boundary

Original B/Data+B still refuses at `class own-key snapshot constructor observes
its receiver`. Preserve the complete `e.set`, `e.remove`, `P.off`, configuration,
nullable lookup and disposal bodies. The next Data composition needs multiple
holder slots and keyed base-constructor arguments, conditional outer/nested Map
creation, conflict detection and child/empty-parent cleanup. Establish every
concrete owner, observer, partial-initialization, exception and reentry path before
widening `nativeMapRecordPayload` or `nativeMapRecordOrigin` consumers.

Intrinsic conditional callees and branch-mutated boxed locals remain separate
from immutable captures; wrapper/ambient captures still refuse. Description
String-method narrowing, mixed primitive unions, String ordering, document views,
registry/keyed fields, custom hooks and the application driver remain unfinished.
No full-Bootstrap admission or coverage gain is claimed.
