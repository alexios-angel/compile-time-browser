# Native supercompilation: design and research basis

Design baseline: `46e94cb` plus the native partial-evaluation tracks described in
[the roadmap](native-pe-roadmap.md). This document specifies an independently
optional supercompilation pass and its first implementation boundary. It is not a
claim that the broader heap-aware design or all of the gates below have shipped.

Start with a CTJS configuration driver for private, capture-free, self-recursive
functions with known primitive arguments. Drive the known parts of their bodies,
retain unknown branches, and fold repeated configurations into calls to residual
functions. Stop growing configurations at an embedding whistle and keep a generic
runtime call. This provides a small foundation for recursive specialization while
the existing heap evaluator handles proved initialization.

The Bootstrap application is later specialization of closed component/configuration
code around runtime element data. This first scalar slice does not resolve DOM
effects, prototype mutation, escaping callbacks, or retained component identities.
The exact vendor Data initializer remains a separate acceptance probe; its native
coverage must be measured rather than inferred from a scalar fixture.

## What the five supplied papers contribute

The references below cover all five PDFs in
`../academic-papers/supercompilation/`, relative to the repository root. References
to the Russian preprints use their **printed page numbers**; the downloaded PDF
page is one greater because of the library cover. References to the three English
papers use the page numbers in the supplied copies. The Russian titles are
translated here; no English edition is implied.

| Reference | Concrete mechanism | Use and limitation in CTNative |
|---|---|---|
| **[KR18]** Klimov and Romanenko, §§2, 4.2–4.7, pp. 4–14 | Configurations describe sets of execution states; driving combines reduction and case analysis; alpha-equivalent nodes fold into a finite graph; free variables become residual function parameters. | Make configuration identity and graph backedges explicit. Its SLL examples use a lazy first-order functional language, not JavaScript object semantics. |
| **[KR18]** §§5.2–5.3, pp. 16–19; §6, pp. 19–21 | Generalization factors a configuration through a substitution and an explicit `let`; specialization is an application of supercompilation, not its definition. | A clone with substituted constants alone does not establish configuration driving or folding. Later generalization must retain the removed expressions and their argument bindings. |
| **[R18]** §§2.3–2.6, pp. 7–15 | Generalize a growing child or replace an ancestor and rebuild its subtree; homeomorphic embedding detects growth; separate local and global histories avoid premature whistles. | Keep exact matching, growth detection and generalization separate. A whistle is a reason to stop or generalize, never evidence that two computations are equivalent. |
| **[R18]** §3, pp. 16–24; §4, pp. 24–28 | Evaluation strategy affects correctness; a basic offline BTA can keep an ever-growing accumulator static; offline PE can still be faster and more predictable. | Preserve strict evaluation and keep the existing BTA/heap evaluator. A `Static` classification does not ensure finite specialization. |
| **[BPJ10]** §§3.1–3.6, pp. 3–8 | An evaluator state `(Heap, Term, Stack)`, a total bounded reducer, promises recorded before driving, state matching modulo renaming, and a splitter that residualizes blocked evaluation. | Adopt the evaluator/driver/memo/split separation. Its heap represents lazy variable bindings and sharing; it is not a mutable JavaScript object heap. |
| **[BPJ10]** §3.5, pp. 6–8; §4, pp. 9–10 | Splitting must preserve work sharing; moving a binding into a returned lambda can repeat work. Recursive lazy updates need a fixed-point treatment. | Preserve SSA definitions and effect order. Do not substitute effectful or allocating expressions into multiple uses or into repeatedly invoked callbacks. Its update-frame machinery is not directly reusable for native Maps. |
| **[M10]** §§2.3–2.6, pp. 3–8; §5.1.2, pp. 10–11 | A manager separates driving, splitting, stopping and residual naming; a finite source-tag alphabet supports bag-based termination checks; local and global histories differ. | Give source and residual functions separate identities and keep graph construction independent of C++ emission. Tag bags are an alternative to tree embedding, but their proof depends on the paper's normalized `let` language. |
| **[K20]** §2, pp. 2–5; §3.3, pp. 7–9 | Multi-result driving explores ordinary driving and early generalization alternatives. `GSNone`, `GSFold` and `GSBuild` compactly represent a set of process graphs. | Retain an identity alternative immediately. Later add shared alternatives before the whistle instead of enumerating complete copies of every residual program. |
| **[K20]** §4, pp. 10–13; Appendix A, p. 19 | Select small graphs without enumerating an exponential result set; excluding unfolding nodes improves the relation between graph cost and residual cost. | Count retained residual definitions and operations, not just driving steps. The smallest process graph need not give the fastest or smallest emitted C++ program. |

