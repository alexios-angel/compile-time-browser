# Next Bootstrap native boundary

## Current continuation: definite scalar-global initialization, 2026-09-09

`801794d8` closes the saved Number Map-identity boundary. The unchanged
**d74ae2ee** and **867378b1** eight-call sources now reach **5/5 native** in
both modes, trace=3/12. Both execute under explicit/deduced GCC/Clang with exact
requested scalar observations and no Script/VM/AOT symbols. All five host/owner
CTests pass. `1bd5b5ac` also passes fourteen focused native programs, eight
refusal/repair families and the new 128-call sanitizer lifetime; the final
173-case census preserves all 169 historical refusal classifications. The full
gate is running on eighteen frozen code/test files. No full-Bootstrap gain is
claimed; see [HANDOFF.md](HANDOFF.md).

The next exact source passes the complete host, owner and Map identity proofs:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {}; state.set(key, item); const saved = state.get(key); return state.size; }
    };
});
host.slot.size(); const first = host.slot.set('x'); const second = host.slot.set('y'); host.slot.set('z'); const alias = first; var trace = alias * 10 + second;
```

Source SHA-256, including its final newline:
`8003b4bc3a35bc936752067dc66c97ab02b36294db685d776d10a53f1a42b388`.
Node/interpreter agree: `alias=1`, `first=1`, `second=2`, **trace=12**. The
source retains five functions, eight calls and its alias store/read, but both
modes remain **0/5 native**, diagnosed as
`store to global alias may be null or undefined; native global observations require a definite number`.
The exact repair changes only `const alias = first;` to
`const alias = first + 0;`; every call, alias store/read, final multiplication
and addition remains. It reaches **5/5**, preserving all observations.

`Analysis/TypeInference.cpp` currently starts each closed-world global-load
join with `absentType`. It cannot yet consume the fresh `OwnedGlobalRoots`
per-load initialization edge. Extend that existing join only when the actual
load, single indexed store and saved value agree with the live edge. Drop the
implicit Undefined seed for that load, then subscribe to and join the real
store operand's lattice. An uninitialized producer must remain pending and
revisit when its type arrives or widens. A boxed producer remains boxed even
when HostContract separately knows a Number category. Neither the category,
an observation name nor a report may manufacture a native NumType.

Keep dynamic globals, multiple writes, read-before-initialization and
stale/exhausted proofs conservative. Do not relax `admission::printable()` or
replace tagged global storage as a shortcut. The raw indirect-call case, whose
host Number evidence coexists with an unproved native type, is a required
independent negative control. The seven-call direct `trace = first` source
hits the same final observation boundary; its `first + 0` repair admits.

All **18** new probes agree on Node/VM and both classifications: ten native,
four unowned, four complete-owner refusals. All original sources remain intact.
Exact zero-size after clear, String leaf fields, full Bootstrap and direct
browser APIs remain separate unfinished boundaries.

## Previous continuation: saved scalar globals, 2026-09-09

`0468fed4` proves entry Number arithmetic and `83c32f0c` gates it. The original
379ccc eight-call sum now reaches **5/5 native**, trace=3, in both modes.
All eighteen focused programs pass Node/interpreter, explicit/deduced GCC/Clang,
no-VM checks and two sanitizer lifetime families. Nine unowned and two
owner-complete refusal families retain exact repairs and live operands. All
169 historical refusal cases were audited without another classification change.
The warning-free full 250-step build passes **512/517 CTests**, including all
**372 compiler tests**; only the five established browser failures remain.
Lit passes **165/165 in 874.79 seconds**, including **233 published Map
programs and 32 sanitizer lifetime families**. Full Bootstrap coverage remains
**19/574** in both modes; see [HANDOFF.md](HANDOFF.md) for measured gates.

The next exact boundary is the unchanged saved-results source:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {}; state.set(key, item); const saved = state.get(key); return saved === item ? 1 : 0; }
    };
});
host.slot.size(); const first = host.slot.set('x'); const second = host.slot.set('x'); const third = host.slot.set('y'); var trace = first + second + third;
```

Logical source SHA-256, including its final newline:
`d74ae2ee15979027a09d9ab8a786f3d2f2640617acf9979ef00ce32a226f2065`.
It has **five functions, eight calls, three result stores/loads and two
additions**, trace=3. Both modes now prove complete host ownership, but native
admission remains **0/5** with
`standard Map identity is unproved with other host/global value reads`.
Replacing only the final saved declarations with the original inline expression
keeps all calls and arithmetic and restores the admitted 379ccc source.

