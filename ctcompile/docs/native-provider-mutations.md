# Private Map mutation summaries

`follow-provider-mutations=true` implements bounded transactional state for
private Maps in the checked host prefix. It requires both
`follow-provider-reads=true` and `follow-publication=true`. The default pipeline
and existing options retain their previous boundaries. On the three exact
Bootstrap Data probes, mutation following resolves 13 calls and completes
eleven method summaries before console lookup stops discovery. Native
admission remains 0/7 in each mode.

This extends the [checked host prefix](native-host-prefix.md), whose retained
factory/cell identities can describe a private Map before an unknown effect.
It does not replace the independent native type, ownership, closed-call or
host-slot proofs. Global exports do not become closed native objects because
one executed entry prefix was observed.

## Historical baseline and first mutation

Bootstrap 5.3.8's exact Data declaration is in
[bootstrap.bundle.js](../../ctbrowser/vendor/bootstrap/bootstrap.bundle.js):
the outer Map is declared at line 10, `set` starts at line 12, the conflict
diagnostic is on line 15, `get` is on line 17, and `remove` starts at line 18.
[bootstrap-data-probe.py](../../tools/check/bootstrap-data-probe.py) preserves
the 1,101-byte vendor fragment, SHA-256
`3eebf5b59691eff3c32478e06fb44a5c9eb554bc045d2ff2e20901813a9f99c8`.
The remainder of the factory is replaced with `return e`; this is an exact
Data prerequisite probe, not complete Bootstrap initialization.

At `031f62a`, the three provider-read probes resolve four calls: factory
`fn$3`, absent `get` (`fn$5`), absent `remove` (`fn$6`), and the first `set`
(`fn$4`). Only the first two method invocations have completed summaries.
Each contains one empty-Map `has` read. UMD branch counts are 2/5/6 for
CommonJS/browser/realm fallback, and native admission remains 0/7 in each.
The existing three boxed differentials pass 19/19/24 observations with GC
stress; Node independently matches those 62 observations.

The CommonJS baseline was checked against the devbox artifact
`build/ctcompile/test/bootstrap-host-prefix/commonjs_provider_reads/evidence.json`
and its `prepared.mlir`. Its program SHA-256 is
`cc6c3960099f201b3e6c17f556c4cc5e08a7a47190e08cfba5628be40ea0a181`.
The read-only mode's public boundary is the first `Data.set` call. Inside
`fn$4`, that reader derives `outer.has(element) == false`, then stops at
`ctjs.get_property outer["set"]`. The subsequent source operations are:

```mlir
%outer = ctjs.load_upvalue %arg2[0]
%set = ctjs.get_property %outer[%key_set]
%Map = ctjs.load_global "Map"
%inner = ctjs.construct %Map(%Map)
%stored = ctjs.call %set(%outer, %arg3, %inner)
```

The actual IR then reads `outer.get(element)`, tests the inner Map's key and
size, and calls `inner.set(key, value)`. Mutation following carries the proved
post-state across a successful normal return. These operations remain in the
original method body, including allocation and failure behavior. Later reads
use committed state; they never fall back to an empty-Map assumption after a
mutation.

## Implemented scope and measured progress

The prefix interpreter supports finite, known private Map state through standard
zero-argument nested Map construction and `has`, `get`, `size`, `set` and
`delete`. Source keys may be supported primitives or already proved fresh
ordinary object identities. Values may be supported primitives or private
nested Maps. Ordinary object payloads, arbitrary callbacks, snapshots, errors
and escaping resources remain outside this slice. A summary returns a proved
primitive and leaves every source method and observer branch at runtime.

All three unchanged exact programs now resolve 13 calls: one factory, eleven
completed method invocations, and the conflicting `set` that reaches the next
unsupported effect. These measurements confirm the earlier design's predicted
four-to-thirteen advance; the historical read-only result above is unchanged.

| Entry call after the factory | Summarized normal result | Provider state/effect |
|---|---|---|
| `get(absent, "bs.alert")` | null | outer remains empty |
| `remove(absent, "bs.alert")` | undefined | outer remains empty |
| `set(element, "bs.alert", 42)` | undefined | allocate inner A; two Map.set calls |
| `set(other, "bs.alert", 21)` | undefined | allocate distinct inner B; two Map.set calls |
| `get(element, "bs.alert")` | 42 | read inner A |
| `get(other, "bs.alert")` | 21 | read inner B |
| `set(element, "bs.alert", 43)` | undefined | replace existing entry in A; one Map.set call |
| `get(element, "bs.alert")` | 43 | read the replacement |
| `get(element, "bs.missing")` | null | missing inner get returns undefined; source fallback returns null |
| `remove(element, "bs.missing")` | undefined | one unsuccessful Map.delete call; A stays nonempty |
| `get(element, "bs.alert")` | 43 | failed deletion preserved A |
| `set(element, "bs.collapse", 99)` | no summary | stop at the conflict arm's `load_global "console"` |

