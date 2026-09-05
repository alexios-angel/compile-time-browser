# Native partial evaluation: implementation plan

Baseline: `46e94cb`. Closed factories and static entry prefixes already evaluate
over a compiler-owned heap. BTA distinguishes known values from executable static
operations. Bootstrap still compiles 19/574 functions natively; its Data initializer
needs retained closures, component values and host contracts.

The initial work is divided into four independently implemented tracks. Each lands
only with source observations and adversarial tests for its own proof boundary.

| Track | First implementation | Owner |
|---|---|---|
| BTA, heap residualisation and closures | Construct known closures without executing their bodies; preserve immutable capture environments and shared Maps | Heap agent |
| Symbolic computation and precomputation | Fold primitive expressions with ctbrowser semantics; derive safe symbolic result facts and resolve known control while retaining effects | Symbolic agent |
| Deforestation | Eliminate a proved temporary Map snapshot when its consumers need only scalar projections | Deforestation agent |
| Code specialization and residualization | Bounded direct-call variants with known primitive arguments; preserve generic execution and run precomputation on residual bodies | Integration agent |

## 1. Closure state in the compiler heap

A closure's code identity can be known while its future parameters remain dynamic.
Evaluating construction must never invoke the body. Represent proved function
identities and immutable captures in the compiler graph. Trace captured Maps and
objects alongside ordinary graph edges, preserving shared identities and distinct
factory invocations. Emit CTJS closure construction and let the existing native
closure and ownership analyses select the C++ representation.

Extend BTA only for the matching proved construction/capture operations. Mutable
captures, unsupported implicit receivers, unknown function targets, live ownership
cycles and unproved host effects remain runtime work or refuse specialization.
No compiler graph object enters ctbrowser's runtime heap. Resource limits and
transactional failure apply to closure environments too.

Gate: a getter invoked after factory return, two methods sharing one Map,
independent factory instances, dynamic method parameters, snapshot captures,
object-key identities, and negative cases for mutable captures and cycles.
The native output must have no VM symbols and pass lifetime sanitizers.

## 2. Symbolic computation and precomputation

Keep a distinction between a literal, a known symbolic result domain and an
unknown runtime value. Reuse the existing primitive adapter for literal
arithmetic, coercion, comparisons and strings. Known boolean/string facts may
justify identities that are invalid for arbitrary numbers or objects. For
example, self-equality requires excluding NaN; `x * 0` and `x + 0` are not general
JavaScript identities. Do not reassociate floating-point arithmetic.

Resolve control only when its condition is proved, retaining the selected runtime
effects once and in order. A known return value does not make an effectful call
removable. Unknown coercions, getters, hosts and overloaded property operations
remain residual expressions. Bound work and rederive any input annotations.

Gate: NaN, signed zero, infinities, coercions, strings, unknown operands, constant
branches with effectful arms, symbolic booleans, and an effectful producer whose
known result is consumed by a folded expression. Differential checks inspect both
the output values and the residual operations.

## 3. Deforestation

Use the strategy inference approach of Chen and Parreaux's *The Long Way to
Deforestation* (ICFP 2024, DOI 10.1145/3674634). The supplied PDF now lives at
`../academic-papers/deforestation/The-Long-Way-to-Deforestation-lumberhack-paper.pdf`.
The upstream implementation targets a pure MLscript/Haskell core and emits OCaml;
there is no CTJS adapter. Infer producer and consumer bounds over native Map
snapshots, selecting the identity strategy on conflicts. The first implementation
is a restricted adaptation of Lumberhack; recursive fusion and its higher-order
subtyping machinery require later work. See [the implemented strategy](native-deforestation.md).

The first target is a compiler-proved Map `keys()`/`values()` snapshot whose
consumers need only length or indexed elements. Avoid constructing an intermediate
vector where a direct scalar projection has the same semantics. Preserve the
snapshot's observation time: a mutation between construction and consumption must
not turn a snapshot into a live view. Retain allocation whenever the vector
escapes, its identity is observed, its consumers are unsupported, or mutation and
effect ordering cannot be proved safe.

Implement the initial proof at the IR level with the strongest existing native
Map information. Report eliminated intermediate allocations. General map/filter/
reduce fusion is a later extension requiring callback effect, cardinality and
iteration-order contracts; it is not implied by projection fusion.

Gate: scalar consumers, multiple reads, empty Maps, insertion order, deletion and
reinsertion, mutation through aliases and calls, escaping snapshots, fractional
indices and numeric edge cases. Compare native and reference observations and
inspect the output to establish that an intermediate allocation was removed.

## 4. Code specialization and residualization

