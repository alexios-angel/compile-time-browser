# Next Bootstrap native boundary

The [diagnostic and callback increment](native-provider-diagnostics.md) and
[ordinary object payloads](native-provider-objects.md) are implemented behind
explicit host-prefix options. The object increment completes the exact Data
method sequence, including the conflict recorder and object reinsertion. All
runtime effects and the seven-function source denominator remain. This is
prefix discovery; native export admission is still unfinished.

CommonJS and browser entry following reach the end. The realm-fallback probe
completes every Data call and stops at the appended `scriptThis === this`
observer, whose realm comparison is outside the ordinary-object proof. The
remaining observer reads also need own-property/presence evidence before
following them; runtime differential execution still checks all 24 observations.

## Completed provider slice: ordinary object payloads

`set(element, "bs.collapse", instance)` now carries the actual initialized
entry-object identity through private Map storage. The subsequent getters
return that identity and observe its current scalar own fields. The transaction
preserves aliases, distinguishes equal-field objects and discards tentative Map,
object and scalar-global effects together on failure.

This slice requires scalar own contents and a source allocation in the entry
invocation. It refuses missing own-field reads, unknown effects, dynamic fields,
accessors, prototype changes, publication and object/Map cycles. Field writes,
deletion and reinsertion remain runtime. Provider-local object allocations and
general heap graphs still require further work.

## Completed prerequisite: one ordinary global owner

Prefix discovery describes one executed startup path. Bootstrap publishes
callable objects for future callers, so native admission still needs live
callee/type proofs and ownership across the export boundary. The existing
confined method-table-field proof cannot be applied to an arbitrary realm or
global owner. Keep the seven-function denominator and named native refusals
while connecting proved value flow to those consumers.

The [ordinary global owner](native-owned-globals.md) now admits
`var host = {}; host.slot = 42; var trace = host.slot;` at **1/1 native** with
explicit `host-manifest` input. Its bounded live proof carries the allocation
through nominal type inference, final admission and shared owning storage.
`StoredGlobal` and external alias semantics remain; only driver-selected numeric
observations are printed. Eight programs pass Node/interpreter and GCC/Clang in
explicit/deduced forms, including owning lifetime sanitizers. Missing/conditional
initialization, replacement, mutation, stale contracts and incomplete budgets
withhold the owner plan. Without the option, the baseline remains **0/1**.

## Completed: owning method fields and current callees

The live host and [owning source graph](native-owned-global-methods.md) queries
now feed closed value flow, returned-table preparation, owning global fields
and final call-component admission. The constant-getter specimen advances
**1/3 -> 3/3 native** with an explicit manifest; the default stays **1/3**.
Preparation validates the original fingerprint before rewriting a clone and
requires a complete new proof before using that clone. It reconstructs native
facts, preserving real stores despite stale annotations. Standalone GCC/Clang
and post-entry owner/table/callable lifetime checks pass.

## Completed: captured Map environments across publication

The [Map-backed publication specimen](native-owned-global-maps.md) advances
**0/4 -> 4/4 native** with an explicit host manifest and standard Map identity.
Its default remains **0/4**. The complete live callable/source-owner proof
follows the immutable capture, and existing Map/capture/table carriers preserve
allocation identity and shared ownership. Six variants pass Node/interpreter,
GCC/Clang and post-entry Map/table/callable lifetime checks. Completed provider
summaries still supply no authority for future callers, mutable slots or a typed
export ABI; see [the export design](native-export-boundary.md).

The exact specimen has four functions and publishes inside its wrapper.
The importer leaves `factory()` indirect. The host query now proves that actual
callback through the entry's sole wrapper invocation before any rewriting.
Native preparation validates the original fingerprint, works on a disposable
clone and requires complete live proofs after callback/capture preparation and
Map annotation. `prepareNativeMaps()` consumes only proved ordinary-root reads
while checking standard Map identity. Mutable captures, replaced Map bindings,
separate publication, additional factory invocations, reentry, throws, cycles
and incomplete proofs remain refusals.

## Completed proof: Map effects in one published method

The complete live callable proof now permits `size`, `set`, `get`, `has` and
`delete` over primitive contents. It checks every actual method receiver,
argument, capture read and Map alias, including `set`'s return. All effects
execute at runtime; a prior invocation cannot supply a later result. Native
Map type/carrier checks remain independent of this ownership proof.
The mutating publication specimen advances **0/4 -> 4/4 native**, with
Node/interpreter `trace=1`. Fourteen complete native programs and post-entry
mutation/lifetime checks pass; see [the measured gate](native-owned-global-maps.md).

## Completed: one captured Map shared by published methods

With the same explicit manifest and Map identity, the fixed setter/getter
specimen advances **0/5 -> 5/5 native**, retaining Node/interpreter `trace=1`.
A three-method variant admits **6/6**. The complete
live capture census now checks every sibling, its fixed field, primitive body
and current calls. The owner query retains the exact source function chain and
requires identical Map identity/family evidence across all calls. Preparation
checks every plan before lifting and unboxes the shared cell after all members.
The existing returned-table and Map carriers preserve ordinary owning calls.

Nineteen complete programs pass Node/interpreter and explicit/deduced GCC/Clang
execution. The shared lifetime gate retains setter/getter after root/table
release, mutates and reads through them for 1024 calls, distinguishes a fresh
entry's Map, and observes destruction after the last callable releases it.
Both forms pass ASan/UBSan and leak checks. Uncalled/unsafe siblings, replaced
fields, capture-stage mixtures, stale proofs and incomplete budgets refuse.

## Completed: primitive arguments to each published method