The eleven completed summaries contain 31 provider reads, two nested
allocations, five executed `set` operations and one executed `delete` attempt.
That delete returns false and does not remove an entry. The report also retains
one factory resource allocation, three capture edges and one publication.
Nested allocations belong to their method invocation reports.
The diagnostic boundary in each mode is
``unsupported provider path at `ctjs.load_global` (console)``.

| Exact mode | UMD branches | Resolved calls | Completed provider calls | Native claimed / refused | Observations |
|---|---:|---:|---:|---|---:|
| CommonJS | 2 | 13 | 11 | 0 / 7 | 19 |
| Browser | 5 | 13 | 11 | 0 / 7 | 19 |
| Browser with undefined globalThis and distinct self | 6 | 13 | 11 | 0 / 7 | 24 |

Vendor and program hashes match the corresponding read-only probes. Node
v26.8.1 matches all 62 observations. The three boxed differential CTests also
pass all 62 observations against the interpreter under ordinary execution and
GC stress, with generated script/wrapper entries and interpreter-dispatched
factory/method bodies. The native refusals still concern script `this`, boxed
wrapper parameters, the open method-table shape and implicit closure ownership.
Neither the summaries nor these boxed checks claim native Bootstrap execution.

## State and identity schema

Provider state is separate from the prefix's ordinary object heap. Its internal
identities come from live IR and the executed path:

| State | Required identity and contents |
|---|---|
| Factory invocation | Existing one-based invocation token carried by the actual closure value; bound to the exact factory CallOp |
| Captured root | `(factory invocation, captured ConstructOp)` maps to one global provider MapId through the checked final cell/capture edge |
| Nested allocation | Fresh MapId per executed method invocation and ConstructOp occurrence; retain both the method CallOp and source ConstructOp as provenance |
| Ordinary object key | Existing prefix object token; aliases share a token, separate CreateObject executions do not |
| Primitive key | Tagged undefined/null/boolean/number/string value with SameValueZero equality |
| Map state | Insertion-ordered entries of `(KnownKey, KnownValue)`; known values are primitives or MapIds |
| Builtin member value | `(MapId, member)` obtained by a checked live property lookup, used with the matching receiver at its call |

Neither a ConstructOp nor a function symbol alone identifies an allocation:
the two exact `set` invocations execute the same inner ConstructOp but must
produce A and B. Likewise, copying the first factory's method into a second
table retains the first factory's capture token. Object tokens and Map IDs
occupy separate tagged domains: object token zero is valid; Map IDs start at
one, with zero reserved for no Map result.

Map key comparison uses neither coercion nor truthiness. Numbers use
SameValueZero: NaNs compare equal, and signed zeros compare equal; boolean,
number and string keys remain different. Stored numeric zero keys are
canonicalized to positive zero. Object keys compare only their proved tokens;
reading their fields or invoking `valueOf`/`toString` is unnecessary and
unauthorized. The mutation mode refuses unknown keys, including lookup in an
empty Map. The separate read-only mode retains its empty-Map lookup rule.
Unproved equality never becomes a proved absence.

Replacement preserves insertion position, deletion removes the entry, and
reinsertion appends a new entry. This state representation does not authorize
`keys`, iterators or Array.from yet. Map values stay within the proved private
resource graph; Map-valued cycles and Map-valued keys are refused.
An object key's token describes identity and retention, not an acyclic native
owner for that object's reachable fields.

## Checked operation rules

The existing `closed-source-v1` promise of unmodified initial intrinsic
prototypes and explicit initial Map identity remains required. All source
replacement, reflection, descriptor, unknown-effect and receiver guards remain.
The source's standard Map binding must reach construction directly with the
same callee and new.target and no arguments. Accepting a provider name in a
report or manifest must never replace this source proof.