Create bounded variants of proved direct callees for exact primitive argument
tuples, leaving remaining parameters dynamic. Preserve receiver/callee semantics,
argument evaluation order and every runtime effect. Key numeric constants by
their actual attributes, including signed zero and NaN bit patterns. Share a
variant only when its static argument tuple and source function match.

Keep a generic call route so native inference retains evidence for the original
function. Exclude captured or self-observing functions and recursive expansion in
the first slice. Cap variants and cloned operations, retain code on refusal, and
avoid repeated-pass proliferation. Precompute each specialized body, then feed it
through BTA and heap residualisation. All residual code still passes normal
native type and ownership admission.

Gate: multiple static argument sets, mixed static/dynamic inputs, reuse of a
variant, runtime effects that execute once, literal coercions, fresh returned
heaps, recursion refusal, budget exhaustion and repeated passes.

## 5. Supercompilation: design before implementation

The first separate pass constructs a bounded process graph for capture-free,
self-recursive scalar kernels. A configuration consists of the source function,
exact static argument attributes and dynamic argument positions. Register a
residual-function promise before driving its body; matching recursive
configurations fold to that promise. Fold exact configurations before checking the
termination whistle. Dynamic arguments retain their original evaluation order.

Drive literal primitives through the existing semantic adapter and resolve known
SCF branches. Retain both alternatives of unknown branches, all dynamic
computations and their order. Initially reject heap, host, capture and non-self
call operations. The ordinary type and ownership admission still decides whether
the residual result has a native C++ representation.

The whistle uses homeomorphic embedding over a finite abstraction of argument
shapes; numeric and string payloads are collapsed only for termination checks.
Exact memo keys retain full attributes, including signed zero and NaN. A whistle
retains the generic recursive call in this slice. It is not an equivalence proof
and does not yet implement generalization. Local driving, graph size and emitted
operation budgets are separate. Always retain the unchanged alternative and
commit a driven graph only if its growth stays within the configured bound.

Preserve one generic external call for the original function's type evidence.
Stage all cloned functions transactionally before redirecting root calls. This
implements recursive configuration folding, beyond the nonrecursive variant
pass; general heap-aware driving, anti-unification and multi-result search follow
the separate literature design.

## Integration and Bootstrap evidence

All transformations below are independently opt-in. Omitting their flags leaves
the default pipeline unchanged; no optimization level silently enables them.

| Control | Default | Purpose |
|---|---|---|
| `--ctnative-precompute` | Off | Primitive and symbolic simplification, preserving runtime effects |
| `--ctnative-specialize` | Off | Bounded variants for exact static argument tuples |
| `--ctnative-partial-evaluate` | Off | BTA-guided initialization and heap residualisation |
| `--ctnative-deforest` | Off | Proved Map snapshot projection fusion after native lowering |
| `--ctnative-prune-unreachable` | Off | Remove proved unreachable private functions after PE, preserving published and numeric closure targets |
| `--ctnative-supercompile` | Off | Bounded recursive driving and folding; generalization remains planned |
| `--ctnative-binding-time-analysis` | Optional diagnostic pass | Print rederived staging facts without evaluating the program |

BTA is always recomputed when heap evaluation needs it. Type, effect and ownership
proofs remain required by the transforms that rely on them; turning off an
optimization does not turn off correctness checks. Each pass has independent work
and/or code-size budgets, and budget exhaustion retains runtime computation.
No annotation claiming a prior proof or optimization result is trusted.

An optional transformation can make previously unsupported code admissible by
removing proved static work. For example, the precomputation fixture contains
constant string coercions that the current native runtime lowering cannot emit;
with precomputation off, those operations retain their existing native diagnostics.
Turning the pass off never silently enables a boxed fallback.

Resolve globals and structure first,
precompute literals, specialize direct calls, precompute the resulting bodies,
supercompile eligible recursive kernels, evaluate proved heap initialization,
optionally prune unreachable private helpers, then lower to native C++. Run deforestation
at its documented proof point. Existing individual passes remain usable.

The exact vendor-derived Data probe remains a separate check from simplified
native fixtures. Measure retained host boundaries and whether closure initialization
advances without claiming whole Bootstrap execution from a fixture. Host contracts
must explicitly describe reads, writes, callback escape and object identity before
module-wide environment guards can be relaxed.

## Integrated results, 2026-09-05

The devbox gate passes **406/406 CTests**, including **123/123 lit cases**, in
**461.12 seconds**. All **482 C++ files** pass the pinned formatter, and
`git diff --check` passes. Each new source fixture passes ordinary and deduced
native C++, GCC/Clang, reference comparison, altered-output rejection and no-VM
checks. The new object-value and reachability fixtures also pass ASan/UBSan
with leak detection and direct reference comparison. Earlier closure,
deforestation and specialization sanitizer results are retained in their
respective implementation records.