`Analysis/NativeMap.cpp` currently exempts only global loads identified by
`OwnedGlobalRoots::lookup()` as the owned ordinary root. The complete host
proof knows the saved scalar categories, but that evidence is not exposed to
this consumer. Extend this existing seam with a bounded live proof of the
actual scalar definitions and uses. Check source ordering, all writes,
current callable/result origins, unknown effects and stale/fresh reports;
never infer a harmless global from its spelling or observed startup value.
Type inference and final native ownership remain independent obligations.
Start with a per-load Number edge recording its single earlier StoreGlobal,
saved SSA value and completed result dependency. Publish it only after the
complete host family and owner checks succeed, sharing their bounded work and
current fingerprint. Rebuild that proof after source transformations; preserve
the constructor, prototype, reflection and unknown-call guards in NativeMap.

The separate eight-call saved size-snapshot source, SHA-256
`867378b10e4c6d18a1902135efc101813e3daadad13b33091c8af25fb89f9a42`,
returns trace=12 and hits the same boundary. Its exact inline repair retains
every call, multiplication and addition and reaches 5/5. Both saved-global
refusals preserve prepared receiver/callee/capture, store/load and binary edges
under fresh/stale forgeries and reruns. All 31 measured cases agree on Node/VM
and both admission modes: **20 native, nine unowned, two owner-complete**.
The repairs preserve call order and arithmetic dependencies but move pure
Number arithmetic earlier. The snapshot repair also publishes `trace` before
the final setter, which neither reads it nor reenters.

Host-only SCF category proofs also require a valid condition and independently
scoped yield operands; entry SCF still has the existing native owner refusal.
Exact zero-size after clear and String leaf-field support remain separate
boundaries. Full native Bootstrap and direct browser APIs remain unfinished.
Evidence: `/tmp/ctcompile-numeric-census.json`, `-saved.mlir`, `-execution.log`
and `-refusal-census.log`. All twelve previous continuation source hashes and
414 historical helper rows remain unchanged.

## Previous continuation: entry numeric results, 2026-09-08

`1c352a82` and `c2b49f99` complete captured standard clear and arbitrary-key
absence, with both original seven-call sources now **5/5 native**, trace=1.
The 21-program focused execution gate passes Node/interpreter, explicit/deduced
GCC/Clang, five refusal/repair controls and three sanitizer lifetime families.
All twenty focused CTests pass across the initial 19/20 run and a corrected
escape expectation rerun. The warning-free full 240-step build completes with
**511/517 CTests**, including the same five browser failures and one old Map
classification (lit **164/165**). **`7011c79e`** retains the historical nine-call
clear source and checks its exact unsupported nullable Number Map-key carrier;
its nine-call repair admits. All twelve focused carrier families and HostContract
CTest pass.
The complete corrected lit rerun passes **165/165 in 776.42 seconds**, including
**215 published Map programs and 30 lifetime families**. All **372 compiler
tests have passing results across the full run and corrected rerun**.