| Operation | Required proof and abstract normal-result behavior |
|---|---|
| Captured load | Actual invoked closure's implicit callee argument and checked capture index resolve the correct final cell/MapId |
| `new Map()` | Fresh, empty MapId in the current transaction; runtime allocation/failure retained |
| Member lookup | Proved private Map with no own override and still-standard prototype; only the supported member names |
| `has`/`get` | Matching builtin member and actual Map receiver, one evaluated key; known membership or stop |
| `size` | Standard getter on the proved Map; number of live entries |
| `set` | Matching member/receiver, two evaluated operands, supported key/value; update or insert; return receiver MapId locally |
| `delete` | Matching member/receiver, one evaluated key; remove if present and return boolean |
| Method return | Known primitive only; Map/object/method/closure escape remains refused |

The source argument evaluation has already occurred before a builtin call is
summarized. `set`, `get`, `has` and `delete` do not coerce keys or call user
JavaScript, but unsupported argument expressions still stop when encountered.
Wrong receivers, detached calls, pending new.target, proxies, own method
overrides, unknown providers and aliases to an unproved builtin receive no
special exemption.

The method-path interpreter may use MapIds returned by `get` to reach a nested
Map inside the same checked invocation. It may use a primitive `get` result
after normal return to discover the next source entry call. Returning an
ordinary payload object is a different proof: the compiler would need its
exact live identity, aliases, possible accessors/proxy behavior, mutations
and ownership. The existing source object-key token or a finite set of past
`get` results establishes none of those requirements. In particular, this
slice cannot authorize the later exact `get(...).value` or instance-identity
observation merely because the Map was tracked earlier.

## Transactions and bounded work

1. Resolve the actual entry callee and its immutable captured roots. Snapshot
   the committed provider state and allocation-ID boundary before entering the
   method. The existing live publication table supplies the callee; it is never
   frozen across a source write or replacement.
2. Interpret only the selected path into a transaction-local state. Reads see
   preceding writes in that transaction. Record builtin effects and fresh
   allocations locally. Do not append reusable-body branches or calls to the
   global rewrite plan.
3. Commit only after a supported primitive normal return, complete resource
   retention checks and sufficient remaining work. Atomically publish all
   changed Map states, fresh IDs, result and summary facts. An accepted method
   cannot expose a private Map through a return, property, global, closure or
   unknown call. Every newly allocated Map must be reachable through proved
   captured Map entries at normal return. Even an otherwise harmless unretained
   temporary allocation refuses this first slice.
4. On a selected unsupported write, call, property read, throw, escape or
   control shape, discard the entire attempted summary and stop the prefix at
   that method invocation. Keep previously completed prefix facts, but supply
   no post-state, partial effects or subsequent-call proof for this invocation.
   Do not resume from the old pre-call state after rejecting a mutating path.
5. Allocation/Map.set failure paths remain runtime. The post-state is
   conditional on normal return; it must not be applied in a catch/exception
   continuation. Exception handling and reentry across this boundary are
   unsupported. Budget exhaustion or an invalid/stale contract discards the
   entire analysis result, including earlier candidate rewrites.

The existing `max-steps` budget charges every visited operation, capture edge,
key comparison, copied state entry, insertion/removal, allocation and
resource-graph edge. A transaction clones the complete committed state and
charges the copy before allocating it. Structured region nesting is bounded
at 64; loops, recursive calls and repeated unproved contexts stay unsupported.
ID/count overflow is checked before allocation. Work exhaustion exposes no
speculative heap or report facts.

## Implementation and reports

`HostContract/ProviderState.h/.cpp` implements the identity, SameValueZero and
transactional Map-state helpers. `PrefixFactories.cpp` registers successful
captured roots in that state. The new `PrefixMutations.cpp` interprets method
paths against transaction-local state. `PrefixReads.cpp` retains the existing
read-only semantics; both use the bounded structured-region walker in
`ProviderPaths.h`. The mutation mode routes every subsequent read through
committed state rather than through the empty-Map reader.

`HostPrefix.h`, the prefix consumer and `Passes.td` expose the opt-in mode.
The live result binds exact entry/method/factory calls and resource identities.
`PrefixPass.cpp` reports their provenance before any rewrite:

| Report field | Meaning |
|---|---|
| `provider_reads` | Legacy read-only summaries; mutation mode uses `provider_calls` for reads too |
| `provider_calls` | Completed mutation-mode invocations, including invocations that only read |
| `summarized_provider_calls` | Total rows in `provider_reads` and `provider_calls` |
| `runtime_provider_reads` | Executed `has`, `get` and `size` operations in completed summaries |
| `runtime_provider_mutations` | Executed `set` and `delete` attempts in completed summaries, including unsuccessful deletion |
| `runtime_provider_allocations` | Factory allocations only |
| `runtime_nested_provider_allocations` | Nested allocations in completed method summaries |
| `provider_boundary` | Diagnostic operation at an unsupported method path; no effect authority |

