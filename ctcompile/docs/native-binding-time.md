# Native Binding-Time Analysis

Binding-Time Analysis (BTA) identifies values known during compilation and
operations that can belong to a static initialization prefix. It supplies the
partial evaluator with candidates; exact evaluation, closed-environment checks
and native ownership proofs still decide whether specialization is permitted.

For example:

```js
function initialize(input) {
    const state = new Map();
    state.set("value", 10);
    state.set("value", 40);
    const before = state.get("value");
    state.set("value", input);
    return before * 100 + state.get("value");
}
```

With different runtime inputs, BTA recognizes the fresh Map, its initial writes
and the saved value as static. The write of `input` starts the runtime suffix.
Heap residualisation emits one fresh Map initialized to 40 and a saved scalar
40, then retains that write and the following computation. The saved scalar
does not change when the Map changes. Repeated calls receive independent Maps.

## Facts and effects

The public `BindingTimeAnalysis` API distinguishes `Static`, `Dynamic` and
`Unknown` SSA values. Its operation query also includes control and effects at
that execution point. A constant inside an unknown branch is a known value,
but executing an allocation or write in that branch is dynamic.

Facts are computed from IR and native Map proofs, never trusted from input
annotations. The analysis tracks fresh object and Map allocation identities,
known own fields and an aggregate of Map payloads. A Map lookup can miss;
payload information alone proves neither a literal key nor a definite object.
Dynamic writes, unknown calls, global publication and possible implicit calls
invalidate tracked heap contents. Earlier scalar snapshots remain known.

Private, capture-free functions can receive static arguments when every visible
direct caller supplies the same primitive literal. Numeric-index closure uses
and declaration loads must prove that other callers cannot reach the function.
Unknown module invocations and host reads disable this argument seeding.
Receiver, constructor state and closure identity remain dynamic.

Bottom-up summaries recognize complete static callees, independent of source
order. An effectful callee that returns a constant remains dynamic, including
through forwarding calls. Recursion, loop invariants and non-entry CFG blocks
are conservative. Structured branch joins retain only compatible facts; unknown
control cannot promote a heap write into initialization.

The analysis does not establish that shared prototypes or the host environment
are immutable. The partial evaluator separately enforces those module-wide
guards and checks each operation while executing it in a bounded compiler heap.
An unsupported operation, live cycle or exhausted budget leaves the attempted
rewrite unapplied. See [heap residualisation](native-partial-evaluation.md).

## Inspecting and consuming BTA

Run `ctjs-opt input.mlir --ctnative-binding-time-analysis='report=true'` to inspect:

| Annotation | Meaning |
|---|---|
| `ctnative.argument_binding_times` | Entry argument facts for each function |
| `ctnative.result_binding_times` | SSA result facts for each operation |
| `ctnative.binding_time` | Operation eligibility including control and effects |
| `ctnative.binding_time_reason` | Explanation of that eligibility |
| `ctnative.binding_time_summary` | Static/dynamic operation counts and module argument count |

Repeating the pass discards and rederives all these annotations. The partial
evaluator directly constructs the analysis; running the annotation pass first
is optional and cannot grant permission to execute forged static operations.

Implementation is split into `Analysis/BindingTime/` for facts, memory, operation
transfer and control flow, `BindingTime/Pass.cpp` for reporting, and
`PartialEvaluation/` for execution and residualisation. The shared callable-use
proof lives in `Analysis/ClosedCallable.cpp`.

## Bootstrap boundary and validation

The source-derived Data probe imports Bootstrap's exact initializer and exercises
its 19 browser-mode observations. BTA keeps host reads, calls and publication
dynamic while identifying local literal facts. Its public, indirectly called
functions remain outside the private factory candidate set. The checker verifies
complete annotations, repeatability and rejection of forged host-read facts; a mutation
of a real host-read result must fail the checker. Partial evaluation must retain
the Map initializer and its three latent methods. These are analysis checks,
not evidence that the complete Data initializer executes natively.

Lit regressions cover static/dynamic arguments, later heap mutations, aliases
passed to dynamic callees, inherited getter hazards, Map misses, effectful call
chains, dynamic branches and loops, escaped closures and forged metadata.
The partial-prefix source fixture checks the resulting standalone C++ against
ctbrowser for shared children, independent calls, saved scalar values, object
keys, retained branches/loops, global effects and calls that must run once.

The combined devbox gate passes 363/363 CTests and 114/114 lit cases. Both new
native fixtures pass ASan/UBSan with leak detection, and all 450 C++ files pass
the pinned formatting check.

Next work needs residual closure environments and explicit host/effect contracts
to specialize Bootstrap's module initialization. This slice stops at one
straight-line entry prefix; it does not split arbitrary regions or freeze DOM
queries, component state, event registration or export publication.
