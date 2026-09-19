# Native captured Maps across global publication

## Owning String globals, 2026-09-09

**0e041bba/4b0a1199** extend the exact initialized scalar edge to String and
emit owning per-binding storage with byte-preserving, exact-tag observations.
Actual stored SSA lattices and the complete store census remain authoritative.
The original **3a99e34c/f0a03c19** sources now reach **5/5 native** in both modes
with all eight calls and five observations. **b4505df6** gates three String
method results, including empty and UTF-8/NUL bytes, through owning callables.

**417cd0ac** passes **52 native programs**, **71 typed source probes**, **24
wrong-tag/missing-store mutations**, **19 refusal/exact-repair families**, fresh/
stale forgeries and reruns. The new long-String lifetime runs **128 future calls**
after owner/table release and preserves snapshots across original-global
mutation, independent entry execution and final Map/leaf destruction under
GCC/Clang explicit/deduced output and sanitizers. Budgets **9271/9316** check
**31/32 cutoffs**; all **596 historical helper rows** and **24 source hashes**
remain unchanged.

All six focused proof checks pass across the initial five passes and corrected
owner query (**113.20 seconds**); source/prepared queries each have **93 rows**,
and type inference has **122 rows/80 live edits**. The two corrected expectations
preserve existing Null/Undefined key proofs with no fabricated String edge.
Six lowering lit tests pass in **0.29 seconds**. The warning-free **246-step**
full build passes **511/517 CTests in 1817.89 seconds**, including all **302
Map programs/37 lifetime families**. The only compiler failure is lit **164/165
in 1096.42 seconds** (CTest **1096.48**): an obsolete String-load diagnostic.
**9582188d** preserves its source and pins the remaining unproved call-result
store; the focused rerun passes **1/1 in 0.08 seconds**. The complete corrected
lit rerun passes **165/165 in 1096.14 seconds** (CTest **1096.20**), including
all **302 Map programs/37 lifetime families**. The initial full run plus this
lit-only rerun provides passing results for all **372 compiler checks** and
**512/517 total**; only the five established browser failures remain. All **24**
final code/test hashes match local files, committed HEAD, frozen inputs and
devbox sources. Fresh corpus coverage stays **19/574, 39/4754, 45/7725**,
both modes, zero pruned; exact Bootstrap Data remains **0/7, 0/7, 0/8**.

Next is the measured **88d51f7d** String-field source, still **0/5** against its
**5/5** Number-field repair. All thirteen continuation probes agree on typed
Node/interpreter results. The captured method proof, independent field admission
and owning per-field emission must advance together. Full Bootstrap and direct
platform integration remain unfinished; see `bootstrap-provider-next.md`.

## Detailed sections

The following sections retain the complete original text and measurements.

