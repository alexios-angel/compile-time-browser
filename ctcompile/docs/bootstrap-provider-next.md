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

## Completed: payload types across possibly aliasing writes

Commit `0054611` retains a definite payload tag when every possible overwrite
has that same independently proved tag. `seeded_dynamic_write` advances
**0/5 -> 5/5 native** with `trace=1`. Exact-key writes replace their tag;
possible incompatible writes lose it. Presence and type evidence remain
independent, and no call is evaluated away.

## Completed: nonempty Map.size snapshots

Commit `e8d5cdb` independently proves that a size read from a Map with a
definite entry is at least one. A delete using that saved number cannot erase
key zero. `seeded_dynamic_delete` advances **0/5 -> 5/5 native**, preserving
Node/interpreter `trace=1`. Host result analysis and native presence each derive
their own evidence from live IR; later mutation cannot change the saved number.
The gate passes **58 complete programs** and all six existing lifetime variants
under GCC/Clang and Node/interpreter comparisons. Initial zero sizes, equal
positive keys and equal snapshots remain explicit refusal controls. See the
[Map checkpoint](native-owned-global-maps.md#nonempty-size-snapshots-2026-09-08).

## Completed: distinct-key size bounds

Commit `c900f84` advances `seeded_size_two_entries` from **0/5 -> 5/5 native**,
with Node/interpreter **`trace=2`**. The getter seeds keys 0 and 1, deletes
`state.size`, then returns `state.get(1)`. Both analyses derive a lower bound
from a pairwise-distinct subset of definite keys; different SSA keys alone
never count twice. Each size read examines at most 64 candidates and retains
its immutable bound across later mutation. The native gate passes **63 complete
programs**, eight size programs and ten size-key refusals, plus six existing
sanitizer lifetime variants. Source/prepared host proof budgets are **2278/2372**;
the independent presence lit passes seven positives and fourteen refusals.
See [the Map checkpoint](native-owned-global-maps.md#distinct-key-size-bounds-2026-09-08).

## Completed: homogeneous boolean and owning-string Map payloads

The existing `result_seeded_bool` and `result_seeded_string` specimens now
admit **6/6 native**, preserving Node/interpreter **`trace=2`**. Their existing
host proofs now feed supported native storage, construction and reads.
The published gate passes **69 complete programs** with all seven lifetime
variants, including saved strings after overwrite, deletion and Map destruction.
Ordinary missing reads keep false/empty-string distinctions through existing
nullable carriers; string value snapshots own their elements. Boolean snapshots
remain refused.

## Completed: closed mixed key/payload storage and exact read types

`result_seeded_mixed_contents`, `result_seeded_join_reseed` and
`result_seeded_bool_string_contents` now admit **6/6 native**, with
Node/interpreter traces **2/3/2** and all **9/10/9** calls retained. Exact
Bool/Number or Bool/String schemas use finite `std::variant` alternatives in
the existing owning storage. Separate literal-write evidence proves each read's
presence and scalar result without narrowing the full Map schema. Key comparison
preserves SameValueZero and false versus zero. The published gate passes **76
programs** and all **eight lifetime variants**; the local mixed gate tests both
associative and ordered storage. See [the Map checkpoint](native-owned-global-maps.md#closed-mixed-map-storage-2026-09-08).

## Completed: saved scalar Map reads through writes

Commit `d9a4b04` advances `saved_read_write` from **0/6 to 6/6 native** in both
modes, retaining Node/interpreter **`trace=1`** and all **twelve calls**. Saved
Boolean, Number and String tags survive known source-entry mutations separately
from current contents. A later write consumes that independent scalar fact;
missing reads, possible aliases and differing branch facts remain conservative.
The gate passes **83 complete programs**, including seven saved-chain programs,
three missing/deleted refusals and all nine lifetime sanitizer variants.
The local mixed gate passes 21 observations and ten refusals across both
storage implementations. See [the Map checkpoint](native-owned-global-maps.md#saved-scalar-reads-through-writes-2026-09-08).

## Completed: a saved scalar selected across control flow

Commit `53b44b9` advances `saved_join` from **0/6 to 6/6 native** in both modes,
with Node/interpreter **`trace=3`** and all **sixteen calls preserved**. The live
host proof checks both structured conditional arms and intersects their mutable
contents. A selected scalar gets a type only when both yielded values prove
the same tag. Constant predicates cannot discard an arm. The gate passes
**89 complete programs**, six new conditional programs, four new refusals and
all **ten lifetime sanitizer variants**. Host units cover 25 conditional rows
each in source/prepared form and all 2501/2626 incomplete budgets. The local
mixed gate passes 27 observations and twelve refusals. See
[the Map checkpoint](native-owned-global-maps.md#saved-scalar-values-across-conditionals-2026-09-08).

## Completed: a guarded saved read after conditional deletion

Commit `677714b` admits **6/6 native** in both modes for the saved-value ternary
`state.has('other') ? state.get('other') : state.get('')` after conditional
deletion. All eighteen calls and Node/interpreter `trace=2` remain. Host/native
proofs keep membership separate from the payload tag valid whenever present;
joins intersect both independently. Live same-key guards can restore membership,
while stale observations and unknown/conflicting payloads still refuse.

The gate passes 95 published programs, eleven lifetime sanitizer variants,
35 local mixed observations, seventeen local refusals and 44 host rows each in
source/prepared form. All guarded budgets, forged edits, both runtime flags and
saved owning Strings after final Map release pass. See
[the Map checkpoint](native-owned-global-maps.md#guarded-saved-reads-after-conditional-deletion-2026-09-08).

## Completed: scalar short-circuit result refinement

Commit `58decfe` admits **6/6 native** in both modes for
`(state.has('other') && state.get('other')) || state.get('')`, preserving all
**eighteen calls**, three getter conditionals and Node/interpreter **`trace=2`**.
The live proofs retain finite primitive alternatives by truthiness and refine
only the tested SSA value in each arm. Every arm's effects remain checked.
A local Bool/String temporary owns its String; independently rederived exact
write tags bridge wider SCF inference without changing the Map storage schema.

The gate passes **103 complete programs**, **twelve lifetime sanitizer
families**, **49 local observations and 21 refusals**, and **82 host rows** each
in source/prepared form. Nine new published refusals, all short-circuit budgets,
forged edits and reruns pass. Empty String, false and zero still select the
fallback, and saved future Strings survive final Map release. See
[the Map checkpoint](native-owned-global-maps.md#scalar-short-circuit-results-2026-09-08).

## Completed proof: a finite nullable result contract

Commit `fa29d49` preserves String/Null/Undefined alternatives through the complete
host body dependency worklist and actual/formal census. Startup truthiness is
widened within every category; unseeded cycles and unknown producers still
refuse. The **82 earlier plus 32 nullable host rows** pass in source/prepared
form, with exhaustive budget cutoffs and live mutation controls.

The normalized nullable setter `state.set(key || 'missing', true)` now admits
**6/6 native** in both modes, retaining all **eighteen source calls** and
Node/interpreter **trace=3**. Stored callable signatures use owning
`nullable_string`. A current per-operation key proof keeps the storage String
without narrowing the nullable SSA value elsewhere. Commit `8d80629` passes **110 complete published programs**,
including seven nullable cases and thirteen lifetime sanitizer families. Both
C++ compilers, identity observations, forgeries, reruns and budgets pass; all
**165 lit cases** pass. Final CTest is **512/517**, all **372 compiler tests**,
with only the five recorded browser failures. See
[the nullable checkpoint](native-owned-global-maps.md#finite-nullable-published-results-2026-09-08).

## Completed: owning nullable String Map keys

Commit `5e63d993` admits the original **eighteen-call** nullable-key program
at **6/6 native** in both modes with Node/interpreter **trace=3**. The smaller
**eleven-call** `Opt<Str>` case also admits **6/6**, trace=2; the **thirteen-call**
String/Null/Undefined/empty String identity witness gives trace=4, versus trace=2
for its normalized control. Both layouts use owning tag-aware keys, including
Boolean composition as `std::variant<bool, ctnative::nullable_string>`.
The per-use key proof remains separate from storage; nullable payloads and
unsupported snapshots still refuse. Local native/VM/compiler and sanitizer
tests pass, as do all 118 published positives and fourteen lifetime families;
final whole-suite results are recorded in [HANDOFF.md](HANDOFF.md).

## Completed: owning nullable Map payload storage

Commit `6e150949` admits the eleven-call nullable write and twelve-call
readback at **6/6 native** in both modes, preserving Node/interpreter trace=2.
Owning `nullable_string` storage preserves Null, Undefined and empty String;
closed Boolean composition reuses `std::variant<bool, nullable_string>`.
Mixed reads still require independent payload facts. The original eighteen-call
mixed write admits **6/6**, trace=3. A deleted read correctly returns Undefined;
saved results survive later writes, deletion and final Map destruction.

The first driver passed all 123 positives and fifteen lifetime families.
After promoting the deleted-read case, all six new payload programs and the
remaining refusal controls pass focused checks. Seven targeted lit cases,
four host/owner CTests and the 63-observation/34-refusal local gate pass.
The full 124-program driver and all 165 lit cases pass. Final CTest is
512/517, all 372 compiler tests, with only the five recorded browser failures;
see [the payload checkpoint](native-owned-global-maps.md#owning-nullable-payloads-2026-09-08).

## Completed: finite nullable read facts in mixed storage

Commit **`c4d6bf07`** advances the exact fourteen/nineteen-call readbacks to
**6/6 native** in both modes, retaining trace=2/3. Per-instance/key payload
alternatives seed `Opt<Str>` independently of the Boolean/nullable storage
schema. All four new published programs pass explicit/deduced GCC/Clang,
identity and saved-payload lifetime checks. Three complete-owner refusals and
their exact admitted repairs, fresh/stale forgeries, four budget families and
the 69-observation/39-refusal local gate pass. The complete 128-program driver,
sixteen lifetime families and all 165 lit cases pass. Final CTest is 512/517,
all 372 compiler tests, with only the five recorded browser failures; native
corpus and exact Bootstrap Data counts remain unchanged.

## Completed: finite nullable host-result alternatives

Commit **`ed1a833c`** advances the exact fifteen-call `get -> set -> size(key)`
source **0/6 -> 6/6 native** in both modes, preserving trace=1. The independent
host body proof keeps finite payload alternatives through live writes, joins
and reads, then the complete actual/formal census supplies the next method's
nullable input. Saved read evidence survives later mutation. No native schema
or Presence annotation supplies host proof authority.

Five programs pass both modes, explicit/deduced GCC/Clang, exact identities and
saved-result lifetimes in **`fe867e6f`**. Four independent host refusals and their
exact repairs pass, together with fresh/stale reports, reruns and budget
cutoffs. The full 253-step build passes. Initial CTest is 511/517; after the
test-only `077328ae` ownership/carrier correction, the complete lit rerun passes
165/165, including all 133 programs and seventeen lifetime families. All 372
compiler tests pass across both runs; five established browser failures remain.
[HANDOFF.md](HANDOFF.md) records the separate measurements and logs.

## Completed: same-method invocation result dependencies

Commit **`d7148fcf`** advances the exact fifteen-call `set(set(get(false)))`
source to complete ownership and **6/6 native** in both modes, trace=2.
Independent invocation body proofs supply only source-ordered result facts;
a second mandatory all-call/all-sibling census authorizes the published family.
Unknown results, forward edges, cycles and unsafe later actuals still refuse.
Four source programs, nested saved-result lifetime, five new refusal/repair
families and three budget sweeps pass in **`a1b11e80`**. The combined focused
gate is **12/12 CTests in 61.47 seconds**. The full 137-program/eighteen-lifetime
driver and all 165 lit cases pass. The warning-free 243-step build completes
with CTest **512/517**, all 372 compiler tests and 140/145 browser tests, leaving
only the five established browser failures; [HANDOFF.md](HANDOFF.md) records
the measurements.

## Completed: published method-local leaf object ownership

Commit **`e9f8e33c`** advances the exact seven-call `{}` and `{value: 1}`
programs **0/5 -> 5/5 native** in both modes, retaining trace=2. The body proof
checks every future local leaf allocation and fixed scalar write; exact source
operations reach the host and owner censuses. An object-writing sibling removes
unknown Map reads' primitive guarantee before any invocation result is proved.
The existing native object identity pass now runs in host Map preparation too,
followed by the final live owner proof. No carrier or emitter code changed.
The old four-function empty-object refusal advances to 4/4, trace=1.

All four host/owner CTests pass. Commit **`2efcbbe2`** passes eight new and one
historical program in both modes, explicit/deduced GCC/Clang, independent
identity/field observers, saved-callable sanitizer lifetime, thirteen
refusal/repair families and three budget sweeps. The full generated CTest gate
is running with 146 programs and nineteen lifetime families; see
[HANDOFF.md](HANDOFF.md) for measured results and preserved test failures.

## Next: method-local object readback and identity

The eight-call saved-object source stores `{value: 1}`, saves a Map.get,
overwrites/deletes the entry and compares exact identities. It gives
Node/interpreter trace=1 and remains unowned **0/5 native** in both modes.
The distinct equal-field eight-call control and fresh-read-after-delete nine-call
control give trace=0 and retain the same refusal. Prove the local object origin,
presence and identity without treating a public object result as already owned.
The next own-field read also needs explicit body proof. Complete sources are in
[the Map boundary](native-owned-global-maps.md#next-boundary).

String fields and Object/String Map payload carriers remain separate. The
historical nested leaf-writing sibling now has complete ownership but stays
0/6 at that mixed carrier; the original eleven-call `{value: 'instance'}` source
remains unowned0/6, trace=2. Provider entry tokens cannot authorize future local
objects, and ordinary ownership supplies no throwing-call or general export ABI.

The exact Bootstrap getter at vendor line 17 also needs nested/object payloads.
Exact Bootstrap Data, general realm owners and future-call contracts remain
unfinished; the measured corpus counts and full gate are in [HANDOFF.md](HANDOFF.md).

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
