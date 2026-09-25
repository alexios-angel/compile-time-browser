# Helper Map environments and signed AND masks — 2026-09-20 UTC

Continued clean `7fd1a499` and its recorded local Data holder/helper Map boundary.
The September 7 WIP was absent; no uncommitted native draft needed rescue.
Read both agents' histories, including the browser Shell/Script splits. The first
build passed. Service rate limits interrupted the read-only review and fixture
agents; both resumed their bounded tasks. The escape agent completed separately.

Linux checked 68 executable/CLI identities with no Claude match, but 56 identities
were unreadable. Windows Get-CimInstance checked 342 processes with no match.
Availability remained uncertain; concurrent restrictions applied throughout.
No browser/runtime/shared-file edits or push.

## Landed

- `a95574e6`: fixed local Map captures may accompany captured sibling helpers
  and local callable holders. After the complete original body/call proof,
  `transportHelperMaps` computes transitive Map needs. Helpers receive trailing
  ordinary parameters; actual class constructor/method closures retain matching
  immutable Map cells. Each direct call receives values from its own frame.
  Original user arguments are padded before Map arguments. No helper closure is
  synthesized, duplicated or imported across a frame boundary.
- The helper census now includes ordinary calls through already-proved fixed
  local cells. Every original helper value use and both symbol-use locations
  must belong to the exact call/capture/holder census before a signature changes.
  Extra arguments, external identity, additional symbol references and static
  transitive Map captures refuse. Class capture additions check Map/cell
  initialization order; existing closure/type/Map ownership admission remains.
  Original helper frames and roots survive; no inlining is needed.
- `ed61b17a`: signed/high-bit AND masks reuse the existing ToInt32 conversion-band
  and common-prefix enclosure. Low-bit masks retain their universal bound.
  The resulting signed interval still needs a nonnegative own-index proof after
  composition. Exact sparse writes and the complete invariant-reload/later-store
  census remain unchanged.

Seven new native positives add 56 executions: missing-argument constructor helper,
Data-style set/get/delete holder, entry plus two-class shared calls, distinct Maps
with mixed captures, a transitive chain, inherited helper callers and a loop.
Two original inherited helper/holder sources add 16 more executions: **72 newly
enabled executions** total. Original fixture files 01–22 are byte-identical.
Original numeric nested-holder and new numeric-helper sources now prepare and
retain the optional numeric-key native refusal.

Nineteen new source cases include cell/helper/holder/Map-member/prototype changes,
unused ambient effects, retained receiver, helper publication, static transport,
extra arguments and the numeric-key boundary. A dedicated same-named method/helper
case records **Node 8 / VM recursion throw** and remains refused. Positive witnesses
use distinct helper names; the runtime was not changed to match native output.
A source module-symbol control refuses, and a helper-root control checks original
frame/root preservation. Complete-proof and one-short budgets remain tested.

The escape fixture grows from 16 to 32 functions; all original source bodies are
unchanged. Its original negative-mask case is promoted. Eight new positive sources
and eight boundary/retention controls cover signed and unsigned bands, composition,
conversion discontinuities, negative own keys, saved children, gaps, interior
reloads and later invalidating stores.

Inspected four generated C++ files: stack class records, borrowed receiver
pointers, direct helper functions and existing
`std::shared_ptr<ctnative::string_to_number_map>` scalar owners. Different Maps
remain different allocations; shared Maps reach all corresponding callers.
No Script/VM/collector dependency, stored-class graph or cyclic owner was added.
This does not claim borrowed or by-value Map emission.

## Focused validation

All devbox actions held `/tmp/ctbrowser-devbox-build.lock`.
Evidence, scripts, source hashes and inspected C++:
`/tmp/ctcompile-helper-maps-0926/`.

- Explicit remote-build targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`.
- Final exact `ctcompile_host_contract`: **1/1**, 0.49s (0.50s total).
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.48s (1.49s total).
- Final lit `^ctcompile :: CTNative/Lowering/Objects/(object-argument-lift|object-argument-refusals|constructor-refusals)[.]mlir$`:
  **3/3**, 4.24s.
- Lit `^ctcompile :: Analysis/Escape/escape-claims/(bitand|bitor-xor|composed)-index-overwrite[.]test$`:
  **3/3**, 0.14s.
- AND oracle: **96 claims / 96 observed sites / 18 sound / 18 of 24 confined
  precision (75%)**; zero violations, partial, pending, unobserved claims or
  unclaimed sites. Six imprecise Stored claims remain.
- Selected class probe: current/prior Map sources, original B/Data+B, empty,
  captured class name, distinct captured holders, shared inherited fields,
  inherited helper/snapshot path and lexical super argument ordering.
  **61 observations / 208 main native executions / 122 unprepared and 90
  preparation refusals**, plus **ten native boundary controls**. GCC/Clang,
  explicit/deduced C++ and both optimization settings are covered.
- New complete-proof budgets: constructor **638**, holder **1,464**, shared
  callers **1,367**, helper chain **1,021**. Other selected budgets:
  **1,759 / 4,345 / 1,543 / 652 / 1,207 / 535 / 4,161 / 2,735 / 1,366 / 218 / 515**.
- Ancillary controls: **16 constructed-method executions / 20 refusals** and
  **eight original-r executions / four refusals**.
- All seven native and four escape final tested hashes match. Node validates
  the 19 new native observations and 32 escape syntax/termination/retention cases.
  Changed C++ formatting, Python AST/Black and diff whitespace pass.
- Required `tools/format.sh --check`: **20 existing diagnostics in six unchanged
  files**. Changed files pass; no repository-wide clean formatting claim.

Early focused runs exposed the VM name collision and the omitted entry-local
fixed-cell helper caller. Both caller recording and transport now resolve that
same proved identity. A root-control result initially replaced the unrooted
baseline used by the budget comparison; the independent root control now leaves
that baseline intact. Final selected checks above ran after those corrections.
Independent read-only review found no unresolved blocking defect.

Skipped: full CTest/compiler lit, whole class-initialization lit, DOM replay,
broad native/corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers.
Historical WPT/test262 measurements were not changed or remeasured.

## Next boundary

Original B and original Data+B both now refuse
`class own-key snapshot constructor observes its receiver`. Data+B's previous
local holder/helper Map-capture refusal is gone. Its complete original bodies
are still present, including registration at
`e.set(this._element, this.constructor.DATA_KEY, this)`.

A read-only borrowed receiver does not permit retention in Data's nested Maps.
Prove the actual constructor/dispose invocation graph and a typed class payload
with ownership/lifetime across set/get/remove, saved aliases, overwrite/delete
and failure. Preserve nested `new Map`, the conflict/error branch, original
`e.remove`, `P.off`, and all configuration/disposal effects. Do not silence
uncalled bodies or replace the registry with a synthetic scalar witness.

Captured lexical super-method targets, static captures, optional numeric Map
keys, constructor own-callee getters, variable fields, inherited getter/DOM
targets, selectors/events/Popper, full Bootstrap and the application driver remain.
