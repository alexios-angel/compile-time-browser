# Receiver forwarding and masked indices — 2026-09-20 UTC

Continued clean `c894c308` and its recorded Data/Map registration boundary.
No dirty interrupted draft remained; the September 7 WIP was absent. Independent
agents investigated the Map boundary, drafted escape analysis and wrote native
fixtures. Two agents hit a service rate limit; root preserved and completed the
escape draft, and the investigation agent completed the native fixtures.

Linux executable/CLI checks found no Claude identity in 18 readable processes,
but 57 executable identities were unreadable. Windows Get-CimInstance checked
345 processes with no Claude match. Availability remained uncertain; concurrent
restrictions applied. No browser/runtime/shared-file edits or push.

## Landed

- `52cc51db`: `borrowedHelperReads` recursively checks explicit receiver
  arguments through the same exact helper-target, implicit-receiver and formal
  checks. Every collected leaf field is checked at the original construction
  point. Existing lowering already proves transitive borrowed parameters with
  its shrinking argument fixpoint, so no new owner or emitter was needed.
  The work budget and 64-level depth limit withhold incomplete/cyclic proofs.
  Returning, storing or writing the receiver, dynamic keys and unknown effects
  remain refused. All original helper bodies remain in the source census.
- `db4d6384`: BitAnd with an invariant Number mask in `[0, INT32_MAX]` bounds
  the result by `[0, mask]`, independently of input ToInt32 discontinuities.
  Both operand orders work. The dense range supports bounds and reload exclusion;
  ordinary replay alone updates actual elements. Negative/fractional/non-Number
  masks, unknown operands, insufficient own bounds and overlapping reloads refuse.

Five new native positives add **40 executions**: global/captured/holder chains,
reordered and repeated formals, inherited construction and argument order.
Seven controls cover deeper missing fields, retained/returned/mutated receivers,
dynamic keys, finite source recursion and unused ambient effects. Original
fixtures 01–19, including the returning `borrow-forward` control, are unchanged.
The new escape fixture contains 16 functions; prior oracle sources are unchanged.
CFG/SCF checks preserve saved children and children in unvisited mask gaps.

Inspected captured/inherited C++ uses stack class records, borrowed pointers,
direct helper calls and field assignments. Neither sample has Script/VM symbols,
a heap owner, collector, closure environment or receiver table.

## Focused validation

Every devbox command held `/tmp/ctbrowser-devbox-build.lock`. Logs, scripts,
hashes and generated samples are in `/tmp/ctcompile-forwarded-receivers-0812/`.

- Built explicit targets `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
  `ctcompile-test-type-oracle` through `tools/remote-build.sh`.
- Exact `ctcompile_host_contract`: **1/1**, 0.49s (0.50s total).
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.42s (1.43s total).
- Lit filter `^ctcompile :: CTNative/Lowering/Objects/(object-argument-lift|object-argument-refusals|constructor-refusals)[.]mlir$`:
  **3/3**, 4.20s.
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(bitand|composed|left-shift)-index-overwrite[.]test$`:
  **3/3**, 0.15s. The new mask recording/claims report **48 observed sites / eight
  sound / eight of 12 confined precision (66.7%)**, zero violations, partial,
  pending or unclaimed sites. Metrics were read from this focused recording.
- Selected class probe: all 12 forwarding sources, all 19 previous borrow
  sources, 18 `OWN_FIELDS` cases, three captured-holder controls, empty and
  original B/Data+B. **55 observations / 160 main native executions / 110
  unprepared and 70 preparation refusals**, plus **12 native boundary controls**.
  GCC/Clang, explicit/deduced C++ and both optimization settings are covered.
  New complete-proof budgets: global chain **2,698**, captured chain **1,848**,
  inherited chain **4,211**. Original budgets remain
  **2,060 / 1,606 / 1,669 / 3,725 / 466 / 520 / 218 / 763 / 1,333**.
  Public symbols, module references, mixed callers, missing identities, partial
  budgets and original super roots pass. Ancillary controls: **16 constructed
  method executions / 20 refusals** and **eight original-r executions / four
  refusals**.
- All five native and four escape final tested source hashes match. Node checks
  cover 12 native observations and all 16 escape functions for syntax/termination,
  with 11 additional retained-child checks. Changed C++ formatting, Python
  AST/Black, unique fixture names and whitespace checks pass.
- Required `tools/format.sh --check`: **20 existing diagnostics in six
  HEAD-identical files**. Changed files pass. No clean repository-wide formatting
  result is claimed.

The first native probe used a custom-assembly control against generic assembly;
the existing generic-compatible budget check fixed that test setup. The first
arrays run rejected the misspelled `bit_and` test mnemonic; all 22 new CFG/SCF
cases passed after correcting it to `bitand`. No production semantics changed in
response to either test-setup failure.

Skipped: full CTest/compiler lit, whole class-initialization lit, DOM replay,
broad native/corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers.

## Next boundary

The original B diagnostic remains `class own-key snapshot constructor observes
its receiver`. Original Data+B remains `class method capture is not its constructor
or an inert sibling helper`. Forwarding proves borrows, not stored ownership.
Keep all original Data/configuration/disposal bodies and `e.set`, `e.remove`,
`P.off`; a later remove alone cannot prove a receiver lifetime.

Class preparation currently clears all proved capture metadata and erases
callable holders/cells. The general closure lifter already transports captures
for methods and constructors (`whyUpvalueReadsDoNotLift`, `liftedCapture`);
preserve selected Map captures through preparation and reuse that mechanism.
It still needs declared Map identity and a complete original body/invocation
proof before any class setup is consumed. Even scalar Map payloads require this
connection. The local Data holder has no published factory/wrapper/entry chain,
so the owned-global factory proof is not directly applicable.

Actual class-receiver storage additionally needs a typed Map payload and owner
across constructor/dispose calls, saved aliases, overwrites/deletes and constructor
failure. Existing Map leaf proof covers scalar-field objects, not Bootstrap's
receiver with methods and object-valued config. Variable field presence,
inherited getter targets, inherited DOM, static construction, selectors/events/
Popper, full Bootstrap and the application driver remain unfinished.