<a id="definite-boolean-globals-2026-09-09"></a>
- [Definite Boolean globals, 2026-09-09](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#definite-boolean-globals-2026-09-09)
<a id="constant-only-number-globals-2026-09-09"></a>
- [Constant-only Number globals, 2026-09-09](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#constant-only-number-globals-2026-09-09)
<a id="definitely-initialized-scalar-aliases-2026-09-09"></a>
- [Definitely initialized scalar aliases, 2026-09-09](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#definitely-initialized-scalar-aliases-2026-09-09)
<a id="saved-number-global-reads-2026-09-09"></a>
- [Saved Number global reads, 2026-09-09](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#saved-number-global-reads-2026-09-09)
<a id="entry-arithmetic-over-published-number-results-2026-09-09"></a>
- [Entry arithmetic over published Number results, 2026-09-09](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#entry-arithmetic-over-published-number-results-2026-09-09)
<a id="standard-clear-and-arbitrary-key-absence-2026-09-08"></a>
- [Standard clear and arbitrary-key absence, 2026-09-08](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#standard-clear-and-arbitrary-key-absence-2026-09-08)
<a id="definite-absence-after-exact-deletion-2026-09-08"></a>
- [Definite absence after exact deletion, 2026-09-08](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#definite-absence-after-exact-deletion-2026-09-08)
<a id="strict-fresh-object-comparisons-2026-09-08"></a>
- [Strict fresh object comparisons, 2026-09-08](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#strict-fresh-object-comparisons-2026-09-08)
<a id="initialized-local-fields-2026-09-08"></a>
- [Initialized local fields, 2026-09-08](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#initialized-local-fields-2026-09-08)
<a id="measured-possible-alias-join-gate-2026-09-07"></a>
- [Measured possible-alias join gate, 2026-09-07](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#measured-possible-alias-join-gate-2026-09-07)
<a id="preceding-per-key-gate-2026-09-07"></a>
- [Preceding per-key gate, 2026-09-07](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#preceding-per-key-gate-2026-09-07)
<a id="preceding-seeded-result-gate-2026-09-07"></a>
- [Preceding seeded-result gate, 2026-09-07](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#preceding-seeded-result-gate-2026-09-07)
<a id="preceding-result-gate-2026-09-07"></a>
- [Preceding result gate, 2026-09-07](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#preceding-result-gate-2026-09-07)
<a id="preceding-argument-gate-2026-09-07"></a>
- [Preceding argument gate, 2026-09-07](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#preceding-argument-gate-2026-09-07)
<a id="nonempty-size-snapshots-2026-09-08"></a>
- [Nonempty size snapshots, 2026-09-08](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#nonempty-size-snapshots-2026-09-08)
<a id="distinct-key-size-bounds-2026-09-08"></a>
- [Distinct-key size bounds, 2026-09-08](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#distinct-key-size-bounds-2026-09-08)
<a id="homogeneous-boolean-and-string-payloads-2026-09-08"></a>
- [Homogeneous boolean and string payloads, 2026-09-08](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#homogeneous-boolean-and-string-payloads-2026-09-08)
<a id="closed-mixed-map-storage-2026-09-08"></a>
- [Closed mixed Map storage, 2026-09-08](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#closed-mixed-map-storage-2026-09-08)
<a id="saved-scalar-reads-through-writes-2026-09-08"></a>
- [Saved scalar reads through writes, 2026-09-08](native-owned-global-maps/01-definite-boolean-globals-2026-09-09.md#saved-scalar-reads-through-writes-2026-09-08)
<a id="saved-scalar-values-across-conditionals-2026-09-08"></a>
- [Saved scalar values across conditionals, 2026-09-08](native-owned-global-maps/02-saved-scalar-values-across-conditionals-2026-09-08.md#saved-scalar-values-across-conditionals-2026-09-08)
<a id="guarded-saved-reads-after-conditional-deletion-2026-09-08"></a>
- [Guarded saved reads after conditional deletion, 2026-09-08](native-owned-global-maps/02-saved-scalar-values-across-conditionals-2026-09-08.md#guarded-saved-reads-after-conditional-deletion-2026-09-08)
<a id="scalar-short-circuit-results-2026-09-08"></a>
- [Scalar short-circuit results, 2026-09-08](native-owned-global-maps/02-saved-scalar-values-across-conditionals-2026-09-08.md#scalar-short-circuit-results-2026-09-08)
<a id="finite-nullable-published-results-2026-09-08"></a>
- [Finite nullable published results, 2026-09-08](native-owned-global-maps/02-saved-scalar-values-across-conditionals-2026-09-08.md#finite-nullable-published-results-2026-09-08)
<a id="owning-nullable-map-keys-2026-09-08"></a>
- [Owning nullable Map keys, 2026-09-08](native-owned-global-maps/02-saved-scalar-values-across-conditionals-2026-09-08.md#owning-nullable-map-keys-2026-09-08)
<a id="owning-nullable-payloads-2026-09-08"></a>
- [Owning nullable payloads, 2026-09-08](native-owned-global-maps/02-saved-scalar-values-across-conditionals-2026-09-08.md#owning-nullable-payloads-2026-09-08)
<a id="finite-nullable-reads-from-mixed-storage-2026-09-08"></a>
- [Finite nullable reads from mixed storage, 2026-09-08](native-owned-global-maps/02-saved-scalar-values-across-conditionals-2026-09-08.md#finite-nullable-reads-from-mixed-storage-2026-09-08)
<a id="finite-nullable-host-result-proof-2026-09-08"></a>
- [Finite nullable host-result proof, 2026-09-08](native-owned-global-maps/02-saved-scalar-values-across-conditionals-2026-09-08.md#finite-nullable-host-result-proof-2026-09-08)
<a id="per-invocation-results-before-the-complete-census-2026-09-08"></a>
- [Per-invocation results before the complete census, 2026-09-08](native-owned-global-maps/02-saved-scalar-values-across-conditionals-2026-09-08.md#per-invocation-results-before-the-complete-census-2026-09-08)
<a id="method-local-leaf-object-ownership-2026-09-08"></a>
- [Method-local leaf object ownership, 2026-09-08](native-owned-global-maps/02-saved-scalar-values-across-conditionals-2026-09-08.md#method-local-leaf-object-ownership-2026-09-08)
<a id="next-boundary"></a>
- [Next boundary](native-owned-global-maps/02-saved-scalar-values-across-conditionals-2026-09-08.md#next-boundary)
