# Native direct-call specialization

`--ctnative-specialize` creates private function variants for exact primitive
arguments while retaining runtime parameters and effects. For example,
`formula(4, 5, input)` can call a variant containing the constants `4` and `5`;
the later primitive and heap evaluators can simplify its initialization without
executing `input` or removing its producer.

## Eligibility and rewrite

The pass requires a private function with a visible body, one entry, no captures,
and no observation of its receiver, constructor state or own callee value.
Closure/cell construction and capture operations are excluded. Recursive call
graphs remain generic. Numeric-index closure uses are checked independently of
private visibility, using the same closed-callable proof as heap evaluation.

Only literal primitive operands at explicit argument positions are candidates.
Unknown positions remain parameters. The key includes every selected attribute
and its position; it preserves primitive tags and IEEE number bits, so `+0` and
`-0` receive distinct variants. Strings are limited to 64 KiB. Identical tuples
share a variant within the pass invocation. All call operands and their original
evaluation order remain present, including arguments that become unused inside
the specialized body.

The pass retains one actual generic call to each source function, preserving
its original native inference evidence. It clones the body, removes incoming
native proof annotations, replaces selected parameter uses with constants, and
redirects the call's symbolic target. The original global callee value remains:
boxed dispatch uses that value, while native calls use the specialized symbol.

A redirected call does not make the original declaration escape when the new
target cannot observe its callee slot. The shared proof rechecks that the target
is private, visible, capture-free, uses that slot only for rooting, and contains
no closure or cell environment operations. It never trusts
`ctnative.specialized_from` or `ctnative.specialized_arguments` to establish
non-observation. Targets that inspect the callee or declare captures are refused,
even when input IR supplies matching provenance attributes.

The original body can still receive arguments through that retained boxed callee
value. BTA therefore leaves its parameters dynamic whenever the value also serves
an alternate symbolic target. Counting only calls to the original symbol would
miss those tuples and could incorrectly freeze the generic body. This check
follows immediate closure uses and their proved global declarations; it does not
weaken the Map or closed-callable proofs. Argument-independent initialization can
still be evaluated.

## Limits and pipeline

The default budgets permit 32 new variants and 4,096 cloned body operations per
invocation, including inserted constants. Exhausted budgets retain the affected
generic call and record a reason. `max-variants` and `max-cloned-ops` configure
these limits; `report=true` reports variants, redirected calls, cloned operations
and budget refusals. Repeated invocations rederive eligibility from current IR;
provenance is diagnostic data. Already substituted parameters are not selected
again. Additional literal opportunities exposed in other bodies may be considered
on a later invocation under its own budget.

The optional pipeline is:

```
ctjs-resolve-globals
ctjs-lift-to-scf
ctnative-precompute
ctnative-specialize
ctnative-precompute
ctnative-partial-evaluate
ctnative-prune-unreachable
ctnative-lower-to-emitc
```

Native ownership and type admission remain mandatory. Default boxed and native
pipelines do not enable specialization implicitly. This is bounded argument
specialization, not general supercompilation or arbitrary symbolic execution.

Retaining a generic call before heap evaluation does not guarantee that it
survives afterward: evaluating an entire caller can remove its calls and leave
private helpers without native type evidence. The optional
[reachability pass](native-reachability.md) removes proved unreachable private
definitions after PE. Published closure values still retain their targets. The differential fixture deliberately retains
runtime consumers with observable writes so that it measures specialized
initialization and residual execution together.

## Validation

`test/CTNative/specialization.mlir` checks exact tuple reuse, distinct signed-zero
variants, both budgets, reuse after a budget is reached, a repeated pass,
recursive refusal, retained argument producers and body effects, original callee
values, forged provenance on observing or captured targets, and parameter
preservation when a closure serves alternate symbolic targets. Existing PE/BTA
cases cover the shared callable proof.

`test/native-specialization-fixture.js` combines arithmetic variants, shared
static tuples, runtime Map mutation after initialized prefixes, global writes,
argument ordering, boolean/null coercion and an unspecialized recursive helper.
The pass creates 12 variants and redirects 17 calls with 175 cloned operations.
The next stages simplify expressions, branches and initialized heap prefixes.
All 24 functions pass native admission;
the fixture's 20 numeric observations agree with ctbrowser.
The optional pipeline requires nonzero specialization, primitive precomputation
and heap evaluation before accepting native output. Its six native CTests compare
all numeric observations with ctbrowser, reject an altered observation, compile
ordinary and deduced C++ with GCC and Clang, check for VM symbols, and validate
the type-deduction pins.

The separate `ctcompile_specialization_boxed_dispatch` CTest applies specialization
and heap evaluation, then unreachable-helper pruning before ordinary boxed lowering. It installs generated entries
for both the generic function and its caller, then compares the result and global
write with interpretation. Calls using tuples `1` and `2` must produce `1020`;
freezing the original body to its remaining symbolic tuple would produce `1010`.
The generator requires both transformations to run and both boxed entries to
survive lowering.
