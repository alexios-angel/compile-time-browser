# Direct class Map captures and OR/XOR indices — 2026-09-20 UTC

Continued clean `b500dace` and its recorded shared Data Map capture boundary.
The September 7 WIP was absent. Two service interruptions killed delegated
agents; root resumed their existing native and escape drafts, and a replacement
agent finished fixture registration. Independent final proof review found no
new soundness or memory-correctness defect.

Linux executable/CLI checks found no Claude match but included unreadable
identities; Windows Get-CimInstance checked 347 processes with no match.
Availability remained uncertain and concurrent restrictions applied.
No browser/runtime/shared-file edits or push.

## Landed

- `cfc7a229`: class preparation recognizes declared empty Maps in each class's
  local declaration frame. It checks all original root/cell/capture aliases
  and exact `get`, `has`, `set`, `delete`, `clear` and `size` uses. Detached
  members, escaping Maps and observed `set` result aliases remain refused.
  It retains selected capture slots and cells, renumbering surviving reads
  after other captured identities are consumed. The existing closure lifter
  then proves initialization at actual calls; native Map analysis still owns
  representation, complete call flow and ownership admission. Normalized method
  bodies retain their captures. All original methods, including unused bodies,
  remain in the source effect census.
- `5b2e832a`: OR/XOR require integer Number operands in the nonnegative signed-i32
  range and one invariant mask. The upper bound encloses all possible input
  bits; OR additionally retains the mask as its lower bound. Endpoints alone
  would miss interior extrema. Dense enclosures drive bounds and reload checks;
  replay alone updates actual indices, preserving saved children and gaps.

Five class positives add **40 executions**: scalar Map operations, sharing across
classes/instances, distinct Maps, mixed class-getter/Map capture slots and a loop.
Two unchanged numeric field-key cases prepare but refuse native output because
`Map<Opt<Num<i32>>, Num<f64>>` has no carrier. The original mixed-capture constructor
case still refuses its own-callee getter read; the added method-only variant
checks actual capture-slot compaction. The fixture contains 18 sources; original
files 01–20 are byte-identical. The new escape fixture contains 14 functions;
all previous source oracles are unchanged.

Inspected C++ uses stack class records, direct functions and the existing
`std::shared_ptr<ctnative::string_to_number_map>` scalar owners. Distinct Maps stay
distinct. No Script/VM/GC dependency, stored-class graph or cyclic owner was
introduced. This does not claim borrowed or by-value Map emission.

## Focused validation

Every devbox build/test command held `/tmp/ctbrowser-devbox-build.lock`.
Evidence, scripts, source hashes and generated C++ are in
`/tmp/ctcompile-class-maps-0828/`.

- Built explicit targets through `tools/remote-build.sh`: `ctjs-opt`,
  `ctjs-translate`, `ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
  `ctcompile-test-type-oracle`.
- Exact `ctcompile_host_contract`: **1/1**, 0.48s (0.50s final total).
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.46s (1.47s total).
- Lit filter `^ctcompile :: CTNative/Lowering/Objects/(object-argument-lift|object-argument-refusals|constructor-refusals)[.]mlir$`:
  **3/3**, 4.19s.
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(bitor-xor|bitand|composed)-index-overwrite[.]test$`:
  **3/3**, 0.13s. New oracle: **42 observed sites / seven sound / seven of 11
  confined precision (63.6%)**, zero violations, partial, pending or unclaimed.
- Final selected class probe: 18 new sources, 12 previous forwarding sources,
  three captured-holder controls, captured class name, static chain, empty and
  original B/Data+B. **38 observations / 120 main native executions / 76
  unprepared and 68 preparation refusals**, plus **nine native boundary controls**.
  GCC/Clang, explicit/deduced C++ and both optimization settings are covered.
  New complete-proof budgets: mixed method **645**, direct numeric-key boundary
  **1,193**, method loop **529**. Existing selected budgets remain
  **2,698 / 1,848 / 4,211 / 466 / 520 / 218 / 515 / 387**.
  Ancillary controls: **16 constructed-method executions / 20 refusals** and
  **eight original-r executions / four refusals**.
- All nine native and four escape tested hashes match. New native sources have
  Node observations; the 14-function escape source passed Node syntax/execution.
  Changed C++ formatting, Python syntax/Black and diff whitespace pass.
- Required `tools/format.sh --check`: **20 existing diagnostics in six
  HEAD-identical files**. Changed files pass; no repository-wide clean formatting
  result is claimed.

The initial build caught a capture-count narrowing conversion, fixed explicitly.
The first probe caught Map producer discovery inspecting the script entry rather
than the class declaration frame. Later probes established the optional numeric
key and constructor own-callee boundaries; original source bodies were retained
and classified accordingly. The prepared-only test path's old throw-only check
was extended to preserve original Map constructs/capture reads for those cases.

Skipped: full CTest/compiler lit, whole class-initialization lit, DOM replay,
broad native/corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers.

## Next boundary

Original B remains `class own-key snapshot constructor observes its receiver`.
Original Data+B now declares Map identity and reports `class Map capture requires
a direct local class without heritage`. The primitive direct-class capture path
is connected; the local Data holder and inherited invocation path are not.

Preserve the Map through original holder/helper closures and inherited
constructor/dispose calls without erasing their captured environment. Reuse
existing closure/Map lowering and prove every actual call and original body;
the published owned-global factory seam is still not this local holder.
Typed class-receiver payload ownership across set/get/remove, saved aliases,
overwrite/delete and constructor failure remains separate. Retain original
`e.set`, `e.remove`, `P.off` and complete configuration/disposal bodies.
Optional numeric Map keys, constructor own-callee getter reads, variable field
presence, inherited getter targets/DOM, static construction, selectors/events/
Popper, full Bootstrap and the application driver remain unfinished.