The papers also give reasons to bound the implementation. [M10] §2.2.1, pp. 2–3
leaves a possible nonterminating simplifier on datatype-encoded recursion
unresolved. CTNative must bound its simplifier too. [BPJ10] §5, pp. 10–11 reports
that evaluating primitive arithmetic can destroy configuration reuse as partial
sums become part of specialization contexts; some examples grow substantially or
allocate more. [K20] §6, pp. 15–16 explicitly describes its small experimental
corpus and leaves stronger size bounds and larger-language evaluation as future
work. None of these results predicts a Bootstrap speedup.

The upstream SPSC implementations and the MRSC size-control implementation are
useful executable references, not CTJS libraries. Their input languages and
evaluation contracts differ. Reimplement the bounded mechanisms in idiomatic C++
over existing MLIR and CTNative structures; do not add a second compiler runtime
or a Haskell/Idris/F# dependency.

## Existing architecture and reuse

| Existing component | What it already establishes | What supercompilation adds |
|---|---|---|
| [PartialEvaluation](../lib/CTNative/PartialEvaluation/Heap.h) | A compiler-owned graph of objects, Maps, cells and closure environments; concrete primitive values; bounded evaluation of closed factories and static entry prefixes. | Symbolic configurations and residual graph cycles when some inputs remain dynamic. The present evaluator has no memo-folding graph or generalized configuration domain. |
| [Prefix evaluation](../lib/CTNative/PartialEvaluation/Prefix.cpp) and [residualization](../lib/CTNative/PartialEvaluation/Residualize.cpp) | Stop at a runtime boundary; preserve live prefix values, shared heap nodes and allocation freshness; reject unsupported/cyclic residual graphs transactionally. | Later use this graph as part of a versioned configuration, rather than replacing its identity and ownership rules. |
| [Binding-time analysis](../include/ctcompile/CTNative/Analysis/BindingTime.h) | Separates a known value from an operation that may execute statically at its original position. Facts are rederived and do not replace callable, environment or ownership proofs. | Online control of which configurations to drive, fold, generalize or retain. BTA remains useful for staging, but does not prove termination of this graph construction. |
| [Symbolic facts](../lib/CTNative/Symbolic/Facts.h) and [rewriting](../lib/CTNative/Symbolic/Rewrite.cpp) | Literal and primitive-domain facts; safe primitive folds and known `scf.if` selection while retaining producers. | Context-specific facts and recursive residual function promises. A fact about a normal result is not a purity or totality proof. |
| [Specialization](../lib/CTNative/Specialization/Candidates.cpp) | Exact primitive call arguments, closed-callable checks, bounded nonrecursive variants, original call routes for native inference. | Self-recursive configurations and folding, with an online growth test. Reuse literal extraction and proof helpers; keep independent pass controls. |
| [Deforestation](native-deforestation.md) | Lumberhack-inspired producer/consumer constraints for a proved native Map snapshot projection. | A future driver could expose more such opportunities; its current snapshot-time proof still applies independently after native lowering. |

Keep the primitive adapter in `PartialEvaluation` as the shared implementation of
JavaScript literal arithmetic, truthiness, comparisons and coercion. It already
reuses ctbrowser's primitive semantics. Extending that small adapter is preferable
to evaluating through the browser VM. Compiler graph nodes must never become
ctbrowser runtime heap objects or persistent runtime memo entries.

