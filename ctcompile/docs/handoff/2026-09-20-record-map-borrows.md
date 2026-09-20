# Native record Map borrows and remainder congruence — 2026-09-20 UTC

Continued clean `eab11ee5` and its completed-record Map preparation boundary.
No dirty predecessor work or unmerged September 7 rescue remained. Parallel
agents handled closure proof, supplemental sources and escape analysis; the
root integrated their drafts, reviewed the representation boundary and ran the
focused devbox gates. Agents later hit rate limits; the root finished the same
claims rather than starting other work.

Linux: 11 readable process identities, no Claude matches, 56 unreadable.
Windows Get-CimInstance: 345 processes, no Claude matches. Availability remained
uncertain, so concurrent restrictions applied. No browser, shared code or
runtime-oracle edits; no push.

## Landed

- `c955af5a` carries completed local class instances in existing native Maps.
  The closure census proves direct entry-block operations, homogeneous completed
  constructors, literal String keys, present reads and confined saved aliases.
  Map operations and constructor effects survive lifting; the final native Map
  analysis independently rederives standard identity and enclosing ownership.
- Closed-shape groups join every stored record and saved get result for schema
  inference. Exact read origins remain separate from schema families. Emission
  uses stack records and `map_storage<std::string, concrete_record *>` through
  the existing Map templates. Saved pointers keep their original targets across
  replacement, deletion and clear; Maps never own the records. No new runtime
  helper, class ownership graph, Script/VM/GC dependency or generic boxed carrier
  was introduced. Existing finite scalar field carriers remain unchanged.
- A live lifted-receiver use selects this representation. Ordinary object
  identity Maps keep their previous path; selected record families cannot also
  acquire identity-object markers. Constructor-time publication remains refused.
- Six unchanged preparation-only sources now execute natively. Three new
  positives cover a shared owner in two Maps, original-owner method mutation,
  and saved aliases across delete/reinsert. Three controls cover a missing read
  after clear and returned/captured aliases. All original class files 01–26
  remain byte-identical.
- `35ceaf8b` keeps the dividend congruence modulo `gcd(stride, |divisor|)` across
  remainder wraps, including signed inputs across zero. Exact replay, complete
  reload checks and work/depth guards remain. All 47 original JavaScript bodies
  are unchanged; 14 new witnesses include eight positives and six retention
  controls. A new coprime test initially stayed in one quotient band; extending
  its allocation made it exercise the intended wrap before the passing gate.
- `2f10fa93` adds `optimize=false` to the conditional RUN in `nested-maps.mlir`
  and `object-keys.mlir`. Precomputation had erased their branches. Source bodies
  and exact expected refusal diagnostics are unchanged.

## Focused validation

Evidence: `/tmp/ctcompile-record-native-1107/`. Every devbox build and check held
`/tmp/ctbrowser-devbox-build.lock`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
  `ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`.
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.50s. Exact
  `ctcompile_host_contract`: **1/1**, 0.48s.
- Lowering lit `object-argument-lift.mlir`, `object-argument-refusals.mlir`,
  `constructor-refusals.mlir`, plus escape lit `remainder-index-overwrite.test`,
  `composed-index-overwrite.test`, `bitand-index-overwrite.test`: **6/6**, 4.24s.
- Adjacent Map lit `maps.mlir`, `map-flow.mlir`, `map-proof.mlir` and
  `object-key-proof.mlir`: **4/4**. The two corrected tests above: **2/2**, 0.19s.
- Selected class driver: **37 observations / 112 main native executions /
  74 unprepared refusals / 47 preparation refusals / zero prepared native
  refusals**. Nine record cases account for **72 new executions**, covering both
  optimization modes, explicit/deduced output and GCC/Clang. Ancillary controls:
  **16 constructed-method executions / 20 refusals** and **eight original-r
  executions / four refusals**, separately counted.
- Four raw input mutations with forged Map proof markers refuse in both modes:
  **8/8**. They cover replaced Map binding, published alias, absent read and
  mixed payload. Added output assertions pass over **36/36 existing generated
  C++ artifacts**; construction preservation passes **9/9**. These final checks
  reuse the execution artifacts, without another compiler matrix replay.
- Source-proof cutoffs: overwrite **622**, helper holder **1464**, empty **218**.
  Existing budget and malformed-input checks remain in the selected driver.
- Existing remainder recording: **183 observed sites / 36 sound / 36 of 41
  confined precision (87.8%)**; zero violations, partial, pending, unobserved
  claims or unclaimed sites. Five imprecise Stored claims remain.
- All **19** tested file hashes match the devbox. Agents checked **61** remainder
  sources and **six** new class sources with Node; the root preserved all old
  class files and checked Python AST/Black, changed C++ formatting and whitespace.
  Completed `tools/format.sh --check` reports **20 existing diagnostics in six
  HEAD-identical paths**; there is no whole-repository formatting pass.

The first build required an explicit `mlir::Value` conversion before setting a
typed operation result's pointer type. The first native probe exposed the
remaining function carrier census; both were fixed before the passing checks.
Review also caught the object-identity representation collision before landing.

Skipped: full CTest/compiler lit, whole class-initialization lit, broad native
matrices/corpus runs, DOM replay, full Bootstrap, WPT/test262, Windows and
sanitizers. Historical browser compliance measurements remain historical.

## Next boundary

Original B/Data+B still reject constructor-time registration of `this`.
Completed local retention now has concrete native record pointers, but partial
publication needs exception/reentry and owner-lifetime proof. Connect that proof
to the original nested Data Maps and helper transport before retaining receivers
during construction. Preserve every conflict check, registration/removal call,
`P.off`, configuration and disposal body.

Current record Maps are direct entry-frame operations with literal String keys
and present gets. Saved-alias method selectors, own-key snapshots, region-local
owners and transported/nested record Maps need further proof. Full Bootstrap
and the application driver remain unfinished.
