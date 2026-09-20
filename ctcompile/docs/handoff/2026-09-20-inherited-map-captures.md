# Inherited Map captures and signed OR/XOR bands — 2026-09-20 UTC

Continued clean `28f2b5a2` and its recorded Data holder/inherited Map boundary.
The September 7 WIP was absent. A service interruption killed the original
delegated agents; replacements finished the existing escape and fixture tasks.
Independent review found no blocking production issue and requested an Object
identity declaration for the early-snapshot control; that control now reaches
its intended construction-time refusal.

Linux checked 15 readable executable/CLI identities with no Claude match, but
56 identities were unreadable. Windows Get-CimInstance checked 348 processes
with no match. Availability remained uncertain; concurrent restrictions applied.
No browser/runtime/shared-file edits or push.

## Landed

- `d4c4a82b`: direct local Map captures may accompany proved heritage.
  Constructor normalization receives the actual base and derived closures.
  Every copied Map read retains its exact source Map identity, reuses a matching
  derived capture slot or appends the base's original immutable cell, and reads
  through the derived callee. Appending requires the base closure to precede the
  derived closure, transferring the already-proved cell initialization order.
  Copied Map provenance replaces erased derived records while preserving base
  records for other leaves. Both ordinary and own-field-snapshot normalization
  paths use this transport. Slot compaction, call-site availability, type and
  ownership admission remain the existing closure/Map lowering's responsibility.
- `adac2c92`: OR/XOR ranges retain the common high-bit prefix and conservatively
  enclose lower varying bits within one ToInt32 band and converted sign half.
  Signed/unsigned integer Number masks preserve signed result endpoints for
  later composition. AND remains unchanged. Exact replay handles actual sparse
  writes; complete reload and later-store checks remain mandatory.

Six new native positives add **48 executions**: copied base captures, different
base/leaf Maps initially occupying the same slot number, one shared Map, a
three-level chain with sibling leaves, ordinary inherited Map methods, and mixed
helper/Map slot compaction. Ten new controls cover early snapshots, cell/global/
prototype/member replacement, unused effects, stored receivers, helper/holder
captures and captured lexical super-method calls. Original fixture files 01–21
are unchanged. The existing numeric-key inherited fixture now prepares, then
refuses the unsupported optional numeric Map-key carrier.

The escape fixture grows from 14 to 28 functions; all original source bodies
are unchanged. Two existing high-band sources now prove confined. New controls
cover negative and unsigned bands, composition, sign/discontinuity refusal,
saved children, gaps, interior extrema and invalidated reloads.

Inspected emitted C++ for distinct Maps, ancestry chains and mixed captures:
stack class records, borrowed receiver pointers, direct functions and the
existing `std::shared_ptr<ctnative::string_to_number_map>` scalar owners.
Distinct owners remain distinct and shared owners pass to the correct functions.
No Script/VM/collector dependency, stored-class graph or cyclic owner was added.
This does not claim borrowed or by-value Map emission.

## Focused validation

All devbox actions held `/tmp/ctbrowser-devbox-build.lock`.
Evidence, scripts, source hashes and inspected C++:
`/tmp/ctcompile-inherited-maps-0900/`.

- Explicit remote-build targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`.
- Exact `ctcompile_host_contract`: **1/1**, 0.48s (0.49s total).
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.45s (1.46s total).
- Lit `^ctcompile :: CTNative/Lowering/Objects/(object-argument-lift|object-argument-refusals|constructor-refusals)[.]mlir$`:
  **3/3**, 4.28s.
- Lit `^ctcompile :: Analysis/Escape/escape-claims/(bitor-xor|bitand|composed|left-shift)-index-overwrite[.]test$`:
  **4/4**, 0.15s.
- OR/XOR oracle: **84 claims / 84 observed sites / 18 sound / 18 of 24 confined
  precision (75%)**; zero violations, partial, pending, unobserved claims or
  unclaimed sites. Six imprecise Stored claims remain.
- Selected class probe: all 34 current/prior Map sources, original B/Data+B,
  empty, captured class name, distinct captured holders, shared inherited fields,
  inherited helper/snapshot path and lexical super argument ordering.
  **42 observations / 136 main native executions / 84 unprepared and 66
  preparation refusals**, plus **six native boundary controls**. GCC/Clang,
  explicit/deduced C++ and both optimization settings are covered.
  New complete-proof budgets: distinct **1,750**, chain **4,326**, mixed captures
  **1,534**. Existing selected budgets: **645 / 1,193 / 529 / 4,161 / 2,735 /
  1,366 / 218 / 515**. Root controls and one-short budgets refuse without output.
- Ancillary controls: **16 constructed-method executions / 20 refusals** and
  **eight original-r executions / four refusals**.
- Early-snapshot diagnostic separately rechecked after pinning:
  `class construction method observes an own-key snapshot`.
- All six native and four escape tested hashes match. Node validates the 16
  new native observations and 28 escape syntax/termination/retention cases.
  Changed C++ formatting, Python syntax/Black and diff whitespace pass.
- Required `tools/format.sh --check`: **20 existing diagnostics in six unchanged
  files**. Changed files pass; no repository-wide clean formatting claim.

The first build found the second normalizeSuper caller in the snapshot path;
both callers now pass exact closures. The first native probe found an old
prepared-only assertion counting compiler-generated super guard Error constructs;
that inherited fixture now checks retained capture reads and the actual native
optional-key refusal. A shell heredoc was consumed during remote building, so
only its build ran; the exact CTest/lit/probe commands were then run explicitly.
An initial report-only oracle command used the wrong recording suffix; reading
the existing `.rec` file produced the measurements above.

Skipped: full CTest/compiler lit, whole class-initialization lit, DOM replay,
broad native/corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers.

## Next boundary

Original B remains `class own-key snapshot constructor observes its receiver`.
Original Data+B remains `class Map capture requires a direct local class` at its
local Data holder/helper environment. Inherited direct Map captures are connected;
the holder/helper call environment is not.

`expandHolders` and captured-helper rewriting currently emit direct calls with
an absent callee and erase consumed captures. Keeping an outer closure reference
alone cannot solve a cross-frame call: existing closure lifting requires each
captured value to be in the call's frame. Multiple synthesized closures targeting
one source function also conflict with its unique-creation rule. Continue with
explicit capture transport/remapping through that local invocation graph, reusing
the existing closure lifter and preserving every original body and actual call.
Do not remove the capture guard without implementing transport.

Typed class-receiver storage across set/get/remove, saved aliases, overwrite/delete
and constructor failure remains separate. Retain original `e.set`, `e.remove`,
`P.off` and complete configuration/disposal bodies. Captured lexical super-method
targets, static captures, optional numeric Map keys, constructor own-callee getters,
variable fields, inherited getter/DOM targets, selectors/events/Popper, full
Bootstrap and the application driver remain unfinished.
