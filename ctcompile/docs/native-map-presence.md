# Conditional membership for native Maps

Nested Map reads now accept a path-sensitive presence proof. This admits
Bootstrap Data's lazy initialization pattern with supported key/value types:

```js
outer.has(key) || outer.set(key, new Map());
const inner = outer.get(key);
```

A fresh true `has` also proves presence inside a branch or after an early
return guard. A `set` establishes presence directly; both live branch arms
must establish the same fact before it survives their merge. Deletion or
clearing can be followed by a fresh check or insertion that restores proof.
An already retrieved child keeps its owning handle after parent deletion.

## Proof and effects

`Analysis/NativeMap/Presence.cpp` performs a forward must-analysis over
structured regions. Facts pair an exact receiver identity with a key.
Receiver identity follows SSA and the identity-preserving result of `set`;
keys match by SSA or equal primitive constants. A schema family never proves
two runtime allocations identical.

`has` observations are snapshots, tracked separately from their SSA boolean.
`delete` and `clear` invalidate both presence and observations for every
possible alias in the receiver's schema family. This is conservative even
when deletion names a different key. Transitive function summaries propagate
erasure effects through direct calls, including recursive call graphs.
Unknown calls invalidate all facts. A helper proved unable to erase entries
preserves facts, so creating a child through a factory remains supported.

Branch conditions unwrap truthiness and logical negation before learning
from a live `has`. Loop analysis starts without facts or cached observations
from earlier iterations, permits fresh local guards and stores, and exports
no facts after the loop. Unstructured regions receive no incoming proof.
All required child reads must be proved before any presence annotation is
written. Input annotations are cleared on both initial and repeated lowering.

## Validation and current limits

The native fixture admits **15/15 functions**, with **eight numeric
observations** checked against the independent interpreter. It covers lazy
initialization, retained values, early guards, short-circuit conditions,
deletion/reinsertion, branch intersection, fresh loop guards and call effects.
The closed-world census records **14** resolved globals, **30** resolver calls
and **0** additional native-lift calls.

GCC/Clang strict compilation, plain/deduced execution, type pins, no-VM checks,
the off-by-one control and an excessive 16-function floor all pass their
expected checks. ASan/UBSan with leak detection matches all eight observations.
The branch-intersection case also pins warning-clean C++ when later folding
eliminates a parameter's last ordinary use.

Source tests cover 29 adversarial cases: cached predicates, alias/callee
mutation, argument-evaluation effects, key mismatch, false branch joins,
loop exits and recursive erasure. An IR regression tries to forge presence
after deletion and checks initial and repeated passes.

Presence does not supply a carrier for arbitrary merged values. A used
`has && numericGet` result can still require a boolean/number union, and
different child allocations merged through structured results remain outside
the current Map-flow proof. Bootstrap also needs null/component carriers,
general object/host identities and typed publication. The
[real Data probe](bootstrap-data-probe.md) records those remaining refusals.