## First implementation: scalar configuration driving

### Placement and API

Add the independently opt-in module pass `--ctnative-supercompile`:

```text
resolve globals / lift structured control
  -> optional precompute
  -> optional nonrecursive specialize
  -> optional supercompile
  -> optional precompute of residual bodies
  -> optional BTA-guided heap partial evaluation
  -> native lowering
  -> optional deforest
```

The pass performs its own bounded local primitive/branch driving; enabling it does
not require the user to enable the separate whole-module precompute pass. Omitting
the supercompile flag leaves this transformation off. No optimization level should
silently select it.

The implemented ODS API is:

```text
--ctnative-supercompile='max-contexts=32 max-steps=20000 max-residual-ops=4096 max-growth=512 report'
```

| Option | Contract |
|---|---|
| `max-contexts` | Bound retained configuration nodes across the module. Each source attempt must also fit the remaining context allowance. |
| `max-steps` | Bound local driving work across the module, including work spent on rejected drafts. Never reset this allowance for every candidate. |
| `max-residual-ops` | Bound newly retained residual operations across the module. |
| `max-growth` | Bound added operations for one source function, summed over all its retained variants. Since its original generic body remains, total family size is `original + added`; the allowance is not `original + added allowance` for the new variants alone. |
| `report` | Print selected kernels, configurations, folds, whistles and residual operations. Module annotations also record steps; refused source functions carry reasons. |

These are work and IR-size limits, not C++ byte-size or runtime guarantees.
Zero allowances must refuse safely. Use checked/wide arithmetic for cost sums.

Suggested file boundaries are `Supercompilation/Driver.h`, `Driver.cpp`,
`Control.cpp` and `Pass.cpp`. `Driver` owns a source's temporary process graph and
residual drafts; `Control` owns admission and the whistle; `Pass` owns module-wide
budgets, candidate discovery and publication. Avoid growing the concrete heap
evaluator into a second graph controller.

### Admission and configuration identity

Use a private, original numeric-index CTJS function with one entry block and no
captures. Its implicit receiver/new-target/callee arguments may only participate
in supported scaffolding, not be observed by the kernel. Require rederived
closed-callable and exact-arity checks. Preserve the original implicit argument
positions and the callee value needed by the boxed dispatcher.

The body admits a small explicit set of primitive operations, supported frame/root
scaffolding, returns, `scf.if` regions and ordinary self calls. Reject heap access,
allocation, cells, closures, unknown calls, constructor calls, runtime global
observations, mutual recursion and unsupported CFG/loop regions. A global load
used solely as the already-proved direct callee can remain scaffolding; it is not
permission to execute arbitrary global loads at compile time.

An operation whitelist alone does not prove all actual inputs scalar or all
dynamic coercions pure. Only evaluate literal operands with the primitive adapter.
Retain dynamic operations at their existing SSA position, including any runtime
coercion they perform. Native type admission must still prove that the final
function can use native carriers.

The first configurations are function-entry states:

```text
Configuration = (original source function, argument bindings)
Binding       = Known(exact primitive attribute) | Dynamic(parameter position)
Promise       = (Configuration, fresh residual symbol, ordered formal arguments)
```

This is an entry-only representation: external continuations remain in the caller
and are never pushed through a recursive call. It does not match arbitrary nested
expressions by their callee name. Dynamic actual expressions have already been
evaluated to SSA values before the call; pass those values in the same positions.
Their SSA names are not part of the memo key, and their evaluation is not repeated.

Known numeric arguments use exact MLIR attributes, including signed zero and NaN
payloads. The termination abstraction below may erase those payloads; semantic
matching may not. Primitive extraction must respect existing string-size limits.
Input `ctnative.*` annotations cannot authorize any optimization or substitute for
the closed-callable checks.

### Driving, splitting and folding

