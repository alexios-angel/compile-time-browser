# Inherited getters and signed remainder bounds — 2026-09-20 UTC

Continued clean `36352995` and its B/Data+B retained-receiver boundary.
`codex-wip-20260907` is already an ancestor of `ctcompile-v1`. No old draft or
unmerged rescue remained. Parallel agents investigated typed Map payloads,
prepared getter fixtures and extended remainder proofs. Service interruptions
left the latter two drafts incomplete; the root finished the same files and
ran the focused gates.

Linux checked 12 readable executable/CLI identities with no Claude match and
56 unreadable identities. Windows Get-CimInstance checked 344 processes with
no match. Availability stayed uncertain; concurrent restrictions applied.
No browser/runtime-oracle/shared-file changes or push.

## Landed

- `ddf01d98`: class preparation carries the exact already-proved static getter
  environment through inheritance. Shared instance methods, copied constructor
  bodies, direct static reads and static methods retain the same getter target
  and transitive dependencies. The accessor definition itself must have passed
  the original proof. Repeated receiver censuses charge their expansion cost;
  rewriting consumes each shared read once.
- Four unchanged sources now execute natively: `inherited-method-getter`,
  `inherited-own-fields-iterate-getter-inherited`, `override-unused-constructor`
  and `inherited-method-unused-constructor`. Seven new positives cover constant
  and dependent getters, three levels, construction, shared receivers, fresh
  empty identity and inherited static reads. Together they add **88 native
  executions**. Original source files 01–24 are unchanged.
- Derived own getters, changed transitive dependencies, static-method/getter
  collisions, receiver shadowing, prototype mutation and unused ambient getters
  remain refused. This does not specialize a shared getter body for different
  leaf targets. Original B/Data+B still report receiver observation at registration.
- `d121e11b`: bounded signed Number dividends and nonzero signed Number divisors
  receive conservative remainder enclosures. Separate positive and negative
  magnitudes avoid signed absolute-value overflow. Exact replay still decides
  actual writes; final indices must be valid own indices. Saved aliases, gaps,
  interior divisor reloads, later writes, Number-only inputs, depth and work
  limits retain their checks. Two original source expectations are promoted;
  all 21 original JavaScript bodies are unchanged. Twelve new source cases cover
  both signs, zero crossing, negation, saved divisors and refusal boundaries.

Inspected four generated files: stack class records, borrowed receiver pointers
and direct functions. Fresh empty getter results use the existing identity-object
owners. No Script/VM/GC dependency or new class ownership graph was introduced.

## Focused validation

Evidence and scripts: `/tmp/ctcompile-inherited-getters-1011/`. All devbox actions
held `/tmp/ctbrowser-devbox-build.lock`.

- Explicit remote targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`.
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.48s (1.49s total).
- Exact `ctcompile_host_contract`: **1/1**, 0.48s (0.49s total).
- Escape lit `remainder-index-overwrite.test`, `bitand-index-overwrite.test`
  and `composed-index-overwrite.test`: **3/3**, 0.14s.
- Lowering lit `object-argument-lift.mlir`, `object-argument-refusals.mlir`
  and `constructor-refusals.mlir`: **3/3**, 4.26s.
- Two disjoint class selections, 39 sources plus one added static-read source:
  **40 observations / 144 main native executions / 80 unprepared refusals /
  49 preparation refusals**. GCC/Clang, explicit/deduced C++ and both optimization
  settings are covered. Each driver invocation also passed its ancillary
  **16 constructed-method executions / 20 refusals** and
  **eight original-r executions / four refusals**; these are separate repeated
  controls, not new main-source coverage.
- Remainder oracle: **99 claims / 99 observed sites / 18 sound / 18 of 23
  confined precision (78.3%)**. Zero violations, partial, pending, unobserved
  claims or unclaimed sites; five imprecise Stored claims remain.
- All eleven final tested file hashes match. Node checks cover 13 getter source
  observations and 33 remainder source syntax/termination/result-shape checks.
- Completed required `tools/format.sh --check`: **20 existing diagnostics in
  six HEAD-identical files**. Changed C++ dry-run formatting, Python Black and
  diff whitespace checks pass. This is not a repository-wide formatting pass.

The first build caught a duplicate local name in the interrupted escape test
draft. It was renamed before the final build. The first native probe passed
the promoted old sources, then found that moving the derived-getter refusal
earlier changed Bootstrap's expected diagnostic. Restoring the original proof
order retained the receiver-storage boundary; the final selection passed.

Skipped: full CTest/compiler lit, whole class-initialization lit, DOM replay,
broad native/corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers.
Historical browser compliance measurements were not changed or remeasured.

## Next boundary

Original B/Data+B still refuse `class own-key snapshot constructor observes its
receiver` at `e.set(..., this)`. The getter work does not grant retained receiver
ownership. The smallest next executable slice is **post-construction registration
in a local Map**, before attempting partial-constructor publication:

```js
const first = new Item(7), second = new Item(9);
map.set("item", first);
const saved = map.get("item");
map.set("item", second);
map.delete("item");
return saved.value; // 7
```

Prove entry-block record owners and Map lifetime within one enclosing function,
complete synchronous helper calls/producers/uses, a homogeneous record family,
present reads, and no escaping Map or aliases. Saved reads must copy the record
pointer, so overwrite/delete cannot invalidate them. Keep region-owned records,
nullable receiver reads and cross-call owner ambiguity refused initially.

The existing runtime templates already accept `map_storage<K, ctn_record *>`;
present reads need no new runtime helper. The missing compiler connections are:

1. Class source and closure-lifting receiver proofs must consume exact Map
   retention evidence while keeping constructor-publication guards.
2. `Analysis/NativeMap.cpp` and `NativeMap/Presence.cpp` must expose complete
   payload-family/origin edges. Presence already tracks saved origins across
   overwrite/delete. Schemas and actual object identities must remain distinct.
3. `Inference/Shapes.cpp` receiver families and field-presence proofs must join
   the retained records and get aliases. Closed records currently bypass the
   type lattice; `TypeInference::mapTypeOf` otherwise sees a boxed payload.
4. Map admission, `LoweringSupport.cpp` and EmitC Maps/Shapes/Types must agree
   on the concrete record pointer spelling and address conversion.

Only afterward prove constructor registration cannot expose a partial receiver
through exceptions or reentry, then extend to the original nested Data Maps.
Preserve the conflict branch, `e.set`, `e.remove`, `P.off` and all configuration
and disposal bodies. Scalar identity storage or shared class ownership is not
a substitute. Getter overrides, static construction, inherited DOM targets,
selectors/events/Popper, full Bootstrap and the application driver remain.