| Source fixture | Measured transformation | Native functions | Numeric observations |
|---|---|---:|---:|
| Owning object/scalar Map values | Exact getter, calls/captures, lifetime and structured-loop flow | 16/16 | 47 |
| Unreachable helpers after specialization and PE | 2 orphan variants removed, generic published callable retained | 4/4 | 4 |
| Immutable closure heaps | 5 evaluated factories, 21 live residual nodes | 19/19 | 7 |
| Precomputation | 69 expressions and 15 structured branches simplified | 11/11 | 20 |
| Direct-call specialization | 12 variants, 17 redirected calls, 175 cloned operations; following PE evaluates 9 functions into 3 live nodes | 24/24 | 20 |
| Deforestation | 9 snapshot sites removed, 6 retained | 20/20 | 34 |
| Recursive supercompilation | 3 kernels, 5 configurations, 6 folds, 1 whistle, 104 added residual operations | 14/14 | 15 |

Deforestation, specialization and supercompilation also pass native/reference
and no-VM checks with their optimizations disabled. The precomputation-disabled
fixture retains the string-coercion diagnostics described above.

Earlier integration review added two regression controls. Snapshot fusion preserves an
empty slot read before its initialization. BTA keeps original parameters dynamic
when a specialized symbolic call still uses the original closure for boxed
dispatch; the executable boxed test requires `1020` instead of the incorrect
`1010`. The BTA summary worklist compares complete facts and completion state,
with conservative fallback on exhaustion.

The current continuation additionally checks observable poison on loop exit and
continuation paths, definite-identity and union-valued loop lifetimes, conditional
heap effects through Map keys/captures, and both symbol/numeric reachability.
The printing gate recognizes the new carrier while still requiring spelling-only
differences and rejecting a wrong type pin. The boxed specialization regression
also runs optional pruning and continues to require `1020` and effect count `2`.

Default corpus coverage is unchanged:

| Corpus | Native functions | Resolved globals / direct calls / lifted direct calls |
|---|---:|---:|
| Bootstrap | 19/574 | 0/21/208 |
| p5 | 39/4754 | 0/41/328 |
| Phaser | 45/7725 | 0/48/283 |

The exact Data browser/CommonJS/AMD probes remain **0/7, 0/7 and 0/8 native**,
with **19/19/20 reference observations**. Boxed Bootstrap remains byte-identical:
**10,984,359 bytes**, SHA-256
`6847477849e52f51b8369b8e9d969c223fd647d881970003b1541c76fc67ec8a`.
These results establish the new carrier and optional stages while preserving the baseline;
full native Bootstrap initialization remains open.

## Object payloads, conditional effects and reachability

[Object values](native-object-values.md) extend the exact Bootstrap getter to
property-free owning identities mixed with number, boolean, null and undefined.
Saved aliases survive Map replacement, deletion and clearing. Calls, immutable
captures and structured control preserve that owner. Field access, object numeric
coercion, host publication and object-valued snapshots still require later work.
Lift placeholders are excluded from schema flow only by a bounded proof that
they cannot be observed, including the separate loop continuation and exit paths.

[Conditional effect queries](native-conditional-effects.md) preserve disjoint
fresh caller heaps across runtime calls. Argument-path clauses describe own
fields and proved native Maps. Writes invalidate the entire reachable local graph,
including Map keys and captures; unresolved clauses retain broad invalidation.
The runtime call and result stay dynamic. These analysis facts do not by
themselves perform additional PE or establish a Bootstrap host contract.

[Reachability](native-reachability.md) removes two orphan specialization variants
from the integrated fixture after PE. Four reachable functions remain, including
the published generic function used by a runtime effectful call. Both symbolic
targets and original numeric closure references retain functions. Unknown graph
contracts and budget exhaustion retain the whole module.

## Next bounded work

For Bootstrap, extend the identity payload to owned component fields and establish
one checked host/effect contract before widening the exact Data initializer.
Keep wrapper calls, error reporting, string-key snapshot/Array.from behavior and
typed export publication explicit. The current conditional queries remain behind
their own closed-environment proof and do not relax PE/native module guards.

Published declarations remain roots after PE. Removing their closure/store pairs
requires a proved export-observation boundary beyond private-function reachability.
Whole-factory PE also retains its own call evaluator; general equivalence between
symbolic native targets and retained boxed callee values remains separate work.
For [supercompilation](native-supercompilation.md), scalar generalization comes
before heap-aware configurations and multi-result graph search. Recursive
Lumberhack fusion, general shared mutable capture environments and compiled
source-site attribution remain separate workstreams.