The retained `set(key) { state.set(key, 1); return state.size; }` specimen,
called with `"x"`, advances **0/5 -> 5/5 native** with Node/interpreter `trace=1`.
`HostContract/Values.cpp` discovers every current method call before checking
the parameterized family bodies, independently classifies each actual and
retains its live SSA operand. Per-call formal/actual evidence and per-method
primitive tags seed the body proof without recursive property-call authority.
Prepared setter arguments are `(this, new.target, callee, MapEnv, key)`;
the getter remains zero-argument and both capture one Map owner. Existing
capture lifting and typed owning callables need no replacement carrier.

The [Map gate](native-owned-global-maps.md) now passes **25 complete native
programs**, including six new string/number/boolean, repeated, alias and
two-parameter variants. The fifth sanitizer lifetime case keeps typed string
setters/getters after root/table release, mutates caller buffers and exercises
1024 changing keys against independent saved/fresh Maps. Ten argument refusals
preserve all actual operands; source/prepared proof units check both formal
positions, global initialization, environment offsets and every incomplete
budget. These current-call proofs do not establish a future-call ABI contract.

## Completed: independent call-result actuals

`host.slot.set(host.slot.get())` is the retained `parameter_call_result`
specimen, now **5/5 native** with Node/interpreter `trace=1`. A bounded
dependency worklist waits for a completed producer body/effect proof before
using its definite primitive result tag for the consuming formal. It handles
consumer-before-producer declaration order without optimistic recursive
authority. All calls, publication and mutations remain runtime operations.
The gate now passes **34 complete programs**, including nine result variants,
and all five existing lifetime variants; see [the Map gate](native-owned-global-maps.md).

## Completed: locally seeded Map.get results

The retained `result_seeded_map_get` uses
`get() { state.set(0, 1); return state.get(0); }` with the same `set(get())`
entry. Commit `b2466a0` advances it **0/5 -> 5/5 native**, retaining
Node/interpreter `trace=1` and every runtime call. A bounded local last-write
fact supplies an independent result tag only after the complete method proof;
native Map preparation separately proves instance/key presence and the schema.
The seeded lookup used as a later key in one method also advances **0/4 -> 4/4**.
The gate passes forty complete programs and six sanitizer lifetime variants.

## Completed: separate live Map entries

`seeded_earlier_key` inserts `state.set(1, 2)` before `return state.get(0)`.
It now advances **0/5 -> 5/5 native**, retaining Node/interpreter `trace=1`.
Bounded per-key contents and SameValueZero key comparison preserve the earlier
payload across independently disjoint writes/deletes. Seven new programs pass;
the complete [published Map gate](native-owned-global-maps.md) passes 47 programs
and the existing six lifetime variants. No lookup or call is evaluated away.

## Next: payload types across possibly aliasing writes

`seeded_dynamic_write` uses `state.set(state.size, 2)` instead. It stays
**0/5 native** with no owner proof and every source call intact. Its runtime key
may alias the seed, so the current proof discards the old payload tag. A complete
type join across possible overwrites could retain an independently proved
numeric payload; key presence and possibly aliasing deletes remain separate
obligations. Prior invocation observations cannot prove current contents.
Unseeded gets remain refused. String/boolean/mixed Map payloads remain separate carrier
boundaries at **0/6** despite completed host proofs. Exact Bootstrap Data stays
**0/7** in CommonJS/browser and **0/8** in AMD (the extra function registers the
delayed factory); complete native initialization, realm owners and future-call
contracts remain unfinished. The full devbox gate passes **475/475 CTests**;
see [the current handoff](HANDOFF.md) for corpus counts and evidence.

Exceptions do not make a callback or allocation inert. Native try/catch covers
one acyclic handler with homogeneous number, boolean or owning string throws
and proved nonthrowing primitive helpers in its try and catch. Checked callee
resolution now follows preserved status/register vectors. Target verification
also follows homogeneous primitive payloads through defined EmitC helpers.
Explicit `ctjs.invoke` regions now separate the normal result from an implicit
thrown payload and pre-call state; bounded type flow now covers both payloads
and the invoked helper's normal returns. Ordinary call inference remains
conservative on throw exits. An internal `CheckedInvocations` recovery mode now
constructs those regions and connects the two completions to the enclosing try
through a value-only tuple, retaining pre-call state on unwind. The default
mode and all native throwing-call refusals remain. An additional
`EffectCheckedInvocations` mode validates other status and continuation effects
before adopting the recovered clone. Commit `e4b2d5f` proves source global
callee lookups from the complete live declaration/store/use and effect census,
with separate bounded primitive completion facts. The three direct-call source
specimens now recover in this internal mode, preserving assignment and argument
state. The focused gate passes all 3521 source-binding budget cutoffs and 937
effect cutoffs, nineteen live binding mutations and uncalled-throw controls.
Native admission still needs the complete call component; target emission must
consume the accepted invokes. Native throwing callees, general
finally, reentry and object payloads require
further work. The [source invocation gate](native-source-invocations.md) now
retains four source programs, sixteen functions and eleven observations for
assignment snapshots, prior normal calls, argument mutation and receiver/key/
getter/argument order. All source throwing calls still refuse native lowering;
the document identifies the remaining admission and emission obligations.
Normal-return provider facts cannot authorize an exceptional continuation.

Retain the eleven scenarios in
[the exact host audit](../../tools/check/bootstrap-host-contract-audit.py),
including callee-order observation `1234` and throwing/reentrant sinks. Preserve
vendor/program hashes, runtime methods and observer branches. Plan parts 24 and
25 remain the native type and ownership requirements.
