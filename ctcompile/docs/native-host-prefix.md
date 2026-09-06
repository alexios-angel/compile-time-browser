# Checked host entry-prefix specialization

This Bootstrap increment adds a narrow consumer of the driver-bound
`closed-source-v1` contract. It does not change the meaning of the complete
host proof in [host-slot analysis](native-host-slots.md).

The exact CommonJS/browser probes initialize ordinary source objects and then
invoke the UMD wrapper once. The wrapper's `typeof` tests select an environment
before the factory can perform a host operation. The prefix analysis proves
that selected control flow and the actual factory closure while retaining the
factory's effects. Separate checks of the initial intrinsic identities and
continuation exclude exposure of the active wrapper through a host callback.

## Proof boundary

The analysis starts at the declared script entry with the manifest's absent and
present-undefined bindings. It tracks executed source global writes, fresh
ordinary object fields, primitive constants, and source closure identities.
It visits straight-line blocks, selected structured branches, and a unique
exact source-call path. It records branch choices and indirect-call targets;
it does not execute the source or residualize its heap.

The first unknown value needed by control, unknown effect, unsupported property
operation, recursive/repeated call context, or work limit ends the prefix.
No result or write after that boundary is assumed. Calls that may throw remain
in the generated program, including the boundary operation itself. Budget
exhaustion and invalid contracts discard every candidate rewrite.

A function body can be specialized only when its sole actual closure producer
is used only by one exact direct callee operand and root bookkeeping. This
proves one invocation path back to the entry. Private visibility is insufficient.
Global declarations are publication: later `this.f` or `globalThis.f` reads can
retrieve them without a `load_global` use. Such bodies are not specialized in
place, nor are closures stored in properties, captured or passed as arguments.
The body's implicit callee argument must also have only root bookkeeping uses:
a named IIFE can otherwise save itself without another use of its creation.
Reflection is checked across the entire source, including the unconsumed
suffix. Caller/callee/arguments properties, dynamic property keys, argument
objects, dynamic import, code evaluation, descriptor/prototype reflection and
unmodeled host bindings prevent in-place body specialization.
An arbitrary later source assignment does not establish an initial binding's
identity. A global load must refer to a declared initial identity, a known value
already established by the executed prefix, or a direct source initializer
that dominates the load in the same function.
Unknown future calls to a closure, repeated wrappers, observed pending
`new.target`, and source mutation of fixed host bindings refuse specialization.
Every proof is tied to current semantic IR; forged report attributes and stale
manifests cannot authorize a rewrite.

## Consumer

The opt-in consumer selects only proved `scf.if` arms and resolves proved
`ctjs.call` targets. It preserves callee values, receiver values, argument
evaluation and invocation order. It neither executes a factory early nor
replaces publication slots with immutable function symbols. Existing native
analysis can then inspect the selected wrapper and direct factory call.
Selected operations move directly into the enclosing block and their yield
values replace the branch results; unrelated structured branches stay intact.

The first implementation stops at the resolved factory call itself; it does
not need to enter the still-open factory body to name its actual callee.
Before changing a shared body, a separate structural check follows its
continuation and known callees to exclude caller exposure while that body is
active. It admits fresh own-data objects, source cells and closures, and the
declared standard zero-argument Map constructor. An opaque callback, property
receiver, coercion, or unsupported operation discards all proposed changes.
This check does not execute or fold any continuation operation, derive its
return value, or claim that allocation and publication are pure or nonthrowing.
Receivers remain explicit. A future typed method may use a C++ instance
receiver only after its own receiver/identity proof; arrow lexical receivers
and the realm receiver are not substituted by this pass. When interpreting a
direct callee, its effective `this` remains unknown: the raw call operand alone
does not establish sloppy-call substitution, boxing, or lexical semantics.
The exact wrapper uses its separate explicit fallback parameter, so this
restriction does not change the measured UMD branch proof.