For a source kernel, build the entire alternative in temporary IR before
redirecting original calls:

```text
drive(configuration, ancestor history):
    if an exact promise exists:
        return its residual call                         // fold first
    if an ancestor shape embeds this configuration:
        return the original generic call                 // whistle
    if work or graph allowance is exhausted:
        reject the source attempt                        // identity alternative
    create a fresh residual symbol and record its promise
    drive a draft of the source under its known arguments
    select proved branches; retain both arms of unknown branches
    for each retained self call:
        derive its argument configuration and drive/fold/stop it
    record the finished residual definition
```

Registering the promise **before** driving is essential [BPJ10 §3.4]. The body is
not required to finish before another occurrence can refer back to it. Do not
derive return facts from an incomplete promise. In particular, a temporary draft
whose recursive reference is unresolved must not be given a fabricated constant
return merely to enable branch selection.

Use existing symbolic literal rules and `scf.if` rewriting for the local driver.
At an unknown branch, keep the runtime condition and both regions. The first
slice need not infer new relational facts from those branches. Such facts would
later become part of the configuration and be scoped to the appropriate arm.

A representative shape is:

```javascript
function accumulate(mode, n, acc) {
  if (n <= 0) return acc;
  if (mode) return accumulate(mode, n - 1, acc + n);
  return accumulate(mode, n - 1, acc - n);
}
// An eligible call with mode=true and runtime n/acc seeds a configuration.
```

The residual `true` configuration retains the runtime `n <= 0` test, removes the
mode branch and recursively calls its own promised residual function. Its result
depends on dynamic inputs; it cannot be replaced by a compile-time constant.
Repeated exact seeds share a residual definition. A changed numeric static
argument encounters the growth policy and retains a generic recursive call;
there is no requirement to unroll a sequence of changing constants.

Keep at least one original external generic call for native inference evidence.
The current inference system derives information from actual call routes; merely
leaving an unused source definition is insufficient. Seed only original indexed
callers/functions so a generic whistle call inside generated output cannot start
a fresh expansion on every pass invocation. This selection policy is separate
from correctness proofs and must be tested by applying the pass twice.

Construct draft IR with the original call arity and types, and check source-family
growth and remaining module allowances before publication. The pass pipeline
verifies the published module. A rejected attempt must leave all original symbols and calls intact.
Folding redirects **code**, not runtime results: each invocation still executes
its residual body. There is no runtime memoization cache.

This is a restricted configuration-driving/folding slice and overlaps polyvariant
partial evaluation. Its distinguishing acceptance evidence is a finite residual
graph with a backedge to a promise created before driving, plus correctly retained
dynamic control. Substituting literals in a nonrecursive clone, or unrolling a
recursive function a fixed number of times, is not enough. It does not yet provide
the papers' general nested-context driving, recursive constructor fusion,
anti-unification, or heap-aware supercompilation.

## Termination control and later generalization

For the first whistle, represent the argument tuple as a finite tree whose leaves
are `Dynamic`, `Number`, `String`, `Null`, `Undefined`, `True` or `False`.
Numbers and strings lose their payload only in this tree. Use the usual coupling
and diving rules: equal labels embed when their children embed pairwise, and a
tree embeds a larger tree when it embeds one of that tree's children. Compare
states of the same source kernel. These tuple/leaf shapes are a small instance
of homeomorphic embedding, not a heap-graph embedding algorithm.

The finite alphabet matters [R18 §§2.4–2.5, pp. 9–13]. Retaining every newly
computed integer or freshly generated residual symbol as a distinct incomparable
label would invalidate this termination argument. If symbolic expression trees
are added, label them by a finite set of original operation identities and
bounded-arity constructors. Allocation IDs and mutation version numbers also
require abstraction for a future whistle, while remaining precise for matching.

Check exact promises before the whistle. Otherwise even a useful invariant-mode
recursive call would stop because its shape embeds itself. Boolean values can
remain separate finite labels, allowing a small alternating-state graph to fold
if admitted; unbounded numeric/string progress still whistles. Shape equality or
embedding alone must never create a semantic backedge.

