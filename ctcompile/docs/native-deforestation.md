# Native snapshot deforestation

`--ctnative-deforest` removes a temporary numeric Map snapshot when one proved
scalar consumer can read the unchanged Map directly. Run it immediately after
`--ctnative-lower-to-emitc`, before the per-function canonicalization and SCF
conversion passes. It is opt-in and changes neither type admission nor the
ownership proofs that make the original Map native.

## Relationship to Lumberhack

This implementation adapts the producer/consumer strategy approach from
Chen and Parreaux's [The Long Way to Deforestation](https://doi.org/10.1145/3674634),
especially §§2.2–2.3 and 5.4. The supplied paper is
`academic-papers/deforestation/The-Long-Way-to-Deforestation-lumberhack-paper.pdf`
in the workspace's parent directory. The
[upstream artifact](https://github.com/hkust-taco/lumberhack/tree/master)
implements the algorithm in Scala, consumes a pure MLscript/Haskell core and
emits OCaml. It has no CTJS or EmitC adapter. The installed MLIR affine fusion
utility handles loop nests; it does not model JavaScript Map snapshots or
their observable mutations.

The implemented strategy has a deliberately small domain:

1. A recognized `map_keys` or `map_values` call supplies a producer lower
   bound. A worklist propagates that bound through local slots and vector
   copies. A slot must have one assignment, an empty initializer, and no read
   before that assignment. Earlier reads still observe the empty vector.
2. `vec_length` and `vec_at` supply consumer upper bounds. Unknown uses,
   escapes, reassigned slots or conflicting consumers select the identity
   strategy, retaining the snapshot. This follows Lumberhack §5.4's conflict
   rule. Several uses of one consumer's scalar result are allowed.
3. After checking evaluation order and effects, elaboration replaces the
   consumer with a Map scalar projection and removes its producer and closed
   forwarding graph. Inference completes before any program rewrite.

This is a restricted Map projection strategy, not an implementation of
Lumberhack's general recursive strategies, higher-order subtyping, thunk
generation, cardinality analysis or definition specialization. Multiple
snapshot destructors are retained even when their results could theoretically
be combined. General callback map/filter/reduce fusion remains separate work.

Lumberhack §2.3 uses thunks to preserve call-by-value evaluation when consumer
computations move into producers. This slice leaves the scalar computation at
its original consumer position. It neither executes a skipped branch nor
duplicates a callback. Its effect proof is stricter than the paper's pure
language assumptions because JavaScript Maps are mutable.

## Effect and representation contracts

The producer, local forwarding operations and consumer must stay in one basic
block. Unknown direct or opaque calls, Map writes, unsupported operations and
control boundaries between production and consumption keep the snapshot.
Writes are barriers regardless of which alias names the Map. Even a call with
no explicit Map operand can mutate one through a retained capture, so it is
also a barrier.

Only the existing native helper ABI is recognized. The pass requires the exact
Map helper definition emitted after native admission; a lookalike call name
or an input optimization annotation does not authorize rewriting. The pass
recomputes its annotations. It inserts its indexing helper only when needed,
leaving the default pipeline's generated C++ unchanged.

Length becomes `map_size`. Indexed keys or values become a bounds-checked
`map_snapshot_at` projection. Both preserve insertion order, deletion and
reinsertion, present NaN and the first inserted numeric key's sign. Indexing
uses the reference's current contract: only numeric keys index elements,
fractional indices truncate before bounds checking, and absent indices return
tagged undefined. In particular, `-0.5` indexes element zero.

`max-snapshots` defaults to 256 candidate sites per invocation. `max-scan`
defaults to 10,000 flow uses and intervening operations per candidate.
Exhausting either budget selects identity. A report counts removed snapshot
sites and retained identity strategies; it does not count allocator calls or
claim a runtime speedup.

## Validation

`native-deforestation-fixture.js` covers empty and populated Maps, dynamic and
fractional indices, NaN, infinity, boolean/null/undefined indices, signed zero,
numeric keys on nested Maps, overwritten values and deletion/reinsertion.
It also covers reused scalar results, conflicting snapshot consumers, direct
and captured mutation, length observed across mutation, control boundaries and
snapshots created inside loops. The lit gate separately checks escape and
identity consumers, conflicting producer bounds, missing runtime contracts,
budget exhaustion, reads and copies before a slot's sole assignment, and repeated
invocation.
