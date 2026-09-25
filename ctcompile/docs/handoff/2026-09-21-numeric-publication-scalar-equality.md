# Numeric publication and scalar equality, 2026-09-21 UTC

Continued clean `0e96b1dd`, resuming its retained file-33 arithmetic witness.
No dirty predecessor work or unmerged `codex-wip-20260907` was present.
Startup inspected 74 Linux process identities and 351 Windows CIM records,
with no matching Claude executable, CLI or loop. The initial PowerShell quoting
error was corrected; concurrent-agent restrictions were retained conservatively.
No browser or shared build files changed.

## Landed

- `836ea47f` admits unchanged `class-map-inherited-base-arithmetic.js`.
  A sole inherited leaf can complete numeric operations after its base registers
  `this` in a confined Map. Every exact construction must supply a literal Number
  for each formal used by that numeric proof. A bounded source-order scan
  propagates Number through arithmetic, static bitwise/shift operations and
  numeric unary operations. Reads, calls, branches and unknown coercions cannot
  cross the original publication point. Ordinary own-field writes retain their
  existing proof. Registration still occurs immediately after the completed
  construction, with concrete borrowed record pointers and enclosing stack owners.
  Existing family, capture, Map identity and observer checks remain required.
- `ac33706d` admits exact Number/Boolean strict and loose equality in the shared
  typed DOM/intrinsic entry proof. Existing native operators and scalar equality
  helpers supply the behavior; no runtime type or implementation was added.
  Mixed kinds retain JavaScript strict-equality tags and loose Number conversion.
  NaN, signed zero, infinities, parameters, helper calls and Boolean loop state
  remain observable. Object coercion and broader mixed unions remain refused.

Parallel agents implemented numeric fixtures and scalar equality while another
audited the Bootstrap ownership boundary. Two agents hit service limits after
useful analysis or saved edits; root inspected and completed their work.

## Focused validation

All compilation and execution ran on the devbox under the shared build lock.
Explicit initial targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-runtime` and
`ctcompile-test-native-reference`. The seven-step build passed. The fixture-only
follow-up built the optimizer, translator and reference targets with no work.

Exact CTests `ctcompile_host_contract` and `ctcompile_native_runtime` passed
**2/2 in 0.58 s**. Three distinct selected lit cases passed across two runs:

| Case under CTNative/ | Result |
| --- | --- |
| Lowering/Objects/class-terminal-publication.test | Passed in initial selection. |
| Browser/native-dom-symbols.test | Passed in initial selection. |
| Exports/native-intrinsic-symbols.test | Corrected selection **1/1 in 131.96 s**. |

Initial lit was **2/3 in 163.94 s**. All new equality positive executions passed;
the later object-coercion refusal fixture failed its source-entry selector because
the object method adds another source function. Selecting the named `bad` entry,
as existing refusal controls already do, preserves that complete source body.
Only the failed export case was rerun. No compiler proof guard changed for this
fixture correction.

The class fixture checks **58 source observations, 160 main native executions
and 116 unprepared refusals**. Four new admissions add **32 native executions**
across GCC/Clang, explicit/deduced and optimized/runtime modes. New sources check
different literal arguments at two construction sites, saved Map record aliases,
infinity, NaN, negative zero and bitwise operations. Five new refusal bodies
cover a later String caller, Boolean input, object coercion with reentry, field
reads and helper results. Existing observer, exception, Map escape, shared/deeper
family and prepared String-field controls remain. The original arithmetic case
also runs super-root and exact first-complete work-budget/rollback controls.
All original file-30 and file-33 bytes are unchanged.

Intrinsic exports check **168 native executions, 161 refusals and two mutations**,
with **29 Node/VM observations**. Two new entries add 16 executable runs; their
clients exercise six equality input tuples and five helper/loop input tuples.
Twenty new refusal runs keep object, Symbol, String, absence and mixed-join
boundaries explicit. Generated artifacts still link Core without DOM or Script.
The adjacent DOM Symbol fixture retains its existing checks.

All **eight final code/test SHA-256 hashes** match the devbox. Both changed C++
files and all four changed Python files pass scoped formatting; Python syntax,
source preservation and whitespace checks pass. Required `tools/format.sh --check`
retains **16 existing diagnostics** in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`HostContract/ProviderPaths.h`, `PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.
It stops before repository-wide Python/web formatting.

Skipped: full CTest/compiler lit, broad corpus/native matrices, full Bootstrap,
WPT/test262, Windows and new sanitizer runs. No local build or push. Evidence:
`/tmp/ctcompile-publication-numbers-{gate1,gate2,format,hashes}.log` and
`/tmp/ctcompile-publication-numbers.sha256`.

## Exact next boundaries

Original B/Data+B still reaches `class own-key snapshot constructor observes its
receiver`: original registration passes `this` through Data.set, whose captured
nested Maps, conflict branch and observers exceed the terminal helper proof.
`ClassInitialization/Sources.cpp` checks that publication while examining the
ordered fields needed by disposal. Preserve the complete `e.set`, `e.remove`,
`P.off`, configuration and disposal bodies.

The next ownership seam is to compose existing captured-Map origin tracking
(`HostContract/Values/CapturedMap.cpp` and `HostContract/CapturedMapBody/Walk.cpp`) with the
concrete record-owner proof in `Analysis/NativeMap.cpp::proveRecords`. That
proof currently requires one direct entry-block Map, literal String keys,
present reads and enclosing stack owners. A schema-family marker is not Map
identity or lifetime evidence. Captured/nested record transport needs every
origin, alias, nullable lookup, conflict, deletion/empty cleanup and reentry/
exception path proved before those restrictions can widen.

File 34 retains the narrower nonnumber/coercion/read/helper controls; file 33
retains shared/deeper families and prepared String-field admission. Global or
captured intrinsic helpers, description String-method narrowing, registry/keyed
fields and custom hooks remain separate. String ordering, document views and
the application driver remain unfinished. No full-Bootstrap admission or
coverage gain is claimed.
