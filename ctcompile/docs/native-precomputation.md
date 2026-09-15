# Native symbolic precomputation

`--ctnative-precompute` folds proved primitive expressions and selects known
structured branches before native lowering. Its facts distinguish a literal,
a primitive result domain, and an unknown runtime value. Numeric domains include
NaN; they do not justify self-equality, multiplication by zero, addition of zero,
or reassociation. Boolean, string and nullish result domains exclude NaN and can
justify self-equality.

Literal arithmetic, comparisons, coercion and truthiness use the existing
compiler-side primitive adapter from partial evaluation. It uses ctbrowser's
operator helpers, canonicalizes tag-colliding NaNs and bounds strings to 64 KiB.
No object reference crosses the adapter, and generated C++ links no interpreter
or collector. Unsupported literal operations remain expressions.

Facts about a normal result do not authorize removing its producer. For example,
a direct callee that stores a global and returns `40` still executes when its
caller reduces `result + 2` to `42`. The pass preserves calls, unknown comparisons
that may coerce objects, and all other runtime producers. It replaces only scalar
operations whose own semantics are proved and copies no heap state.

Direct-call summaries derive from all normal returns in the visible body, with
every function and region argument initially unknown. Caller literals do not
freeze a generic function. Summaries start unknown and propagate through bounded
rounds; recursive dependencies cannot invent a result fact. The initial summary
consumer accepts only ordinary calls with a proved undefined `new.target`.

For a known `scf.if` condition, the pass moves the selected region's operations
to the original branch position, replaces yielded values, and removes the
unselected region. Selected effects execute once and keep their order. Unknown
conditions remain branches. Loops are not unrolled, and their carried arguments
remain unknown; safe literal work within their bodies can still simplify.

`max-steps` defaults to 100,000 analysis/rewrite visits. Exhaustion leaves remaining
expressions and control intact. Each completed local rewrite is independently
valid, so no partially executed runtime effect needs rollback. Input
`ctnative.symbolic_results` and `ctnative.precompute_summary` annotations are
discarded and rederived. The summary records expression and branch rewrites,
steps, and budget exhaustion. `report=true` also emits those counters.

`lib/CTNative/Symbolic/Rewrite.cpp` owns scalar replacement for `ctjs.binary`,
`ctjs.binary_static`, `ctjs.unary`, `ctjs.compare`, `ctjs.truthy`, and
`arith.trunci`: one `OpRewritePattern` template, instantiated per root, that
captures the current analysis by reference, adapts a proved literal to the
result representation, and replaces only the root with a CTJS or integer
constant of the root's own type. Analysis, budget scheduling, and
structured-region splicing stay beside it. The patterns live within one
invocation, with no global state or trusted proof attributes. (Until
2026-09-15 the six patterns were a PDLL file with native callbacks; every body
was already one C++ call, so the file bought an mlir-pdll build step and a
PDL bytecode interpreter for nothing a template does not say.)

The driver derives scalar candidate kinds from the patterns' root kinds and visits
them once in the same postorder as structured branches. A candidate without a
literal still consumes its original budget step. It uses `PatternApplicator`
directly, so greedy folding or dead-code elimination cannot remove producers,
retry candidates, or perform unbudgeted work.

The source fixture `test/CTNative/Fixtures/Optimization/symbolic.js` covers literal coercions,
NaN, signed zero, infinities, symbolic boolean/string results, varying inputs,
effectful producers and chosen branch order. Standalone lit cases pin retained
unknown operations, original call multiplicity, precise numeric literals,
forged-fact rejection, repeated invocation, and budget exhaustion. The dedicated
`test/CTNative/Precomputation/precompute-pdll.mlir` exercises every scalar pattern with proved
and unknown values, integer widths, and exact step limits around scalar and
branch rewrites. Native reference comparison remains separate from structural
optimization evidence.

This pass runs by default at the native lowering entry, before reachability;
see [native optimization defaults](native-optimization-defaults.md) for opt-outs
and limits. Specializing a function supplies explicit literals; precomputation then simplifies its
residual body. Heap ownership, closures and host effects still require their
separate native proofs. Full Bootstrap support is not implied by this fixture.
