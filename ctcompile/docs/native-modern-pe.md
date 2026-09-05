# Modern partial evaluation for CTNative

Design review, 2026-09-05. The starting implementation was `46e94cb` plus the
closure, precomputation and direct-call specialization tracks in
[the PE roadmap](native-pe-roadmap.md). The integrated gate now passes 394/394
CTests and 120/120 lit cases. This document distinguishes the implemented BTA
summary worklist from the proposed dependency queries and richer contexts.

Keep partial evaluation optional. An optimization may remove proved work, retain
ordinary CTJS, or decline an attempt. It must preserve observable values, effects,
control flow and allocation identity. Native type and ownership admission remains
an independent requirement for every residual program. Turning an optimization
off need not make an otherwise unsupported program native-compilable; it must
never change the meaning of a program accepted by both configurations.

## Papers read and the mechanisms they establish

Both PDFs in `../academic-papers/partial-evaluation/` (from the repository root)
were read in full with
`pdftotext -layout`: 37 and 24 pages, including limitations and evaluation. Page
references below use PDF pages, equivalent to article pages `16:n` and `160:n`.

| Source | Actual contribution | Transfer to CTNative |
|---|---|---|
| **PYE**, Manas Thakur and V. Krishna Nandivada, TOPLAS 41(3), 2019, [DOI 10.1145/3337794](https://doi.org/10.1145/3337794) | Stage a *program analysis*: statically compute conditional summaries, then resolve application/library dependencies using installed-library summaries during JIT compilation. | Retain unresolved analysis queries and their dependencies; discharge them when the closed module or a checked host contract supplies the missing evidence. |
| **Partial Evaluation, Whole-Program Compilation**, Chris Fallin and Maxwell Bernstein, PLDI 2025, [DOI 10.1145/3729259](https://doi.org/10.1145/3729259) | `weval` specializes an SSA interpreter using context-sensitive constant propagation and code duplication, producing whole guest-function CFGs ahead of time. | Explicit specialization inputs, bounded context memoization, dependency-driven fixed points and careful SSA reconstruction. |

The Fallin file is a research article, although its filename/request context may
suggest slides. PYE does **not** execute Java initialization or reconstruct an
application heap. Its EASE instance proves **thread** escape for synchronization
elision (PYE §2.2, p.5; §5, pp.17–20). That result alone says nothing about C++
return lifetimes, ownership cycles or removal of a collector.

PYE's conditional values encode dependencies on unavailable callees, callers and
dereferenced objects (§3.3–3.6, pp.8–12). A finite-height lattice and worklist solve
these dependencies to a fixed point; pre-applying meet and retaining only queried
results reduce summary size. Its equivalence claim is relative to the underlying
analysis and correct dependency generation, with conservative fallback for
callbacks (§6–6.1, pp.21–23). PYE explicitly identifies allocation-site abstraction
and array-field merging as precision limits, and treats summary verification as a
separate problem (p.21). Its optimistic treatment of residual dependency cycles
belongs to those particular analysis lattices; it is not permission to execute
unknown recursion at compile time.

In `weval`, `(block, context)` and `(value, context)` maps drive reachable code
cloning; changed abstract inputs re-enqueue blocks (§3.2, pp.7–9). A context hint
improves precision without supplying a semantic fact (§3.1, p.6). By contrast,
`SpecializedConst` and `SpecializedMemory` promise actual argument values and
constant memory (§3.5, p.11). Directed value specialization introduces runtime
branches covering the enumerated alternatives (§3.3, pp.8–10); it does not guess
the runtime value. Cross-context live values require SSA repair and dependency
tracking (§3.4, pp.10–11).

The paper's interpreter-state optimizations have stronger obligations than its
context hints. Virtual locals/stack state require flushing before outside code
observes memory (§4, pp.12–13). Its SpiderMonkey integration retains interpretation
for some exceptional/async paths (§6.2, p.16); snapshotting is an integration
choice, not a requirement of the transform (§3.5). Its specialization cache keys
the input module hash plus request argument data (§6.6, p.19). Profile-guided
guarded inlining and runtime specialization are future work (§9, p.22).

CTNative already imports CTJS and targets C++ without a VM/GC runtime. Borrowing
these mechanisms does not require embedding SpiderMonkey, snapshotting its heap,
or retaining an interpreter fallback. Neither paper's performance results predict
CTNative performance: their workloads, runtimes and baselines differ.

## What the current source actually provides

| Component | Existing mechanism | Boundary to preserve |
|---|---|---|
| `Analysis/BindingTime/` | Value binding time is separate from operation eligibility; eligibility includes control and heap effects. Common literal parameters require closed call evidence. Unknown memory operations invalidate tracked heaps. | `Flow.cpp` conservatively handles dynamic control and loops. The first change from this review makes `Analysis.cpp` re-enqueue callers on full return-summary changes, with a conservative work limit; focused validation passes. |
| `PartialEvaluation/` | An exact compiler-owned heap evaluator, whole-factory evaluation and a straight-line static prefix with an unchanged dynamic suffix. Stable node IDs preserve graph sharing. Failed attempts leave source IR intact. | Module environment/callable checks still exclude unproved hosts, prototypes and implicit invocation. Resource limits and live-cycle refusal remain independent of BTA. |
| Closure extension | Immutable capture cells and known closure code can enter the compiler heap without invoking latent bodies. A private native-lowering copy supplies additional checked Map/call provenance. | Construction is not call execution; mutable bindings, inherited environments and unproved identities still decline. Native closure ownership must be proved again after rewriting. |
| `Symbolic/` | Shared primitive folding, primitive result domains, normal-return summaries and selection of known structured branches. Calls/producers retain their effects even when a consumer folds. | Generic arguments and carried loop values start unknown. Number facts include NaN; no general `x + 0`, `x * 0` or floating reassociation. |
| `Specialization/` | Exact primitive argument tuples select bounded direct-call clones; one real generic call remains for current native inference. Clones discard input proof attributes. | Capture/implicit-call-state/recursive cases remain generic. Tuple reuse currently lasts one pass invocation; there is no persistent compilation cache or general CFG context engine. |

Paths in this table are relative to `ctcompile/lib/CTNative/`. In particular,
`PartialEvaluation/Primitives.cpp` calls ctbrowser's primitive semantic helpers
inside the compiler; graph references cannot enter that adapter. It does not
dispatch bytecode. `Residualize.cpp` reconstructs allocations and edges in the
factory, preserving fresh identities per invocation; it does not emit one shared
snapshot instance. A captured Map has an immutable *binding* while its contents
may still change when a retained method runs.

The exact vendor-derived Data checker,
[`bootstrap-data-binding-time.py`](../../tools/check/bootstrap-data-binding-time.py),
separately records seven imported functions, reference observations, dynamic host
boundaries, PE refusals and functions outside the candidate set. It verifies a
retained Map and three latent methods and rederives forged annotations. These are
source-boundary checks, not native host execution. The roadmap's 19/574 Bootstrap
native count must not be replaced with a simplified fixture's success rate.

## Contracts for the next implementation

**Separate facts, permissions and outcomes.** A literal or result domain describes
what a value is *if its producer returns normally*. Static execution additionally
requires known control, supported semantics, valid environment assumptions and
allowed effects. For example, `effect(); return 42` permits folding `result + 1`
after the retained call; it does not permit erasing the call or assuming that it
returns. The first shared API can be a small compiler-internal record:

```text
Decision = { valueFact, executionPermission, dependencies, reason }
Attempt  = unchanged(reason) | rewritten(residualPlan) | budgetExhausted(reason)
```

These are proposed interfaces, not new trusted IR attributes. Reasons and debug
annotations are outputs. Every consumer rederives or validates evidence against
the current IR; missing or stale evidence means unknown. A later explicit
"require initialization removal" diagnostic may fail a build when optimization
does not occur, without changing the semantic rules of ordinary optimization.

**Make assumptions attributable.** Initially accept only exact literals and
closed callable/native-Map proofs already derived from the module. Later immutable
input or host contracts must name their provenance, lifetime and invalidating
events. A request to create a specialization is not evidence that mutable memory
is constant. Context-splitting hints select analysis precision; they must not
silently become semantic assumptions.

**Represent residual queries as compiler work.** Start with a bounded query such
as `MayWrite(callee, argument-reachable-region)` or
`ReturnDomain(callee, abstract-arguments)`, plus its dependency edges. An unavailable
answer retains the call/read and invalidates affected heap facts. Resolving a
query is analysis/link-time work, not a runtime query engine. General recursive
heap transformers and serialized summaries can wait until this module-local
contract is tested.

**Track effects and observation points.** A call summary must separately describe
reads, writes, escapes/callback retention, possible callback execution, and abrupt
completion. Pure normal results do not imply an effect-free call. Include globals,
prototypes and implicit coercion/getter/iterator invocation. Retaining an unknown
call does not by itself make compile-time object writes safe: it may previously
have changed shared prototypes. Keep the broad environment guard until a more
precise proof covers the relevant execution history. Unknown effects invalidate
the reachable heap conservatively, or all tracked heap state when aliases are
unbounded.

**Keep heap identity distinct from abstract shape.** Analysis nodes may denote
sets of allocations. Exact evaluation IDs denote individual allocations in one
attempt. A schema match or allocation-site match never proves runtime identity.
Track mutation versions for facts that depend on contents; an immutable binding
does not freeze the object. Prefix roots include scalar snapshots, aliases, Map
keys/values and captured cells. Residual allocation recipes instantiate a fresh
graph per factory invocation and preserve internal sharing, insertion order and
SameValueZero key semantics. Live cycles remain a native ownership refusal.

**Make convergence and rollback explicit.** Compare complete summaries: binding
time, domain, literal, reference set and execution eligibility. Enqueue reverse
call/query dependents on any semantic change; compare reference sets without
depending on insertion order. Recursive SCCs require a sound lattice treatment,
not a guess that a finite iteration count proved purity. Bound work, contexts,
heap nodes and emitted code. If an attempt fails after a private heap mutation,
discard it; a separately attempted prefix starts from fresh state. Budget limits
reduce optimization, never authorize incomplete analysis results.

**Cache recipes under complete keys.** Begin with an invocation-local cache keyed
by source function and exact static tuple. A future persistent key needs the IR
revision/module digest, semantic-helper version, target/ABI, argument values,
closure code/capture descriptors and assumptions on which the result depends.
Preserve numeric attribute distinctions such as signed zero; do not key primitive
specializations using Map's SameValueZero relation. If heap inputs are added,
include graph aliasing and mutation versions, not only shape or node IDs. Cache
code/recipes and verified summaries, never a mutable per-call heap instance.

**Distinguish declining from deoptimization.** PYE can recompile dependent JVM
methods when assumptions change; some `weval` integrations retain interpreted
paths. CTNative's AOT choices are to keep a semantically valid native operation,
emit a guard with a fully supported native generic branch, or report native
admission failure. It has no VM state reconstruction/deoptimization facility.
Speculation must not introduce one implicitly.

## Staged, buildable changes

1. **Make BTA's summary fixed point explicit.** The first implementation replaces
   completion-only sweeps with full-summary comparison and a bounded reverse-call
   worklist. Recursive calls stay dynamic; exhaustion rederives local facts with
   every direct call dynamic. `test/CTNative/binding-time-summaries.mlir` adds
   source-order permutations, exact literal propagation, joined keys and recursive
   heap invalidation; existing effect tests cover dynamic mutations. The devbox
   compiler build, all eight focused BTA/PE lit tests and both exact-source Data
   binding-time CTests pass, including the forged-annotation negative control.
   This strengthens the contract before richer domains are introduced; this review
   has not established a wrong-code witness in the old acyclic subset.
2. **Unify decision reporting without expanding admission.** Add the small decision
   and attempt records behind the existing APIs. Test forged/stale annotations,
   normal-result facts across effectful calls, and a budget failure after mutation.
   Keep current native/source observations identical with the feature disabled.
3. **Add one module-local conditional effect query.** Use a closed visible helper
   operating on a proved fresh Map/object. Preserve facts across a proved unrelated
   write; invalidate them for reachable writes, callbacks and unknown aliases.
   Implement a reverse-dependency queue and measure solved/remaining queries.
   Do not relax host/prototype guards as a side effect of this step.
4. **Extend specialization only where measured.** Reuse exact tuple variants across
   repeated passes, then consider finite primitive contexts for residual branches.
   Memoize `(block, context)`, join incoming facts and repair cross-context SSA.
   Unbounded contexts generalize to supported generic code or decline. Keep
   irreducible CFGs outside the slice until both transformation and native lowering
   support them. General heap-context specialization follows identity/version tests.
5. **Advance the exact Data initialization boundary.** Keep the vendor fragment and
   provenance intact. First distinguish static closure construction from dynamic
   method execution in reports, then introduce one checked host boundary only when
   its reads/writes/callback/identity contract is available. Report admitted native
   functions and retained host work separately. A native fixture remains useful
   differential evidence but cannot stand in for this probe.

Every stage stays opt-in and independently testable. Existing options are
`--ctnative-precompute`, `--ctnative-specialize` and
`--ctnative-partial-evaluate`; their resource options are documented in
`Transforms/Passes.td`. A recommended pipeline may run precomputation before and
after specialization, then heap PE and native lowering. Pass ordering may improve
precision, but a pass must never require a prior optimization's debug attributes
for correctness.

For each stage, compare reference execution with native output under disabled,
individual and combined optimization configurations where all are admitted.
Inspect effects as well as values: a chosen branch or retained call executes once
and in order; two equal-shaped factories remain distinct; alias mutation is visible;
a saved scalar predates a later write. Include NaN, signed zero, null/undefined,
Map misses/deletion/reinsertion, recursive calls, unknown callbacks, live cycles,
repeat passes and exhausted budgets. Require nonzero transformation evidence for
positive cases, no-VM output checks, lifetime sanitizers and the full devbox gate.
Measure compile time, residual code/allocation size and runtime separately.