The finite graph argument does not replace practical budgets. [BPJ10 §3.6] and
[M10 §2.3] distinguish divergence inside reduction from divergence while building
the outer residual graph. Here local SSA/structured-region driving has its own
step limit, and graph expansion has history/context limits. If a later splitter
restarts driving after a stop, prove it descends to smaller states or revisits
only a finite set; resetting history indefinitely defeats a whistle.

**First implementation: whistle means a retained generic call. Generalization is
not implemented by naming that fallback a generalization.**

The next scalar generalizer should compute a configuration `G` and substitutions
`old = G[old_args]`, `new = G[new_args]`. Preserve common source/control structure
and stable literal modes; turn growing accumulator components into residual
parameters. Preserve relationships between repeated variables, and evaluate each
substitution expression once at its original strict evaluation point. Explicit
SSA bindings provide the role of the papers' `let` expressions.

If `G` generalizes an ancestor, replace that ancestor and invalidate/rebuild all
dependent draft nodes and promises transactionally [R18 §2.3]. Merely changing its
memo key while retaining the old specialized body is unsound. For a bounded first
generalizer, static positions may only become dynamic, never become static again
during that attempt; bound restarts as well. This gives a finite weakening order
in addition to the embedding check. More expressive generalization requires its
own termination argument.

## Heap identities, versions and effects: the next representation

The following is a CTNative adaptation, not a mutable-heap algorithm supplied by
the five papers. [BPJ10]'s lazy environment heap is a useful organizational model
but does not describe Map mutation, JavaScript property access or DOM callbacks.

Before admitting these operations, extend a configuration to carry:

```text
(control point, SSA environment, residual continuation,
 reachable heap graph, dependency versions, ordered effect frontier, path facts)

HeapNode = (allocation identity, kind, version, payload, ownership/escape facts)
```

Allocation identity and mutable state must remain distinct. Two objects with equal
fields are still different objects; two fields may refer to the same Map; two
factory invocations must construct independent runtime graphs. Closure code
identity and captured identities belong to the graph too. A known closure is not
permission to execute its future body.

Each proved mutation creates a new node version. A read fact records the version
and aliases on which it depends. Preserve Map insertion order, deletion/reinsertion
history as reflected in that order, key equivalence and object-key identity.
A `keys()`/`values()` snapshot belongs to its construction-time state; substituting
a projection from a later Map version changes the program. Existing deforestation
proofs already enforce this observation-time rule.

Canonicalizing configurations may rename private allocation IDs through a
bijection that preserves the full alias partition, ordered entries, closure
captures and observable identities. It may not merge structurally equal nodes.
Version/dependency compatibility and the effect frontier must also match before
folding. Initially reject different alias partitions and unproved version
relationships. Heap generalization must parameterize retained state and effects;
erasing a write, forgetting an alias, or dropping a version from a cache key is
not a valid generalization.

Use the existing compiler-owned `snapshot` graph as the starting representation.
Extend values with residual SSA references only when the residualizer can preserve
their dominance, evaluation time and sharing. Keep the existing rejection of
unsupported ownership cycles until a separate ownership proof handles them.
Residualize every live node once per factory invocation, then establish its edges;
do not share a compile-time allocation globally between runtime invocations.

Effects impose a stricter contract than the pure functional examples:

| Operation class | Required treatment |
|---|---|
| Literal primitive operation | Evaluate using the existing JavaScript adapter, with exact coercion/number behavior. |
| Dynamic primitive operation | Retain at its original SSA position; no algebraic assumptions such as `x * 0 == 0`, `x + 0 == x`, or `x == x` for arbitrary JavaScript values. |
| Proved fresh private-heap operation | Execute only with BTA, environment, identity and ownership evidence; record mutations in the private attempt's heap. |
| Dynamic call with a known normal return | Preserve the call once and in order. Its return fact does not prove absence of effects, exceptions or divergence. |
| Unknown call, getter, host operation, escaping callback or unproved coercion | Retain runtime work. Initially end/reject the driving region; a later resumable driver must invalidate all reachable dependencies the operation may affect. |
| Failed nested evaluation after a private mutation | Roll back the entire attempted prefix/configuration. Do not resume by replaying the call against its partially mutated compiler heap. |