Each `provider_calls` row records `target`, `call_operation`, zero-based
`factory_index`, primitive `result`, `allocations` and `operations`.
An allocation row records its one-based `map_id`, source
`allocation_operation` and allocating `invocation_operation`. An operation
row also records its own `operation`, `member`, `result` and `result_map_id`.
Its allocation/invocation pair identifies the receiver Map's origin, which may
be an earlier factory or method call. Operation ordinals refer to the global
module walk before rewriting; repeated source allocation sites retain distinct
Map IDs and allocating calls.

Primitive results are printed CTJS attributes. A Map-valued operation result,
such as `set`'s receiver, uses JSON null for `result` and a positive
`result_map_id`; primitive results have `result_map_id` zero. A refused attempt
supplies no row, allocation or post-state, although its resolved boundary target
can remain in the prefix's call list. Reports are diagnostics, not admission
authority; `full_host_contract_claimed` remains false. This mode adds no native
carrier, ownership or admission rule.

The mutation mode's additional emitted change names further actual entry call
targets. It preserves callee values, raw receivers, argument evaluation and
call order. Reusable methods, observer branches, source stores, construction
and provider operations stay unchanged. Unknown later calls or accessors still
stop discovery, and the existing in-place shared-body continuation guard is
unchanged. An ordinary call's effective `this` stays unknown.

## Focused semantic and lifetime checks

`host-provider-mutations.test` passes 38 source cases plus contract, rerun and
work-limit controls. Independent Node execution matches all 38 source oracles.
The tests cover:

- `set`/`get`/`has`/`size`/`delete`, replacement, failed deletion and reinsertion;
  positive sizes 1 and 2; primitive tags; NaN and signed-zero keys; aliased and
  distinct object keys; nested Map sharing and replacement; two factories;
  copied methods; and live method/table replacement. NaN and signed-zero
  fixtures normalize exact constants in prepared IR, while a separate
  arithmetic case confirms unsupported source arithmetic still refuses.
- Unknown keys/values, ordinary object payloads, returned resources or methods,
  detached receivers, provider/prototype/own-member replacement, mutable
  captures, resource cycles and unretained allocations. A write followed by an
  unknown call, throw, property access or escape supplies no partial summary,
  state or later-call proof.
- Byte-for-byte preservation of reusable method bodies and unchanged observer
  branches and runtime operations. Stale fingerprints, forged attributes,
  undeclared providers and omitted option prerequisites supply no facts. Fresh
  analyses do not reuse state. Work-limit probes verify withholding at the
  completion boundary, including earlier summaries and tentative allocations.

The standalone `ProviderState` test passes key equality, insertion order,
allocation provenance, resource-cycle and transaction checks. It exhausts
budgets through lookup, replacement, insertion, deletion, allocation, size and
complete state copying, checking failure leaves state and outputs unchanged.
The existing `host-provider-reads.test` also passes its 23 source cases and
controls with the original read-only boundary.

The exact driver checks the measured counts above, source hashes and native
denominator, unchanged reusable method bodies and observer branches, and
allocation/operation provenance. Its three mutation differential CTests pass
with runtime factory/provider operations and GC stress. These are boxed
semantic/lifetime checks, not a standalone native or RAII claim. Reproduce the
independent exact Node observations with:

```sh
python3 tools/check/bootstrap-host-prefix.py \
  --bootstrap ctbrowser/vendor/bootstrap/bootstrap.bundle.js \
  --mode browser --follow-publication --follow-provider-reads \
  --follow-provider-mutations --node node --oracle-only \
  --work /tmp/bootstrap-provider-mutations-node
```

The CMake mutation fixtures require Node for this independent oracle. Set
`CTCOMPILE_BOOTSTRAP_NODE` to its executable when it is outside the standard
search path or the devbox's `~/tools/node-*/bin` directories.

## Remaining effect and ownership boundary

The newly reachable conflict arm begins with console lookup, followed in the
actual IR by error member lookup, Array.from member lookup, Map.keys lookup
and call, Array.from call, indexed extraction, concatenation, then error call.
The initial Array identity alone does not authorize the snapshot or console
effects. A source-created console object does not prove its callback harmless:
the callback can mutate publication, reenter Data, throw or change provider
state. No Map state or current export target may be carried across it without
a separate checked effect/reentry proof. The measured 4-to-13 target advance
therefore leaves all error-path effects and full native Bootstrap admission
unfinished. The existing error-order, accessor and reentrant-error host oracles
remain separate semantic checks; they do not authorize traversal across those
effects.