The next exact boundary is **entry arithmetic over published method results**:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {}; state.set(key, item); const saved = state.get(key); return saved === item ? 1 : 0; }
    };
});
host.slot.size(); var trace = host.slot.set('x') + host.slot.set('x') + host.slot.set('y');
```

Logical source SHA-256 (including its final newline):
`379ccc667b2d463c5fbdc531c53a90ec01c7ba7d8ab578ef1493c3e62f4e281e`.
This unchanged source has **five functions, eight raw/prepared calls, two binary
operations and trace=3**, but stays unowned **0/5** in both modes, diagnosed as
`` unsupported provider behavior through `ctjs.binary` ``. Replacing only its
final declaration with
`host.slot.set('x'); host.slot.set('x'); var trace = host.slot.set('y');`
retains **all eight calls**, reaches **5/5** and returns trace=1. The older
one-call repair also stays admitted, with six total calls.

`HostContract/Analysis.cpp` rejects binary operations in the live environment.
`HostContract/Values.cpp` already proves invocation-result dependencies before
the complete method census. Extend those live proofs only with independently
proved operand/result categories; source order, complete future-call argument
joins and effect checks remain mandatory. Reports or observed startup numbers
cannot supply type authority. Keep the evaluated operands and every call.
A later method key computed from earlier results needs the same bounded
result-dependency proof, without allowing a circular method summary.

Concretely, `capturedMapParameters()` currently accepts a primitive constant or
an exact key in the invocation worklist's local `completedResults`; it cannot
trace a binary expression. Publish no provisional result category until the
complete family recheck succeeds. Keep this category proof separate from
`primitive()`/`truth()`, whose attributes denote actual values and select UMD
branches: an unknown Number result must never become a fabricated Number
constant. Saved global aliases also need their current source order checked.

All **twelve probes** agree across Node/interpreter and both modes: four admitted
5/5 and eight unowned 0/5. Saved results, Number-plus-literal and result-fed keys
remain refused, with original binary and call edges intact. Number/String
concatenation and object coercion controls also refuse. The zero-size witness
after clear is separate: existing lower bounds do not prove that snapshot is
exactly zero. Its eight-call literal-zero repair retains the actual size read
and reaches 5/5. The original seven-call String leaf-field source remains 0/5;
its numeric repair is 5/5. Full Bootstrap and browser API integration are open.

Evidence: `/tmp/ctcompile-map-clear-next.{py,json,log}` and
[HANDOFF.md](HANDOFF.md). Additional numeric globals in the saved-result probe
are included in the interpreter comparison; all original JavaScript is intact.

## Previous continuation: captured Map.clear, 2026-09-08

`f05c3e0b` closes exact-key object-valued Map absence; `3825f3cf` gates
21 programs, ten exact refusal/repair controls and two sanitizer lifetime
families. The original seven-call fresh Undefined source reaches **5/5 native**,
trace=1; historical seven/nine-call post-delete identity sources reach **5/5**,
trace=0. The source hashes below are unchanged. Both affected CTests pass after
preserving when-present payloads through deletion. The full warning-free
250-step build completes at **511/517 CTests**: the same five browser failures
and one old Map refusal classification. Initial lit passes **164/165**.
`4eb2390d` preserves all historical sources while asserting eleven refined type
refusals and gating the original ten-call empty-String deletion program at
**6/6 native**, including future Undefined/empty/distinct-key observations under
explicit/deduced GCC/Clang. The complete corrected lit rerun passes **165/165 in
709.94 seconds**; the published Map gate passes **194 programs and 27 lifetime
families**. All **372 compiler tests have passing results across the first full
run and corrected rerun**. No second full 517-test run is claimed.

The next isolated boundary is **captured `Map.clear()`**. A saved-object identity
read across clear separates method admission from result typing; a fresh read
also needs whole-Map absence. `HostContract/CapturedMapBody.cpp` currently only
admits size/set/get/has/delete and fixed one/two-argument calls. NativeMap,
NativeObject field analysis and EmitC Maps already understand the standard
zero-argument clear method. Reuse those implementations; do not duplicate Map
behavior. Validate every future invocation, live spelling/receiver/arity and
source effects before admitting clear. Whole-Map absence must account for later
writes, possible key aliases, saved values and surviving branch joins.

All **29 measured continuation sources** agree across Node/the interpreter and
both admission modes: five controls reach 5/5, while 24 remain unowned 0/5 with
prepared calls intact. The 22 clear sources include saved identity/fields,
repeated clearing, unseen keys, aliases, later writes and surviving branches.
The original saved-identity case is **five functions, seven calls, trace=1**:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {value: 1}; state.set(key, item); const saved = state.get(key); state.clear(); return saved === item ? 1 : 0; }
    };
});
host.slot.size(); var trace = host.slot.set('x');
```

Logical source SHA-256, including its final newline:
`5aefbb04e557a199248b20305a10953764ee1c14b977eff5b7ce2c4a55fdb024`.
Replacing only `state.clear();` with `state.delete(key);` preserves seven calls
and trace=1 and reaches **5/5 native**. The fresh Undefined read variant also
has seven calls/trace=1 but needs the separate whole-Map absence proof; its hash
is `a041e8248d43dac780775c97916939a7e9d88034ce153a24d4576ebbc2f25a16`.

The original entry-addition control remains unowned, **eight calls/trace=3**;
the String-field control remains unowned, **seven calls/trace=2**. Their exact
one-call/numeric-field repairs stay admitted. Full native corpus coverage stays
Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725** in both modes, zero pruned;
exact Data remains **0/7 browser/CommonJS, 0/8 AMD**. Direct browser API integration
also remains unfinished. Evidence: `/tmp/ctcompile-absence-recovery-next.json`
and `/tmp/ctcompile-map-absence-next.py`.

## Previous continuation: object-valued Map absence, 2026-09-08

`2593acd7` and `316818b2` close the comparison-only fresh identity boundary from
`bf2fd02e`. The exact local six-call and historical eight-call sources advance
**0/5 -> 5/5 native**, preserving trace=0; both saved-identity repairs remain **5/5**,
trace=1. Four programs agree across Node, the interpreter and standalone
explicit/deduced GCC/Clang, with fresh allocations, field writes and runtime
comparisons intact and no Script/VM symbols. Four lifetime families pass future
calls, reentry, Map/owner release and distinct retained-leaf ASan/UBSan/leak checks.
The focused CTest gate is **15/15**. First full CTest is **511/517** after a
warning-free 245-step build: the same five browser failures plus an existing
specific-diagnostic assertion. `8e603ce5` restores that diagnostic; rebuilt type
and exact diagnostic tests pass. The complete corrected lit rerun passes
**165/165 in 658.71 seconds**; all **372 compiler tests have passing results across
the full run and rerun**. The published Map gate passes **172 programs/25 lifetimes**.
Native Bootstrap stays **19/574**;
exact Data stays **0/7 browser/CommonJS and 0/8 AMD**.