Future host contracts must state read/write sets, throws, callback invocation and
escape, allocation/identity behavior and determinism. These contracts must be
rederived from trusted compiler knowledge. Bootstrap names such as `Data` or `Map`
and input annotations do not themselves establish those contracts.

Preserve argument evaluation, runtime effects and observable exceptional behavior,
including when an argument's result becomes unused. [R18 §3, pp. 20–23] explicitly
shows that lazy driving can turn strict `erase(omega(x))` into a terminating
constant. The weaker equivalence accepted for some historical optimizers is not
CTNative's contract. Neither a known return nor an unused result permits dropping
the original computation. Moving a definition into a repeated callback is also
forbidden without the sharing/effect proof discussed in [BPJ10 §3.5] and [M10 §2.5].

## Residual alternatives and code-size control

The first implementation has two alternatives per source: the unchanged program
and one bounded driven graph. Select the driven graph only after it performs a
proved simplification and fits both source-family and module allowances. All
variants count, including ones reachable only through other variants. Count
recursive definitions once, rather than following their backedges indefinitely.
This is a limited choice inspired by multi-result supercompilation, not the full
[K20] algorithm.

For later multi-result work, make a driving step return a compact set of options:
ordinary driving, a let-generalized state where copying would duplicate work,
and a residual boundary. Share common subgraphs rather than enumerating their
Cartesian product. [K20 §3.3] adds generalization where unfolding duplicates
nontrivial arguments, and avoids it when no duplication is expected. These choices
must occur before a whistle; waiting until after arbitrary expansion misses the
benefit of its approach.

Bound alternatives and graph nodes, then retain a small set of candidates by a
cost tuple such as `(residual operations, residual allocations, call overhead)`.
Ignore driving-only/unfolding nodes that emit no code, as motivated by [K20 §4].
Evaluate costs after residual sharing is established. Global shared-definition
costs need explicit accounting; choosing each locally smallest child is not a
proof of a globally smallest C++ program. Keep the unchanged program among the
options and measure emitted C++ size and runtime separately.

## Acceptance gates and staged follow-up

The first implementation is present in `Supercompilation/`. Its source fixture
admits all 14 functions, creates five residual configurations, folds six repeated
configurations and retains one generic call at a whistle. All six native CTests
pass, including ordinary/deduced output, GCC/Clang, no-VM checks and a deliberately
wrong output. The lit test also covers exact signed-zero/NaN keys, two-state
recursive folding, retained argument effects, divergent residual calls, repeated
passes, forged metadata and each independent budget. Integration and vendor
measurements are recorded in [the roadmap](native-pe-roadmap.md).

The first slice needs both structural and executable evidence:

| Gate | Required observation |
|---|---|
| Invariant recursive mode with runtime counter/accumulator | A residual function calls its own promise; the static mode branch disappears; the runtime base-case branch remains; results agree for empty/base and several recursive cases. |
| Repeated seeds and different primitive modes | Exact seeds reuse code; distinct values do not alias memo entries. Preserve at least one actual generic source call. |
| Changing static numeric accumulator | A whistle retains a generic recursive call; compilation terminates within limits without manufacturing infinitely many variants. |
| Numeric and coercion edges | Signed zero, NaN, infinities, boolean/number distinctions and supported literal coercions agree with reference execution; no floating-point reassociation. |
| Dynamic argument evaluation | Effectful caller argument producers remain once and in order even if specialization makes the corresponding formal unused. |
| Refused bodies | Heap mutations, dynamic globals, host calls, closure/cell state, mutual recursion, constructor state and unsupported control remain unchanged. |
| Budget exhaustion | Tiny/zero work, graph, residual-size and growth limits preserve the original program; rejected work still consumes the module work budget. |
| Repeated pass and fabricated annotations | A second invocation does not grow new families from generic boundaries; input annotations cannot bypass proof or refusal. |
| Native output | Generated and deduced C++ compile/run; native observations agree with reference output; no VM symbols are introduced; normal admission remains enforced. |