## Explicit initial provider identities

The exact driver extends its existing manifest with:

```json
{
  "initial_intrinsics": ["Map", "Array"],
  "realm_global_this": true
}
```

These optional fields default to no additional knowledge. The embedding must
supply the standard initial Map and Array constructors and, when declared, its
actual realm `globalThis` binding. The differential driver uses the runtime's
`install_builtins` and ordinary classic-script entry to establish those facts.
An embedding that replaces an intrinsic before the supplied program runs does
not satisfy this manifest. The compiler cannot inspect external host state.

Only those two intrinsic names are accepted. Their direct binding replacement,
possible replacement through a named global-view property, constructor aliases,
prototype access, and Array.from replacement/escape refuse the supplied
identity. Dynamic property access independently blocks body specialization.
The realm binding is an opaque realm view, not a fresh ordinary object or a
substitute for a function's receiver; source replacement of `globalThis` remains
distinct from the stable realm receiver.

These identities close the possible external caller-inspection boundary; they
do not prove Map/Array operations pure, nonthrowing, or safe to execute early.
The consumer still stops at the factory invocation. In particular, the
complete `HostContractAnalysis::property` query remains unavailable for the
exact Data probes. Unknown intrinsic/provider declarations and forged IR
attributes cannot widen this trust boundary.

```sh
ctjs-opt prepared.mlir \
  --ctnative-specialize-host-prefix='manifest=host.json output=prefix.json report=true' \
  -o specialized.mlir
```

## Exact-source evidence

`tools/check/bootstrap-host-prefix.py` uses the existing Data probe extractor.
It retains the exact 1,101-byte Bootstrap 5.3.8 fragment with SHA-256
`3eebf5b59691eff3c32478e06fb44a5c9eb554bc045d2ff2e20901813a9f99c8`.
Each exact mode retains all seven source functions and declares the same
19 numeric observations. The measured compiler results are:

| Mode | Proved branch choices | Resolved factory calls | Native claimed | Refused | Pruned |
|---|---:|---:|---:|---:|---:|
| CommonJS | 2 | 1 | 0 | 7 | 0 |
| Browser | 5 | 1 | 0 | 7 | 0 |

Both selected wrappers contain no remaining UMD branch and one direct call to
the actual `fn$3` factory, retaining its original callee operand and receiver.
The analysis stops at that call with the diagnostic `resolved call body remains
a runtime effect boundary`. The result advances wrapper control and target
proof, while native admission remains 0/7. Explicit script receivers, boxed
public parameters, the open returned method table and captured closure
identities remain admission frontiers. The complete host-property proof is
still withheld; `full_host_contract_claimed` and `native_execution_claimed`
remain false in the evidence.

The differential driver installs only the generated boxed wrapper. Factory
and Data methods retain interpreter dispatch, so this execution check measures
the prefix transformation rather than a native Bootstrap executable. It
checks the exact numeric observations and the expected compiled invocation
count under ordinary execution and GC stress. Separate global-publication and
implicit-callee-publication fixtures retain both invocations and require the
second invocation to observe the changed host value.
The four registered `ctcompile_bootstrap_host_prefix_*` differential tests pass:
both exact modes match all 19 observations in ordinary and GC-stress execution,
and both reentry controls preserve the final observation `trace = 2` after two
compiled invocations. The three existing host-slot tests and the live
`HostContractAnalysis::property` unit test also pass.

The focused lit suite covers four useful source consumers and 16 refusals,
including mutable source slots, unknown and throwing effects, terminating
control arms, effective receivers, repeated callers, accessors, late host
initialization and opaque callbacks. It also checks the guarded standard Map
factory, source intrinsic replacement and aliases, caller reflection, forged reports and declarations,
stale manifests and exhausted work. AMD's retained and delayed callable remains
outside this first specialization path. Its invocation identity, full provider
effects and owning publication-table flow need further proofs.
