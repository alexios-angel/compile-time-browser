# Checked host entry-prefix specialization

This Bootstrap increment adds a narrow consumer of the driver-bound
`closed-source-v1` contract. It does not change the meaning of the complete
host proof in [host-slot analysis](native-host-slots.md).

The exact CommonJS/browser probes initialize ordinary source objects and then
invoke the UMD wrapper once. A separate browser fallback probe supplies the
stable classic-script realm receiver and its writable publication slot.
The wrapper's `typeof` tests select an environment before the factory can
perform a host operation. The prefix analysis proves
that selected control flow and the actual factory closure while retaining the
factory's effects. Separate checks of the initial intrinsic identities and
continuation exclude exposure of the active wrapper through a host callback.

## Proof boundary

The analysis starts at the declared script entry with the manifest's absent and
present-undefined bindings and optional explicit realm receiver. It tracks
executed source global writes, fresh ordinary object fields, primitive
constants, and source closure identities.
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
are not substituted by this pass. When interpreting a direct callee, its
effective `this` remains unknown: the raw call operand alone
does not establish sloppy-call substitution, boxing, or lexical semantics.
The exact wrapper uses its separate explicit fallback parameter, so this
restriction does not change the measured UMD branch proof.

When explicitly contracted, the script entry receives an opaque realm token.
Passing that value as the wrapper's ordinary fallback parameter preserves the
token and its object truthiness. This does not characterize the wrapper's
effective `this`, infer a native receiver type, or classify the realm as a
fresh object. The token has no known property values. Prefix interpretation
still stops before a realm property operation; the separate continuation
check may exclude callbacks only for the finite own-data slots described below.

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

## Explicit classic-script receiver and publication slots

The fallback driver additionally declares:

```json
{
  "entry_receiver": {
    "kind": "classic-script-realm",
    "own_data_properties": ["bootstrap"]
  }
}
```

The embedding promises to invoke the declared entry as a classic script with
its stable realm receiver. Each listed property must initially exist as a
writable own data slot: its ordinary read or write cannot invoke JavaScript.
The slot's initial value remains unknown to the analysis. The Node harness
creates an own writable `bootstrap` property before execution; the ctbrowser
differential supplies the matching ordinary data slot in its realm's global
view. Host accessors and arbitrary proxy traps do not satisfy this promise.

This entry contract is independent of `realm_global_this`, which describes the
initial writable binding. Source can overwrite `globalThis` with `undefined`
while the entry and an explicit argument still retain the realm identity.
An embedding may instead declare an initially undefined `globalThis` without
declaring `realm_global_this`; the separate entry receiver still applies.
An ES module entry or an ordinary function called with `undefined` must omit
this contract. CTJS currently has no validated import-kind metadata from which
to infer it; the exact driver explicitly compiles a classic script. The only
accepted entry kind is `classic-script-realm`, and omission adds no knowledge.

Malformed kinds, duplicate/nonordinary slot names, unsupported fields, and
writable slots conflicting with fixed absent/undefined/intrinsic bindings are
rejected. The typed API repeats slot validation. Possible source descriptor,
prototype or deletion changes refuse this narrow contract, including changes
in an unconsumed suffix. Existing alias, dynamic-property, reflection, callee
identity and unknown-effect guards still apply. A different property on the
realm is opaque. Reports cannot supply the receiver or a slot promise, and
stale fingerprints and exhausted work withhold all candidate rewrites.

The continuation check uses a slot promise only to exclude a callback while
the specialized wrapper is active. It does not evaluate its value, advance
past the factory, fold publication, or establish the complete host-property
proof. Native ownership and callable-publication analysis remain separate.

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
19 Data observations. The fallback adds five observations for stable realm
identity, a distinct untouched `self`, publication and the undefined alias.
The measured compiler results are:

| Mode | Proved branch choices | Resolved factory calls | Native claimed | Refused | Pruned |
|---|---:|---:|---:|---:|---:|
| CommonJS | 2 | 1 | 0 | 7 | 0 |
| Browser | 5 | 1 | 0 | 7 | 0 |
| Browser with undefined globalThis and distinct self | 6 | 1 | 0 | 7 | 0 |

All three selected wrappers contain no remaining UMD branch and one direct call to
the actual `fn$3` factory, retaining its original callee operand and receiver.
The analysis stops at that call with the diagnostic `resolved call body remains
a runtime effect boundary`. The result advances wrapper control and target
proof, while native admission remains 0/7. Explicit script receivers, boxed
public parameters, the open returned method table and captured closure
identities remain admission frontiers. The complete host-property proof is
still withheld; `full_host_contract_claimed` and `native_execution_claimed`
remain false in the evidence.

The fallback source sets `globalThis = undefined`, captures the script receiver
and creates a distinct `self`. Its extra branch is the exact vendor's `t ||
self` fallback: the explicit argument retains the contracted realm identity.
Node v26.8.1 matches all 24 observations. A separate executed mutation setting
`traceRealmDistinct = 0` is rejected with that exact observation diagnostic.
The fallback program SHA-256 is
`a8dd4151142665d7aebd72bb7b099c4956ab9def0a8d304b134e057151293a00`;
the embedded vendor fragment retains the hash above. Reproduce the Node check
and its negative control independently of the compiler:

```sh
python3 tools/check/bootstrap-host-prefix.py \
  --bootstrap ctbrowser/vendor/bootstrap/bootstrap.bundle.js \
  --mode browser_this_fallback --node node --oracle-only \
  --work /tmp/bootstrap-realm-prefix-node
```

The differential driver installs only the generated boxed wrapper. Factory
and Data methods retain interpreter dispatch, so this execution check measures
the prefix transformation rather than a native Bootstrap executable. It
checks the exact numeric observations and the expected compiled invocation
count under ordinary execution and GC stress. Separate global-publication and
implicit-callee-publication fixtures retain both invocations and require the
second invocation to observe the changed host value.
All five `ctcompile_bootstrap_host_prefix_*` differential tests pass:
CommonJS/browser modes match all 19 observations, the realm fallback matches
all 24 observations in ordinary and GC-stress execution, and both reentry
controls preserve the final observation `trace = 2` after two compiled
invocations. The live `HostContractAnalysis::property` unit test also passes.

The focused lit suite covers four useful source consumers and 16 refusals,
including mutable source slots, unknown and throwing effects, terminating
control arms, effective receivers, repeated callers, accessors, late host
initialization and opaque callbacks. It also checks the guarded standard Map
factory, source intrinsic replacement and aliases, caller reflection, forged
reports and declarations, stale manifests and exhausted work. Realm regressions
cover overwritten and initially undefined aliases, omission of either receiver
or slot knowledge, unknown slot contents, a different publication slot, module
and undefined receiver declarations, ordinary-call receiver opacity, descriptor
and prototype mutation, and repeated typed-API validation.
AMD's retained and delayed callable remains
outside this first specialization path. Its invocation identity, full provider
effects and owning publication-table flow need further proofs.