Compiler-only tests can retain a syntactically diverging recursive argument and
assert it remains; do not execute an intentionally nonterminating fixture. Use
finite effect/throw probes where supported for runtime order checks.

Add heap support only with tests for shared versus equal-but-distinct nodes,
independent factory invocations, alias writes across calls, old snapshots after
mutation, Map object keys and order, immutable closure captures, dynamic captured
state and rollback after a failed nested evaluator call. Then add scalar
generalization tests with growing accumulators and changed variable relations,
followed by compact multi-result tests that demonstrate avoided code duplication.

Each stage remains optional and reports its own proof boundary. Run source
observations, focused lit checks and the native/deduced pipelines on the devbox,
then the repository's required gate before committing implementation changes.
Record Bootstrap admission counts, exact Data probe behavior and boxed vendor
output separately from optimization statistics. A smaller scalar fixture or a
removed Map snapshot is not evidence of whole Bootstrap execution.

## References and local copies

- **[KR18]** Andrei V. Klimov and Sergei A. Romanenko. *Supercompilation: main
  principles and basic concepts* (Russian). Keldysh Institute preprint 2018 No. 111,
  36 printed pages. DOI [10.20948/prepr-2018-111](https://doi.org/10.20948/prepr-2018-111).
  [Supplied PDF](../../../academic-papers/supercompilation/Supercompilation-main-principles-and-basic-concepts-IN-RUSSIN-NOT-ENGLISH.pdf).
  Executable examples: [SPSC](https://github.com/sergei-romanenko/spsc) and
  [SPSC Idris](https://github.com/sergei-romanenko/spsc-idris).
- **[R18]** Sergei A. Romanenko. *Supercompilation: homeomorphic embedding,
  call-by-name, partial evaluation* (Russian). Keldysh Institute preprint 2018
  No. 209, 32 printed pages. DOI [10.20948/prepr-2018-209](https://doi.org/10.20948/prepr-2018-209).
  [Supplied PDF](../../../academic-papers/supercompilation/Supercompilation-homeomorphic-embedding-call-by-name-partial-evaluation-IN-RUSSIN-NOT-ENGLISH.pdf).
- **[BPJ10]** Max Bolingbroke and Simon Peyton Jones. *Supercompilation by
  Evaluation*. Supplied preprint dated 2010/6/28; the cited page numbers are 1–12
  in that copy. Published in Haskell 2010, pp. 135–146.
  [Supplied PDF](../../../academic-papers/supercompilation/Supercompilation_by_Evaluation.pdf).
- **[M10]** Neil Mitchell. *Rethinking Supercompilation*. ICFP 2010, pp. 309–320;
  cited pages are 1–12 in the author's supplied copy, dated 29 September 2010.
  DOI [10.1145/1863543.1863588](https://doi.org/10.1145/1863543.1863588).
  [Supplied PDF](../../../academic-papers/supercompilation/paper-rethinking_supercompilation-29_sep_2010.pdf).
- **[K20]** Dimitur Krustev. *Controlling The Size Of Supercompiled Programs
  Using Multi-result Supercompilation*. [arXiv:2006.02204v1](https://arxiv.org/abs/2006.02204v1),
  3 June 2020. [Supplied PDF](../../../academic-papers/supercompilation/Controlling-The-Size-Of-Supercompiled-Programs-Using-Multi-result-Supercompilation-Dimitur-Krustev.pdf).
  Implementation linked by the paper: [MRScpOptSize](https://github.com/dkrustev/MRScpOptSize).