The completed **31-probe** run preserves every source hash and agrees across
Node and the interpreter. Both modes give **13 admitted 5/5 and 18 unowned 0/5**.
The next exact source, `local_absence_delete_undefined`, has **five functions,
seven raw/prepared calls, trace=1**, and remains **0/5 native** with the diagnostic
`property call lacks a current source getter proof`:

```js
var host = {};
(function(factory) { host.slot = factory(); })(function() {
    const state = new Map();
    return {
        size() { return state.size; },
        set(key) { const item = {value: 1}; state.set(key, item); state.delete(key); return state.get(key) === void 0 ? 1 : 0; }
    };
});
host.slot.size(); var trace = host.slot.set('x');
```

Source SHA-256 (including its final newline):
`f3350b8928408e7ca35dfd7a66da79a26d0c917e3d15ff70b4fb4c8901ea4fb5`.
The exact saved-before-delete repair remains **5/5**, seven calls, trace=1.
Unseeded six-call and numeric-payload seven-call absence controls **already reach
5/5**; the new work concerns object-valued Maps. The unchanged local/historical
fresh post-delete object comparisons (**seven/nine calls, trace=0**) remain
unowned **0/5**, preserving every prepared call.

The host method proof in `HostContract/CapturedMapBody.cpp` needs exact-key
**definite absence**. Its current `present=false` also covers maybe absent, so it
cannot prove Undefined. Preserve absence across exact deletes and known disjoint
writes, invalidate it after potentially aliasing writes, and intersect it at
branch joins. Saved reads retain their earlier values independently of later
Map mutations. Repeated deletion, saving Undefined before reseeding, disjoint
overwrites and possible formal-key aliases all remain refused. Exact same-key
reseeding already reaches **5/5**. Keep the original refused sources and their
exact repairs as independent controls.

`clear()` is a separate unsupported host method. Both fresh-read and saved-object
identity variants remain **0/5**, seven calls, trace=1; replacing `clear()` with
exact `delete()` in the saved-object variant reaches **5/5**. Do not attribute
that refusal solely to an absence result.

Four identical-arm sources have **eight raw calls but seven prepared calls**:
LiftToSCF merges their identical blocks. The deleting versions refuse; their
nondestructive repairs reach 5/5. These probes do not validate a surviving
two-arm join; add a nonidentical safe positive witness for that proof. One-arm
deletion (eight calls) and conditional reseeding (nine calls) remain refused
with both Boolean startup values. The corrected temporary runner records raw
and prepared counts separately without changing any JavaScript. Evidence:
`/tmp/ctcompile-comparison-identity-next.json` and `-next-final.log`.

Entry numeric addition, String/object field carriers, exact Data, full native
Bootstrap initialization and direct browser API integration remain open.
No full-bundle coverage gain is claimed. See [HANDOFF.md](HANDOFF.md).

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
refusal/repair families and three budget sweeps. The complete 146-program/
nineteen-lifetime driver and all 165 lit cases pass. The warning-free 244-step
build finishes with CTest 512/517 in 1199.12 seconds: all 372 compiler tests and
140/145 browser tests, leaving the five established browser failures. All
nineteen code/test paths match HEAD, frozen input and final devbox sources;
[HANDOFF.md](HANDOFF.md) records the measurements and preserved test failures.

## Next: method-local object readback and identity

The eight-call saved-object source stores `{value: 1}`, saves a Map.get,
overwrites/deletes the entry and compares exact identities. It gives
Node/interpreter trace=1 and remains unowned **0/5 native** in both modes.
The distinct equal-field eight-call control and fresh-read-after-delete nine-call
control give trace=0 and retain the same refusal. Prove the local object origin,
presence and identity without treating a public object result as already owned.
Final probes isolate a direct fixed own-field read at five calls, trace=1,
and saved same-key identity, Map.get field-read and guarded-read controls at
six calls, trace=1. All remain unowned 0/5 in both modes. The two-stored-object
distinct control is seven calls, trace=0; the comparison-only fresh-object
control is six calls, trace=0 and adds a separate native family limitation.
Saved references must retain their original object after replacement/deletion
and observe later writes through aliases. All sixteen measured sources,
including the unchanged historical 8/8/9-call cases, are in
`/tmp/ctcompile-leaf-object-next.json`. The source and proof obligations are in
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
